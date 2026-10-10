// Holds one zwlr_output_power_v1 per output, the way an idle daemon such as
// swayidle does, and prints every mode event it receives, starting with the
// state each object reports on creation. stdin lines "on <output>" and
// "off <output>" send set_mode, "quit" (or EOF) exits, so a check can drive
// and observe power transitions from outside the client.

#include "wlr-output-power-management-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <poll.h>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

namespace {
  struct OutputEntry {
    std::string name;
    wl_output* output = nullptr;
    uint32_t outputVersion = 0;
    zwlr_output_power_v1* power = nullptr;
  };

  struct State {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    zwlr_output_power_manager_v1* manager = nullptr;
    std::vector<std::unique_ptr<OutputEntry>> outputs;
  };

  void powerMode(void* data, zwlr_output_power_v1*, uint32_t mode) {
    const auto* entry = static_cast<OutputEntry*>(data);
    std::println("mode {} {}", entry->name, mode == ZWLR_OUTPUT_POWER_V1_MODE_ON ? "on" : "off");
    std::fflush(stdout);
  }

  void powerFailed(void* data, zwlr_output_power_v1*) {
    auto* entry = static_cast<OutputEntry*>(data);
    std::println("failed {}", entry->name);
    std::fflush(stdout);
    // The protocol asks the client to destroy a control once it failed.
    zwlr_output_power_v1_destroy(entry->power);
    entry->power = nullptr;
  }

  constexpr zwlr_output_power_v1_listener kPowerListener{
      .mode = powerMode,
      .failed = powerFailed,
  };

  void
  outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
  void outputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
  void outputDone(void*, wl_output*) {}
  void outputScale(void*, wl_output*, int32_t) {}
  void outputName(void* data, wl_output*, const char* name) {
    static_cast<OutputEntry*>(data)->name = name != nullptr ? name : "";
  }
  void outputDescription(void*, wl_output*, const char*) {}

  constexpr wl_output_listener kOutputListener{
      .geometry = outputGeometry,
      .mode = outputMode,
      .done = outputDone,
      .scale = outputScale,
      .name = outputName,
      .description = outputDescription,
  };

  void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* state = static_cast<State*>(data);
    if (std::strcmp(interface, zwlr_output_power_manager_v1_interface.name) == 0) {
      state->manager = static_cast<zwlr_output_power_manager_v1*>(
          wl_registry_bind(registry, name, &zwlr_output_power_manager_v1_interface, 1)
      );
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
      // The name event, used to address commands per output, needs version 4.
      auto entry = std::make_unique<OutputEntry>();
      entry->outputVersion = std::min(version, 4U);
      entry->output =
          static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, entry->outputVersion));
      wl_output_add_listener(entry->output, &kOutputListener, entry.get());
      state->outputs.push_back(std::move(entry));
    }
  }
  void registryRemove(void*, wl_registry*, uint32_t) {}
  constexpr wl_registry_listener kRegistryListener{.global = registryGlobal, .global_remove = registryRemove};

  void sendSetMode(State& state, std::string_view requested, bool powered) {
    const auto entry = std::ranges::find_if(state.outputs, [requested](const std::unique_ptr<OutputEntry>& candidate) {
      return candidate->name == requested;
    });
    if (entry == state.outputs.end() || (*entry)->power == nullptr) {
      std::println(stderr, "output-power-client: unknown output: {}", requested);
      return;
    }
    zwlr_output_power_v1_set_mode(
        (*entry)->power, powered ? ZWLR_OUTPUT_POWER_V1_MODE_ON : ZWLR_OUTPUT_POWER_V1_MODE_OFF
    );
    wl_display_flush(state.display);
  }

  void handleLine(State& state, std::string_view line) {
    const auto space = line.find(' ');
    if (space == std::string_view::npos) {
      std::println(stderr, "output-power-client: unrecognized command: {}", line);
      return;
    }
    const std::string_view op = line.substr(0, space);
    const std::string_view requested = line.substr(space + 1);
    if (op == "on") {
      sendSetMode(state, requested, true);
    } else if (op == "off") {
      sendSetMode(state, requested, false);
    } else {
      std::println(stderr, "output-power-client: unrecognized command: {}", line);
    }
  }

  // Dispatches Wayland events while also accepting one command per stdin line,
  // so a check can drive set_mode against a client that stays connected.
  bool pump(State& state) {
    std::string pending;
    const int displayFd = wl_display_get_fd(state.display);
    while (true) {
      wl_display_flush(state.display);
      pollfd fds[2] = {
          {.fd = displayFd, .events = POLLIN, .revents = 0},
          {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
      };
      if (poll(fds, 2, -1) < 0) {
        return false;
      }
      if ((fds[0].revents & POLLIN) != 0 && wl_display_dispatch(state.display) < 0) {
        return false;
      }
      if ((fds[1].revents & (POLLIN | POLLHUP)) == 0) {
        continue;
      }
      char buffer[256];
      const ssize_t got = ::read(STDIN_FILENO, buffer, sizeof(buffer));
      if (got <= 0) {
        return true;
      }
      pending.append(buffer, static_cast<size_t>(got));
      for (size_t newline = pending.find('\n'); newline != std::string::npos; newline = pending.find('\n')) {
        const std::string_view line = pending.substr(0, newline);
        if (line == "quit") {
          return true;
        }
        handleLine(state, line);
        pending.erase(0, newline + 1);
      }
    }
  }
} // namespace

int main() {
  State state;
  state.display = wl_display_connect(nullptr);
  if (state.display == nullptr) {
    std::println(stderr, "output-power-client: cannot connect");
    return EXIT_FAILURE;
  }
  state.registry = wl_display_get_registry(state.display);
  wl_registry_add_listener(state.registry, &kRegistryListener, &state);
  bool ready = false;
  for (int roundtrip = 0; roundtrip < 4 && !ready; ++roundtrip) {
    if (wl_display_roundtrip(state.display) < 0) {
      break;
    }
    ready =
        state.manager != nullptr && std::ranges::all_of(state.outputs, [](const std::unique_ptr<OutputEntry>& entry) {
          return !entry->name.empty();
        });
  }
  if (!ready || state.outputs.empty()) {
    std::println(stderr, "output-power-client: manager or named outputs did not arrive");
    return EXIT_FAILURE;
  }

  for (const std::unique_ptr<OutputEntry>& entry : state.outputs) {
    entry->power = zwlr_output_power_manager_v1_get_output_power(state.manager, entry->output);
    zwlr_output_power_v1_add_listener(entry->power, &kPowerListener, entry.get());
    std::println("power {}", entry->name);
    std::fflush(stdout);
  }

  const bool dispatched = pump(state);
  for (const std::unique_ptr<OutputEntry>& entry : state.outputs) {
    if (entry->power != nullptr) {
      zwlr_output_power_v1_destroy(entry->power);
    }
    if (entry->outputVersion >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
      wl_output_release(entry->output);
    } else {
      wl_output_destroy(entry->output);
    }
  }
  zwlr_output_power_manager_v1_destroy(state.manager);
  wl_registry_destroy(state.registry);
  wl_display_disconnect(state.display);
  return dispatched ? EXIT_SUCCESS : EXIT_FAILURE;
}

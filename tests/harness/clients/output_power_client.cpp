// Observe live output power controls and drive them with stdin commands:
// "on NAME", "off NAME", "sync LABEL" (a server roundtrip), and "quit".
// "retain NAME" keeps a control across global withdrawal for policy-rejection
// tests; "release NAME" releases it before rediscovery.
#include "wlr-output-power-management-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
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
  struct State;

  struct OutputEntry {
    State* state = nullptr;
    uint32_t global = 0;
    std::string name;
    wl_output* output = nullptr;
    uint32_t version = 0;
    zwlr_output_power_v1* power = nullptr;
    bool failed = false;
    bool retained = false;
  };

  struct State {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    zwlr_output_power_manager_v1* manager = nullptr;
    uint32_t managerGlobal = 0;
    // Retired entries stay allocated until disconnect: queued callbacks must
    // never refer to freed listener data or select an old same-name control.
    std::vector<std::unique_ptr<OutputEntry>> outputs;
  };

  void powerMode(void* data, zwlr_output_power_v1*, uint32_t mode) {
    const auto* entry = static_cast<OutputEntry*>(data);
    std::println("mode {} {}", entry->name, mode == ZWLR_OUTPUT_POWER_V1_MODE_ON ? "on" : "off");
    std::fflush(stdout);
  }

  void powerFailed(void* data, zwlr_output_power_v1* power) {
    auto* entry = static_cast<OutputEntry*>(data);
    std::println("failed {}", entry->name);
    std::fflush(stdout);
    zwlr_output_power_v1_destroy(power);
    entry->power = nullptr;
    entry->failed = true;
  }

  constexpr zwlr_output_power_v1_listener kPowerListener{.mode = powerMode, .failed = powerFailed};

  void createControl(OutputEntry& entry) {
    if (entry.state->manager == nullptr
        || entry.output == nullptr
        || entry.name.empty()
        || entry.power != nullptr
        || entry.failed) {
      return;
    }
    entry.power = zwlr_output_power_manager_v1_get_output_power(entry.state->manager, entry.output);
    zwlr_output_power_v1_add_listener(entry.power, &kPowerListener, &entry);
    std::println("power {}", entry.name);
    std::fflush(stdout);
  }

  void retireOutput(OutputEntry& entry) {
    if (entry.power != nullptr) {
      zwlr_output_power_v1_destroy(entry.power);
      entry.power = nullptr;
    }
    if (entry.output != nullptr) {
      if (entry.version >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
        wl_output_release(entry.output);
      } else {
        wl_output_destroy(entry.output);
      }
      entry.output = nullptr;
    }
  }

  void
  outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
  void outputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
  void outputDone(void*, wl_output*) {}
  void outputScale(void*, wl_output*, int32_t) {}
  void outputName(void* data, wl_output*, const char* name) {
    auto* entry = static_cast<OutputEntry*>(data);
    entry->name = name != nullptr ? name : "";
    createControl(*entry);
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
      state->managerGlobal = name;
      state->manager = static_cast<zwlr_output_power_manager_v1*>(
          wl_registry_bind(registry, name, &zwlr_output_power_manager_v1_interface, 1)
      );
      for (const auto& entry : state->outputs) {
        createControl(*entry);
      }
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
      auto entry = std::make_unique<OutputEntry>();
      entry->state = state;
      entry->global = name;
      entry->version = std::min(version, 4U);
      entry->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, entry->version));
      wl_output_add_listener(entry->output, &kOutputListener, entry.get());
      state->outputs.push_back(std::move(entry));
    }
  }

  void registryRemove(void* data, wl_registry*, uint32_t name) {
    auto* state = static_cast<State*>(data);
    if (name == state->managerGlobal) {
      zwlr_output_power_manager_v1_destroy(state->manager);
      state->manager = nullptr;
      state->managerGlobal = 0;
    }
    for (const auto& entry : state->outputs) {
      if (entry->global == name && entry->output != nullptr) {
        std::println("removed {}", entry->name);
        std::fflush(stdout);
        if (!entry->retained) {
          retireOutput(*entry);
        }
      }
    }
  }
  constexpr wl_registry_listener kRegistryListener{.global = registryGlobal, .global_remove = registryRemove};

  bool handleLine(State& state, std::string_view line) {
    const size_t space = line.find(' ');
    const std::string_view op = line.substr(0, space);
    if (space != std::string_view::npos) {
      const std::string_view requested = line.substr(space + 1);
      if (op == "sync") {
        if (wl_display_roundtrip(state.display) < 0) {
          return false;
        }
        std::println("synced {}", requested);
        std::fflush(stdout);
        return true;
      }
      if (op == "on" || op == "off" || op == "retain" || op == "release") {
        const auto entry = std::ranges::find_if(state.outputs, [requested](const auto& candidate) {
          return candidate->output != nullptr && candidate->power != nullptr && candidate->name == requested;
        });
        if (entry == state.outputs.end()) {
          std::println(stderr, "output-power-client: no live control: {}", requested);
          return false;
        }
        if (op == "retain") {
          (*entry)->retained = true;
          return true;
        }
        if (op == "release") {
          retireOutput(**entry);
          return true;
        }
        zwlr_output_power_v1_set_mode(
            (*entry)->power, op == "on" ? ZWLR_OUTPUT_POWER_V1_MODE_ON : ZWLR_OUTPUT_POWER_V1_MODE_OFF
        );
        return true;
      }
    }
    std::println(stderr, "output-power-client: unrecognized command: {}", line);
    return false;
  }

  bool pump(State& state) {
    std::string pending;
    const int displayFd = wl_display_get_fd(state.display);
    while (true) {
      while (wl_display_prepare_read(state.display) != 0) {
        if (wl_display_dispatch_pending(state.display) < 0) {
          return false;
        }
      }
      const bool blocked = wl_display_flush(state.display) < 0;
      if (blocked && errno != EAGAIN) {
        wl_display_cancel_read(state.display);
        return false;
      }
      pollfd fds[2] = {
          {.fd = displayFd, .events = static_cast<short>(POLLIN | (blocked ? POLLOUT : 0)), .revents = 0},
          {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
      };
      if (poll(fds, 2, -1) < 0) {
        wl_display_cancel_read(state.display);
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if ((fds[0].revents & POLLIN) != 0) {
        if (wl_display_read_events(state.display) < 0) {
          return false;
        }
      } else {
        wl_display_cancel_read(state.display);
      }
      if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 || wl_display_dispatch_pending(state.display) < 0) {
        return false;
      }
      if ((fds[1].revents & (POLLIN | POLLHUP)) == 0) {
        continue;
      }
      char buffer[256];
      const ssize_t got = ::read(STDIN_FILENO, buffer, sizeof(buffer));
      if (got <= 0) {
        return got == 0;
      }
      pending.append(buffer, static_cast<size_t>(got));
      for (size_t newline = pending.find('\n'); newline != std::string::npos; newline = pending.find('\n')) {
        const std::string line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (line == "quit") {
          return true;
        }
        if (!handleLine(state, line)) {
          return false;
        }
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
  bool ready =
      wl_display_roundtrip(state.display) >= 0 && wl_display_roundtrip(state.display) >= 0 && state.manager != nullptr;
  if (ready) {
    std::println("ready");
    std::fflush(stdout);
    ready = pump(state);
  } else {
    std::println(stderr, "output-power-client: manager did not arrive");
  }
  for (const auto& entry : state.outputs) {
    retireOutput(*entry);
  }
  if (state.manager != nullptr) {
    zwlr_output_power_manager_v1_destroy(state.manager);
  }
  wl_registry_destroy(state.registry);
  wl_display_disconnect(state.display);
  return ready ? EXIT_SUCCESS : EXIT_FAILURE;
}

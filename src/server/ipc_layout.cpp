#include "layout/scrolling.h"
#include "output/output.h"
#include "scene/ssd_frame_geometry.h"
#include "server/ipc_commands.h"
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"
#include "workspace/workspace.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>
#include <random>

namespace umbriel {
  namespace {
    using Json = nlohmann::json;

    const std::string& instance() {
      static const std::string id = [] {
        std::random_device random;
        return std::format("{:08x}{:08x}{:08x}{:08x}", random(), random(), random(), random());
      }();
      return id;
    }

    Json box(const wlr_box& value) {
      return {{"x", value.x}, {"y", value.y}, {"width", value.width}, {"height", value.height}};
    }

    std::string id(const View* view) {
      const char* value = view->extForeignIdentifier();
      return value != nullptr ? value : "";
    }

    Json weights(const Column& column) {
      return {{"rows", column.heightWeights}, {"top", column.topGapWeight}, {"bottom", column.bottomGapWeight}};
    }

    Json columnState(Workspace& workspace, int index, const Layout& preview) {
      const auto& scrolling = *workspace.scrollingLayout();
      const Column& column = scrolling.columns()[static_cast<size_t>(index)];
      const auto& config = workspace.layoutConfig();
      const bool vertical = workspace.scrollingVertical();
      const auto area = workspace.tiledArea();
      const int cross = std::max(1, (vertical ? area.width : area.height) - 2 * config.edgePad);
      const int units = static_cast<int>(column.tabs.unitCount(column.views.size()));
      const int span = std::max(units, cross - std::max(0, units - 1) * config.totalGap);
      Json members = Json::array();
      Json geometry = Json::array();
      bool supported =
          column.tabs.empty() && !column.views.empty() && column.heightWeights.size() == column.views.size();
      for (const View* view : column.views) {
        const auto constraints = scrolling.constraintsFor(view);
        const auto target = preview.targetBox(view);
        const auto content = contentBoxInTile(target, view->titleBarHeight());
        supported = supported
            && view->mapped()
            && view->tiled()
            && !id(view).empty()
            && !view->scheduledFullscreen()
            && !view->scheduledMaximized()
            && !view->maximizedToEdges()
            && !view->pinned();
        members.push_back(
            {{"id", id(view)},
             {"title_bar_height", view->titleBarHeight()},
             {"constraints",
              {{"min_width", constraints.minWidth},
               {"min_height", constraints.minHeight},
               {"max_width", constraints.maxWidth},
               {"max_height", constraints.maxHeight}}},
             {"fullscreen", view->scheduledFullscreen()},
             {"maximized", view->scheduledMaximized()},
             {"maximized_to_edges", view->maximizedToEdges()},
             {"floating", view->floating()}}
        );
        geometry.push_back(
            {{"id", id(view)},
             {"target_tile", box(target)},
             {"target_content", box(content)},
             {"committed_content", box(view->committedContentBox())}}
        );
      }
      double total = column.topGapWeight + column.bottomGapWeight;
      for (size_t row = 0; row < column.heightWeights.size(); row = column.tabs.unitEnd(row)) {
        total += std::max(0.05, column.heightWeights[row]);
      }
      const int top = total > 0 ? static_cast<int>(std::lround(column.topGapWeight / total * span)) : 0;
      const int bottom = total > 0 ? static_cast<int>(std::lround(column.bottomGapWeight / total * span)) : 0;
      // The basis excludes transient committed pixels and scroll positions. It includes all inputs to this
      // column's secondary allocation, including the exact native weights even when rounding hides their change.
      Json basis{
          {"instance", instance()},
          {"workspace", workspace.id()},
          {"output", workspace.group()->output()->wlr()->name},
          {"column", index},
          {"axis", vertical ? "horizontal" : "vertical"},
          {"members", std::move(members)},
          {"weights", weights(column)},
          {"area", box(area)},
          {"edge_pad", config.edgePad},
          {"gap", config.totalGap},
          {"width_fraction", column.widthFrac},
          {"primary_extent", scrolling.columnWidth(index, workspace.scrollViewportExtent())},
          {"tabbed", !column.tabs.empty()},
          {"supported", supported}
      };
      return {
          {"basis", std::move(basis)},
          {"geometry", std::move(geometry)},
          {"cross_span", span},
          {"row_budget", span - top - bottom},
          {"top_gap", top},
          {"bottom_gap", bottom}
      };
    }

    Json failure(std::string_view reason) { return {{"err", reason}}; }
  } // namespace

  nlohmann::json IpcCommands::layoutState(Server& server, IpcArgs) {
    Json columns = Json::array();
    Json unsupported = Json::array();
    for (const auto& output : server.outputs()) {
      const auto* group = output->workspaceGroup();
      if (group == nullptr) {
        continue;
      }
      for (size_t i = 0; i < group->workspaceCount(); ++i) {
        auto* workspace = group->workspaceAt(i);
        if (workspace == nullptr) {
          continue;
        }
        const auto* scrolling = workspace->scrollingLayout();
        auto preview = workspace->previewArrangedLayout();
        if (scrolling == nullptr || preview == nullptr) {
          unsupported.push_back(workspace->id());
          continue;
        }
        for (size_t column = 0; column < scrolling->columns().size(); ++column) {
          columns.push_back(columnState(*workspace, static_cast<int>(column), *preview));
        }
      }
    }
    Json active = nullptr;
    for (const auto& view : server.views()) {
      if (view->mapped() && view->activated()) {
        active = id(view.get());
        break;
      }
    }
    return {
        {"ok",
         {{"version", 1},
          {"instance", instance()},
          {"active", active},
          {"columns", std::move(columns)},
          {"unsupported_workspaces", std::move(unsupported)}}}
    };
  }

  nlohmann::json IpcCommands::layoutAllocate(Server& server, const nlohmann::json& request) {
    if (!request.is_object()
        || request.value("cmd", "") != "layout-allocate"
        || !request.contains("target")
        || !request["target"].is_string()
        || !request.contains("expected")
        || !request["expected"].is_object()
        || !request.contains("weights")
        || !request["weights"].is_object()) {
      return failure("malformed layout allocation");
    }
    View* target = nullptr;
    for (const auto& view : server.views()) {
      if (view->mapped() && id(view.get()) == request["target"].get<std::string>()) {
        target = view.get();
        break;
      }
    }
    if (target == nullptr || target->workspace() == nullptr || !target->tiled()) {
      return failure("layout target unavailable");
    }
    auto& workspace = *target->workspace();
    if (workspace.group() == nullptr || workspace.group()->output() == nullptr) {
      return failure("layout output unavailable");
    }
    auto* scrolling = workspace.scrollingLayout();
    if (scrolling == nullptr || scrolling->columnOf(target) < 0) {
      return failure("unsupported layout");
    }
    const int index = scrolling->columnOf(target);
    auto preview = workspace.previewArrangedLayout();
    if (preview == nullptr) {
      return failure("layout preview unavailable");
    }
    const Json before = columnState(workspace, index, *preview);
    if (!before["basis"]["supported"].get<bool>()) {
      return failure("unsupported column");
    }
    if (before["basis"] != request["expected"]) {
      return failure("stale layout basis");
    }
    if (request.contains("expected_active")) {
      if (!request["expected_active"].is_string() || request["expected_active"] != id(target) || !target->activated()) {
        return failure("active window changed");
      }
    }
    const auto& value = request["weights"];
    if (!value.contains("rows")
        || !value["rows"].is_array()
        || !value.contains("top")
        || !value["top"].is_number()
        || !value.contains("bottom")
        || !value["bottom"].is_number()
        || !std::ranges::all_of(value["rows"], [](const Json& row) { return row.is_number(); })) {
      return failure("malformed weights");
    }
    const auto rows = value["rows"].get<std::vector<double>>();
    const double top = value["top"].get<double>();
    const double bottom = value["bottom"].get<double>();
    // Preview all effects using the same layout implementation before touching the authoritative allocation.
    auto proposed = *scrolling;
    if (!proposed.setColumnWeights(index, rows, top, bottom)) {
      return failure("invalid weights");
    }
    proposed.arrange(workspace.tiledArea());
    // A requested pixel vector is optional: when present, reject clamping/rounding mismatch atomically.
    if (request.contains("target_content_extents")) {
      const auto& extents = request["target_content_extents"];
      const auto& members = scrolling->columns()[static_cast<size_t>(index)].views;
      if (!extents.is_array() || extents.size() != members.size()) {
        return failure("invalid target extents");
      }
      for (size_t i = 0; i < members.size(); ++i) {
        const auto content = contentBoxInTile(proposed.targetBox(members[i]), members[i]->titleBarHeight());
        const int actual = workspace.scrollingVertical() ? content.width : content.height;
        if (!extents[i].is_number_integer() || extents[i] != actual) {
          return failure("target extents not representable");
        }
      }
    }
    if (!scrolling->setColumnWeights(index, rows, top, bottom)) {
      return failure("invalid weights");
    }
    workspace.markArrange();
    server.scheduleIpcWindowsEvent();
    return {
        {"ok",
         {{"version", 1},
          {"previous_weights", before["basis"]["weights"]},
          {"accepted", columnState(workspace, index, proposed)}}}
    };
  }
} // namespace umbriel

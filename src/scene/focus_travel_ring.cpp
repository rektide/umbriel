#include "scene/focus_travel_ring.h"

#include "config/config.h"
#include "scene/border_rect.h"
#include "scene/color.h"

// clang-format off
#include "wlr.h"
// clang-format on

namespace umbriel {

  FocusTravelRing::FocusTravelRing(wlr_scene_tree* parent) : m_parent(parent) {}

  FocusTravelRing::~FocusTravelRing() {
    if (m_tree != nullptr) {
      wlr_scene_node_destroy(&m_tree->node);
      m_tree = nullptr;
      m_border = nullptr;
    }
  }

  void FocusTravelRing::ensureScene() {
    if (m_tree != nullptr) {
      return;
    }
    m_tree = wlr_scene_tree_create(m_parent);
    float innerColor[4];
    float outerColor[4];
    premultiplied(innerColor, config().colors.border.focused, 1.0F);
    premultiplied(outerColor, config().colors.border.outer, 1.0F);
    m_border = wlr_scene_border_create(m_tree, innerColor, outerColor);
    wlr_scene_node_set_enabled(&m_tree->node, false);
  }

  void FocusTravelRing::present(const wlr_box& contentBox) {
    if (contentBox.width <= 0 || contentBox.height <= 0) {
      hideImmediate();
      return;
    }
    ensureScene();
    const auto& appearance = config().appearance;
    applyBorderGeometry(
        m_border,
        makeBorderRing(
            contentBox.width, contentBox.height, appearance.cornerRadius, appearance.borderWidth,
            appearance.outerBorderWidth
        ),
        appearance.borderWidth, appearance.outerBorderWidth
    );
    // Colors are refreshed every present, not only at node creation: a theme change between journeys must not
    // resurrect a stale ring, and the ring always draws what a real focused border would draw.
    float innerColor[4];
    float outerColor[4];
    premultiplied(innerColor, config().colors.border.focused, 1.0F);
    premultiplied(outerColor, config().colors.border.outer, 1.0F);
    wlr_scene_border_set_colors(m_border, innerColor, outerColor);
    // applyBorderGeometry positions the border relative to the content origin: the ring's render margin hangs
    // outside contentBox, matching how view borders wrap their windows.
    wlr_scene_node_set_position(&m_tree->node, contentBox.x, contentBox.y);
    if (!m_visible) {
      wlr_scene_node_set_enabled(&m_tree->node, true);
      m_visible = true;
    }
    m_box = contentBox;
    // Above every workspace member, including floating views, for the rest of this tick's frame.
    wlr_scene_node_raise_to_top(&m_tree->node);
  }

  void FocusTravelRing::hideImmediate() {
    if (m_tree != nullptr && m_visible) {
      wlr_scene_node_set_enabled(&m_tree->node, false);
    }
    m_visible = false;
    m_box = wlr_box{};
  }

} // namespace umbriel

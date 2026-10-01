#pragma once

#include "view/border_ring.h"

extern "C" {
#include <wlr/util/box.h>
}

struct wlr_scene_border;
struct wlr_scene_tree;

namespace umbriel {

  // The traveling focus ring: a compositor-owned border drawn over the workspace's windows while a leading-focus
  // travel runs. Pure presentation. It owns no timeline and no destination reference: the workspace that borrows the
  // shared layout motion calls present() once per geometry tick with the box to draw and hides it when travel ends or
  // cancels, so a disappearing window can never leave this presenter holding a stale pointer.
  class FocusTravelRing {
  public:
    explicit FocusTravelRing(wlr_scene_tree* parent);
    ~FocusTravelRing();
    FocusTravelRing(const FocusTravelRing&) = delete;
    FocusTravelRing& operator=(const FocusTravelRing&) = delete;

    // Draw the ring around `contentBox` in the parent tree's coordinate space. Creates the scene nodes on first use.
    void present(const wlr_box& contentBox);
    // Erase the ring on the next frame; safe to call repeatedly.
    void hideImmediate();

    // The last presented content box; the ring's visible position when travel retargets from what is onscreen.
    [[nodiscard]] const wlr_box& box() const { return m_box; }
    [[nodiscard]] bool visible() const { return m_visible; }

  private:
    void ensureScene();

    wlr_scene_tree* m_parent = nullptr;
    wlr_scene_tree* m_tree = nullptr;
    wlr_scene_border* m_border = nullptr;
    wlr_box m_box{};
    bool m_visible = false;
  };

} // namespace umbriel

#pragma once

extern "C" {
#include <wlr/util/box.h>
}

#include <optional>

namespace umbriel {

  // One leading-focus travel journey along a scrolling strip. All boxes share one output/workspace coordinate space;
  // `verticalStrip` selects the axis the scrolling layout translates along. The names match the geometry contract:
  // - origin:        O, the visible box the ring leaves (usually the previously focused view as presented at request
  // time);
  // - destPresented: D0, the destination's presented box when the pan began, possibly offscreen;
  // - destFinal:     D1, the destination's final layout box, where the ring must land exactly;
  // - lead:          L in [0, 1], how far the ring may run ahead of the camera.
  struct FocusTravelPath {
    wlr_box origin{};
    wlr_box destPresented{};
    wlr_box destFinal{};
    bool verticalStrip = false;
    double lead = 0.0;
  };

  // The path fraction the ring travels by at camera progress `progress`: q = p + L*p*(1-p). Bounded to [0, 1] and
  // monotonic in p; q equals p at both endpoints, so lead never displaces the supported endpoints. Out-of-range
  // progress and lead clamp before use.
  [[nodiscard]] double focusTravelFraction(double progress, double lead);

  // The ring's content box at camera progress `progress` before viewport constraint, edges rounded like
  // interpolateBox. Exact at 0 (origin) and 1 (destFinal); may lie partly offscreen in between. The strip-axis
  // position follows the travel contract mix(O, D1, p) + (q - p) * (D0 - O); the cross-axis position and both sizes
  // morph with q so the ring becomes the destination slightly ahead of the camera and settles with it.
  [[nodiscard]] wlr_box focusTravelBox(const FocusTravelPath& path, double progress);

  // Pixels the drawn border extends beyond the ring's content box on every side: the border stroke plus the one
  // render-margin pixel makeBorderRing reserves for fractional outer coverage. Zero when no stroke is drawn.
  [[nodiscard]] int focusTravelStrokeExtent(int innerWidth, int outerWidth);

  // The ring's content box at camera progress, translated — never resized — so the ring together with its complete
  // border stroke stays inside `viewport`, whatever distance the journey spans. Journeys whose origin or final
  // destination cannot fit the viewport at all skip travel: the function returns nullopt rather than a clipped or
  // repositioned box that claims an endpoint fidelity the geometry does not have. Nullopt as well for a degenerate
  // viewport or a degenerate origin or final destination box.
  [[nodiscard]] std::optional<wlr_box> constrainedFocusTravelBox(
      const FocusTravelPath& path, double progress, const wlr_box& viewport, int innerWidth, int outerWidth
  );

  // Whether the journey's natural endpoints survive constraining exactly: the ring leaves the origin's box and meets
  // the destination's final box without translation. An endpoint that merely overlaps the viewport edge fits the
  // viewport but would draw shifted, offset from the border the journey promises to meet, so callers refuse the
  // journey rather than claim an exact handoff.
  [[nodiscard]] bool
  focusTravelEndpointsPreserved(const FocusTravelPath& path, const wlr_box& viewport, int innerWidth, int outerWidth);

} // namespace umbriel

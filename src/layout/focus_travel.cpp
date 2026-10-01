#include "layout/focus_travel.h"

#include <algorithm>
#include <cmath>

namespace umbriel {

  namespace {

    double mix(double from, double to, double fraction) { return from + (to - from) * fraction; }

    // Edges are rounded independently and width is right - left, matching interpolateBox: two boxes that do not cross
    // in reals do not cross after rounding, and rounding stays the last step after all floating-point geometry.
    wlr_box roundEdges(double left, double top, double right, double bottom) {
      const int roundedLeft = static_cast<int>(std::lround(left));
      const int roundedTop = static_cast<int>(std::lround(top));
      const int roundedRight = static_cast<int>(std::lround(right));
      const int roundedBottom = static_cast<int>(std::lround(bottom));
      return {
          roundedLeft,
          roundedTop,
          std::max(0, roundedRight - roundedLeft),
          std::max(0, roundedBottom - roundedTop),
      };
    }

  } // namespace

  double focusTravelFraction(double progress, double lead) {
    const double p = std::clamp(progress, 0.0, 1.0);
    const double l = std::clamp(lead, 0.0, 1.0);
    return std::clamp(p + l * p * (1.0 - p), 0.0, 1.0);
  }

  wlr_box focusTravelBox(const FocusTravelPath& path, double progress) {
    const double p = std::clamp(progress, 0.0, 1.0);
    const double q = focusTravelFraction(p, path.lead);
    const wlr_box& origin = path.origin;
    const wlr_box& presented = path.destPresented;
    const wlr_box& final = path.destFinal;

    // Strip axis runs along the layout's scroll direction. Only the strip-axis position follows the travel contract;
    // the cross-axis position and both extents morph on the lead fraction, so the ring becomes the destination
    // slightly ahead of the camera and still settles on destFinal when the pan does.
    if (path.verticalStrip) {
      const double y = mix(origin.y, final.y, p) + (q - p) * (presented.y - origin.y);
      const double x = mix(origin.x, final.x, q);
      return roundEdges(x, y, x + mix(origin.width, final.width, q), y + mix(origin.height, final.height, q));
    }

    const double x = mix(origin.x, final.x, p) + (q - p) * (presented.x - origin.x);
    const double y = mix(origin.y, final.y, q);
    return roundEdges(x, y, x + mix(origin.width, final.width, q), y + mix(origin.height, final.height, q));
  }

  int focusTravelStrokeExtent(int innerWidth, int outerWidth) {
    const int thickness = std::max(0, innerWidth) + std::max(0, outerWidth);
    return thickness > 0 ? thickness + 1 : 0;
  }

  std::optional<wlr_box> constrainedFocusTravelBox(
      const FocusTravelPath& path, double progress, const wlr_box& viewport, int innerWidth, int outerWidth
  ) {
    if (viewport.width <= 0 || viewport.height <= 0) {
      return std::nullopt;
    }
    if (path.origin.width <= 0 || path.origin.height <= 0 || path.destFinal.width <= 0 || path.destFinal.height <= 0) {
      return std::nullopt;
    }

    const int stroke = focusTravelStrokeExtent(innerWidth, outerWidth);
    const wlr_box raw = focusTravelBox(path, progress);
    const int expandedWidth = raw.width + 2 * stroke;
    const int expandedHeight = raw.height + 2 * stroke;
    // Explicit skip: a ring that cannot fit the viewport on an axis has no honest containment, so travel is refused
    // rather than clipped or pinned to an edge. Interpolated sizes stay between the endpoint sizes, so a journey
    // rejected here is rejected at its endpoints too — callers gate the journey once at bind time the same way.
    if (expandedWidth > viewport.width || expandedHeight > viewport.height) {
      return std::nullopt;
    }

    // Translate, never resize: the whole stroke moves inside the viewport while the ring keeps the exact box the
    // geometry asked for, including across arbitrarily long journeys whose raw box lands far outside.
    const int left = std::clamp(raw.x - stroke, viewport.x, viewport.x + viewport.width - expandedWidth);
    const int top = std::clamp(raw.y - stroke, viewport.y, viewport.y + viewport.height - expandedHeight);
    return wlr_box{left + stroke, top + stroke, raw.width, raw.height};
  }

} // namespace umbriel

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
    const int left = raw.x - stroke;
    const int top = raw.y - stroke;
    const int right = raw.x + raw.width + stroke;
    const int bottom = raw.y + raw.height + stroke;

    // Squeeze against the edges so the complete stroke stays visible; a box that cannot fit pins to the leading edge,
    // which keeps most of it onscreen without pretending the ring is contained.
    int constrainedLeft = std::clamp(left, viewport.x, viewport.x + viewport.width);
    int constrainedRight = std::clamp(right, viewport.x, viewport.x + viewport.width);
    int constrainedTop = std::clamp(top, viewport.y, viewport.y + viewport.height);
    int constrainedBottom = std::clamp(bottom, viewport.y, viewport.y + viewport.height);
    if (right - left > viewport.width) {
      constrainedLeft = viewport.x;
      constrainedRight = constrainedLeft + (right - left);
    }
    if (bottom - top > viewport.height) {
      constrainedTop = viewport.y;
      constrainedBottom = constrainedTop + (bottom - top);
    }

    const wlr_box constrained{
        constrainedLeft + stroke,
        constrainedTop + stroke,
        std::max(0, constrainedRight - constrainedLeft - 2 * stroke),
        std::max(0, constrainedBottom - constrainedTop - 2 * stroke),
    };
    if (constrained.width <= 0 || constrained.height <= 0) {
      return std::nullopt;
    }
    return constrained;
  }

} // namespace umbriel

#include "layout/layout_motion.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace umbriel {

  namespace {

    int lerpEdge(int from, int to, double progress) {
      return static_cast<int>(std::lround(from + (to - from) * progress));
    }

  } // namespace

  wlr_box interpolateBox(const wlr_box& from, const wlr_box& to, double progress) {
    const int left = lerpEdge(from.x, to.x, progress);
    const int top = lerpEdge(from.y, to.y, progress);
    const int right = lerpEdge(from.x + from.width, to.x + to.width, progress);
    const int bottom = lerpEdge(from.y + from.height, to.y + to.height, progress);
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
  }

  bool keepsSeparation(const MotionBox& a, const MotionBox& b) {
    // `lo` entirely on the low side of `hi` along one axis, touching allowed.
    const auto leftOf = [](const wlr_box& lo, const wlr_box& hi) { return lo.x + lo.width <= hi.x; };
    const auto above = [](const wlr_box& lo, const wlr_box& hi) { return lo.y + lo.height <= hi.y; };
    return (leftOf(a.from, b.from) && leftOf(a.to, b.to))
        || (leftOf(b.from, a.from) && leftOf(b.to, a.to))
        || (above(a.from, b.from) && above(a.to, b.to))
        || (above(b.from, a.from) && above(b.to, a.to));
  }

  bool isStripPan(const std::span<const MotionBox> members, const bool verticalStrip) {
    if (members.empty()) {
      return false;
    }
    const MotionBox& first = members.front();
    if (first.from.width != first.to.width || first.from.height != first.to.height) {
      return false;
    }
    const int delta = verticalStrip ? first.to.y - first.from.y : first.to.x - first.from.x;
    for (const MotionBox& member : members) {
      if (member.from.width != member.to.width || member.from.height != member.to.height) {
        return false;
      }
      if (verticalStrip ? member.from.x != member.to.x : member.from.y != member.to.y) {
        return false;
      }
      const int memberDelta = verticalStrip ? member.to.y - member.from.y : member.to.x - member.from.x;
      if (memberDelta != delta) {
        return false;
      }
    }
    return true;
  }

} // namespace umbriel

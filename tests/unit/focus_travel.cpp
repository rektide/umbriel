#include "layout/focus_travel.h"

#include "check.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using umbriel::constrainedFocusTravelBox;
using umbriel::focusTravelBox;
using umbriel::focusTravelFraction;
using umbriel::FocusTravelPath;
using umbriel::focusTravelStrokeExtent;

namespace {

  bool sameBox(const wlr_box& a, const wlr_box& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  }

  double mix(double from, double to, double fraction) { return from + (to - from) * fraction; }

  double centerX(const wlr_box& box) { return box.x + box.width / 2.0; }

  double centerY(const wlr_box& box) { return box.y + box.height / 2.0; }

  // Ring plus stroke footprint, as the constraint defines it.
  struct Footprint {
    int x;
    int y;
    int width;
    int height;
  };

  Footprint footprint(const wlr_box& box, int stroke) {
    return {box.x - stroke, box.y - stroke, box.width + 2 * stroke, box.height + 2 * stroke};
  }

  // Wide viewport around a horizontal strip journey; the viewport origin is deliberately non-zero.
  constexpr wlr_box kViewport{1920, 1080, 2000, 1200};
  constexpr wlr_box kVisibleOrigin{2000, 1200, 600, 1000};
  constexpr wlr_box kFarRightFinal{3400, 1300, 500, 900};
  constexpr wlr_box kFarRightPresented{5600, 1300, 500, 900};

  FocusTravelPath horizontalJourney(double lead) {
    return {
        .origin = kVisibleOrigin,
        .destPresented = kFarRightPresented,
        .destFinal = kFarRightFinal,
        .verticalStrip = false,
        .lead = lead,
    };
  }

} // namespace

UMBRIEL_TEST(focusTravelFractionIsBoundedMonotonicAndExactAtEndpoints) {
  for (const double lead : {0.0, 0.25, 0.6, 1.0, 5.0, -2.0}) {
    double previous = focusTravelFraction(-1.0, lead);
    CHECK_EQ(previous, 0.0);
    for (int step = 0; step <= 100; ++step) {
      const double fraction = focusTravelFraction(step / 100.0, lead);
      CHECK(fraction >= previous);
      CHECK(fraction >= 0.0);
      CHECK(fraction <= 1.0);
      previous = fraction;
    }
    CHECK_EQ(previous, 1.0);
    // The endpoints never move, whatever the lead.
    CHECK_EQ(focusTravelFraction(0.0, lead), 0.0);
    CHECK_EQ(focusTravelFraction(1.0, lead), 1.0);
  }
}

UMBRIEL_TEST(focusTravelEndpointsMeetOriginAndFinalBoxOnBothStripAxes) {
  const FocusTravelPath horizontal = horizontalJourney(0.6);
  CHECK(sameBox(focusTravelBox(horizontal, 0.0), kVisibleOrigin));
  CHECK(sameBox(focusTravelBox(horizontal, 1.0), kFarRightFinal));

  const FocusTravelPath vertical{
      .origin = kVisibleOrigin,
      .destPresented = wlr_box{2100, 3400, 600, 500},
      .destFinal = wlr_box{2200, 1600, 600, 500},
      .verticalStrip = true,
      .lead = 0.6,
  };
  CHECK(sameBox(focusTravelBox(vertical, 0.0), kVisibleOrigin));
  CHECK(sameBox(focusTravelBox(vertical, 1.0), vertical.destFinal));
}

UMBRIEL_TEST(focusTravelWithZeroLeadMirrorsCameraProgress) {
  const FocusTravelPath path = horizontalJourney(0.0);
  for (int step = 0; step <= 20; ++step) {
    const double progress = step / 20.0;
    const wlr_box box = focusTravelBox(path, progress);
    CHECK_EQ(box.x, static_cast<int>(std::lround(mix(kVisibleOrigin.x, kFarRightFinal.x, progress))));
    CHECK_EQ(box.y, static_cast<int>(std::lround(mix(kVisibleOrigin.y, kFarRightFinal.y, progress))));
    CHECK_EQ(box.width, static_cast<int>(std::lround(mix(kVisibleOrigin.width, kFarRightFinal.width, progress))));
    CHECK_EQ(box.height, static_cast<int>(std::lround(mix(kVisibleOrigin.height, kFarRightFinal.height, progress))));
  }
}

UMBRIEL_TEST(focusTravelLeadDrivesStripPositionAheadOfCameraAndBackToFinal) {
  const FocusTravelPath path = horizontalJourney(0.6);
  for (int step = 1; step < 20; ++step) {
    const double progress = step / 20.0;
    const wlr_box led = focusTravelBox(path, progress);
    const wlr_box plain = focusTravelBox(horizontalJourney(0.0), progress);
    // Leading: at or beyond the plain camera path, on the destination-facing side.
    CHECK(led.x >= plain.x);
    // The bump grows then returns to zero: near the end the ring is back on the camera path.
    if (progress >= 0.9) {
      CHECK(led.x - plain.x < (kFarRightPresented.x - kVisibleOrigin.x) / 8);
    }
  }
}

UMBRIEL_TEST(constrainedFocusTravelStaysInsideViewportThroughLongHorizontalJump) {
  const FocusTravelPath path = horizontalJourney(0.6);
  const int stroke = focusTravelStrokeExtent(2, 2);
  CHECK_EQ(stroke, 5); // 2 + 2 border widths plus the one render margin pixel

  double previousCenter = centerX(kVisibleOrigin);
  for (int step = 0; step <= 40; ++step) {
    const double progress = step / 40.0;
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, progress, kViewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    const Footprint drawn = footprint(*box, stroke);
    CHECK(drawn.x >= kViewport.x);
    CHECK(drawn.y >= kViewport.y);
    CHECK(drawn.x + drawn.width <= kViewport.x + kViewport.width);
    CHECK(drawn.y + drawn.height <= kViewport.y + kViewport.height);
    // Correct direction: early flight advances toward the destination, and the whole path never falls back left of
    // the segment between origin and final centres (the tail retreats only toward the final handoff).
    if (progress <= 0.5) {
      CHECK(centerX(*box) + 1e-6 >= previousCenter - 1e-6);
    }
    CHECK(centerX(*box) + 1e-6 >= std::min(centerX(kVisibleOrigin), centerX(kFarRightFinal)) - 1.0);
    previousCenter = centerX(*box);
    if (progress > 0.0 && progress < 0.5) {
      // A destination presented far right projects the ring onto the destination-facing edge early.
      CHECK(centerX(*box) > centerX(kVisibleOrigin));
    }
  }
  // The supported endpoints survive constraining unchanged: both are fully inside this viewport.
  const std::optional<wlr_box> start = constrainedFocusTravelBox(path, 0.0, kViewport, 2, 2);
  const std::optional<wlr_box> end = constrainedFocusTravelBox(path, 1.0, kViewport, 2, 2);
  CHECK(start.has_value() && end.has_value());
  if (start && end) {
    CHECK(sameBox(*start, kVisibleOrigin));
    CHECK(sameBox(*end, kFarRightFinal));
  }
}

UMBRIEL_TEST(constrainedFocusTravelHandlesLongJumpUpward) {
  const FocusTravelPath path{
      .origin = wlr_box{2100, 1600, 800, 600},
      .destPresented = wlr_box{2200, 4200, 700, 500},
      .destFinal = wlr_box{2200, 1400, 700, 500},
      .verticalStrip = true,
      .lead = 1.0,
  };
  for (int step = 0; step <= 40; ++step) {
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, step / 40.0, kViewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    const Footprint drawn = footprint(*box, focusTravelStrokeExtent(2, 2));
    CHECK(drawn.y >= kViewport.y);
    CHECK(drawn.y + drawn.height <= kViewport.y + kViewport.height);
    CHECK(drawn.x >= kViewport.x);
    CHECK(drawn.x + drawn.width <= kViewport.x + kViewport.width);
    CHECK(centerY(*box) >= centerY(path.destFinal) - 1);
  }
}

UMBRIEL_TEST(constrainedFocusTravelRejectsDegenerateViewport) {
  const FocusTravelPath path = horizontalJourney(0.6);
  CHECK(!constrainedFocusTravelBox(path, 0.5, wlr_box{0, 0, 0, 100}, 2, 2).has_value());
  CHECK(!constrainedFocusTravelBox(path, 0.5, wlr_box{10, 10, 100, 0}, 2, 2).has_value());
  // A valid viewport still constrains the same journey fine.
  CHECK(constrainedFocusTravelBox(path, 0.5, kViewport, 2, 2).has_value());
}

UMBRIEL_TEST(constrainedFocusTravelRejectsDegenerateJourneyBoxes) {
  FocusTravelPath path = horizontalJourney(0.6);
  path.origin = wlr_box{2000, 1200, 0, 500};
  CHECK(!constrainedFocusTravelBox(path, 0.5, kViewport, 2, 2).has_value());
  path.origin = kVisibleOrigin;
  path.destFinal = wlr_box{3400, 1300, 500, 0};
  CHECK(!constrainedFocusTravelBox(path, 0.5, kViewport, 2, 2).has_value());
  // A degenerate presented box alone is survivable: the constraint works from origin and final boxes.
  path.destFinal = kFarRightFinal;
  path.destPresented = wlr_box{5600, 1300, 0, 0};
  CHECK(constrainedFocusTravelBox(path, 0.0, kViewport, 2, 2).has_value());
}

UMBRIEL_TEST(constrainedFocusTravelPinsOversizedRingToLeadingEdgeDeterministically) {
  FocusTravelPath path = horizontalJourney(0.6);
  path.origin = wlr_box{2000, 1200, 2400, 1100}; // wider than the 2000px viewport
  path.destPresented = wlr_box{5600, 1300, 2400, 900};
  path.destFinal = wlr_box{3400, 1300, 2400, 900};
  const std::optional<wlr_box> first = constrainedFocusTravelBox(path, 0.5, kViewport, 2, 2);
  const std::optional<wlr_box> second = constrainedFocusTravelBox(path, 0.5, kViewport, 2, 2);
  CHECK(first.has_value() && second.has_value());
  if (first && second) {
    CHECK(sameBox(*first, *second));
    CHECK_EQ(first->x, kViewport.x + focusTravelStrokeExtent(2, 2));
    // Mostly visible: the pinned ring still covers the viewport, overflowing right by the excess.
    CHECK(first->width > kViewport.width / 2);
  }
}

UMBRIEL_TEST(constrainedFocusTravelClampsOutOfRangeProgress) {
  const FocusTravelPath path = horizontalJourney(0.6);
  const std::optional<wlr_box> before = constrainedFocusTravelBox(path, -3.0, kViewport, 2, 2);
  const std::optional<wlr_box> atStart = constrainedFocusTravelBox(path, 0.0, kViewport, 2, 2);
  const std::optional<wlr_box> after = constrainedFocusTravelBox(path, 7.0, kViewport, 2, 2);
  const std::optional<wlr_box> atEnd = constrainedFocusTravelBox(path, 1.0, kViewport, 2, 2);
  CHECK(before.has_value() && atStart.has_value() && after.has_value() && atEnd.has_value());
  if (before && atStart) {
    CHECK(sameBox(*before, *atStart));
  }
  if (after && atEnd) {
    CHECK(sameBox(*after, *atEnd));
  }
}

UMBRIEL_TEST(focusTravelStrokeExtentHandlesZeroWidths) {
  CHECK_EQ(focusTravelStrokeExtent(0, 0), 0);
  CHECK_EQ(focusTravelStrokeExtent(3, 0), 4);
  CHECK_EQ(focusTravelStrokeExtent(0, 3), 4);
}

int main() { return RUN_TESTS(); }

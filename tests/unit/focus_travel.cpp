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

UMBRIEL_TEST(constrainedFocusTravelStaysVisibleThroughArbitrarilyLongJumpRight) {
  // The parent review's failing case: a short camera journey whose destination is presented absurdly far right.
  // The raw ring leaves the viewport entirely; the constraint must still return a visible, dimension-preserving box.
  const FocusTravelPath path{
      .origin = wlr_box{28, 50, 340, 500},
      .destPresented = wlr_box{30000, 50, 340, 500},
      .destFinal = wlr_box{330, 50, 340, 500},
      .verticalStrip = false,
      .lead = 0.6,
  };
  const wlr_box viewport{0, 0, 1000, 700};
  const int stroke = focusTravelStrokeExtent(2, 2);
  for (int step = 0; step <= 40; ++step) {
    const double progress = step / 40.0;
    const wlr_box raw = focusTravelBox(path, progress);
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, progress, viewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    // Dimensions are preserved exactly; only the position translates.
    CHECK_EQ(box->width, raw.width);
    CHECK_EQ(box->height, raw.height);
    const Footprint drawn = footprint(*box, stroke);
    CHECK(drawn.x >= viewport.x);
    CHECK(drawn.y >= viewport.y);
    CHECK(drawn.x + drawn.width <= viewport.x + viewport.width);
    CHECK(drawn.y + drawn.height <= viewport.y + viewport.height);
  }
  // Mid-flight the raw box is entirely offscreen right, yet the ring rides just inside the destination-facing edge.
  const wlr_box rawMid = focusTravelBox(path, 0.5);
  CHECK(rawMid.x > viewport.x + viewport.width);
  const std::optional<wlr_box> mid = constrainedFocusTravelBox(path, 0.5, viewport, 2, 2);
  CHECK(mid.has_value());
  if (mid) {
    CHECK_EQ(mid->x + mid->width + stroke, viewport.x + viewport.width);
    CHECK(centerX(*mid) > centerX(path.origin));
  }
  // Endpoints stay exact: both fit the viewport, so constraining changes nothing.
  const std::optional<wlr_box> start = constrainedFocusTravelBox(path, 0.0, viewport, 2, 2);
  const std::optional<wlr_box> end = constrainedFocusTravelBox(path, 1.0, viewport, 2, 2);
  CHECK(start.has_value() && end.has_value());
  if (start && end) {
    CHECK(sameBox(*start, path.origin));
    CHECK(sameBox(*end, path.destFinal));
  }
}

UMBRIEL_TEST(constrainedFocusTravelStaysVisibleThroughArbitrarilyLongJumpLeftWithNegativePresented) {
  // Journey direction leftward; the destination was presented far offscreen into negative coordinates.
  const FocusTravelPath path{
      .origin = wlr_box{1720, 50, 340, 500},
      .destPresented = wlr_box{-40000, 50, 340, 500},
      .destFinal = wlr_box{40, 50, 340, 500},
      .verticalStrip = false,
      .lead = 1.0,
  };
  const wlr_box viewport{0, 0, 2000, 700};
  for (int step = 0; step <= 40; ++step) {
    const double progress = step / 40.0;
    const wlr_box raw = focusTravelBox(path, progress);
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, progress, viewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    CHECK_EQ(box->width, raw.width);
    CHECK_EQ(box->height, raw.height);
    const Footprint drawn = footprint(*box, focusTravelStrokeExtent(2, 2));
    CHECK(drawn.x >= viewport.x);
    CHECK(drawn.x + drawn.width <= viewport.x + viewport.width);
    CHECK(drawn.y >= viewport.y);
    CHECK(drawn.y + drawn.height <= viewport.y + viewport.height);
    if (progress > 0.0 && progress <= 0.5) {
      CHECK(centerX(*box) - 1e-6 <= centerX(path.origin) + 1e-6); // moving left early
    }
    CHECK(centerX(*box) + 1e-6 <= std::max(centerX(path.origin), centerX(path.destFinal)) + 1.0);
  }
  const std::optional<wlr_box> mid = constrainedFocusTravelBox(path, 0.5, viewport, 2, 2);
  CHECK(mid.has_value());
  if (mid) {
    // Riding the destination-facing (left) edge: the ring hugs x = viewport left + stroke.
    CHECK_EQ(mid->x, viewport.x + focusTravelStrokeExtent(2, 2));
  }
}

UMBRIEL_TEST(constrainedFocusTravelStaysVisibleThroughLongJourneyDownward) {
  // Vertical strip, journey direction downward, destination presented far below (large positive coordinates).
  const FocusTravelPath path{
      .origin = wlr_box{100, 40, 1800, 300},
      .destPresented = wlr_box{100, 50000, 1600, 280},
      .destFinal = wlr_box{100, 820, 1600, 280},
      .verticalStrip = true,
      .lead = 0.6,
  };
  const wlr_box viewport{0, 0, 2000, 1200};
  for (int step = 0; step <= 40; ++step) {
    const double progress = step / 40.0;
    const wlr_box raw = focusTravelBox(path, progress);
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, progress, viewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    CHECK_EQ(box->width, raw.width);
    CHECK_EQ(box->height, raw.height);
    const Footprint drawn = footprint(*box, focusTravelStrokeExtent(2, 2));
    CHECK(drawn.x >= viewport.x);
    CHECK(drawn.x + drawn.width <= viewport.x + viewport.width);
    CHECK(drawn.y >= viewport.y);
    CHECK(drawn.y + drawn.height <= viewport.y + viewport.height);
  }
  const wlr_box rawMid = focusTravelBox(path, 0.5);
  CHECK(rawMid.y > viewport.y + viewport.height);
  const std::optional<wlr_box> mid = constrainedFocusTravelBox(path, 0.5, viewport, 2, 2);
  CHECK(mid.has_value());
  if (mid) {
    CHECK_EQ(mid->y + mid->height + focusTravelStrokeExtent(2, 2), viewport.y + viewport.height);
  }
}

UMBRIEL_TEST(constrainedFocusTravelStaysVisibleThroughLongJourneyUpwardWithNegativePresented) {
  // Vertical strip, journey direction upward, destination presented far above in negative coordinates.
  const FocusTravelPath path{
      .origin = wlr_box{100, 1800, 1800, 300},
      .destPresented = wlr_box{100, -60000, 1600, 280},
      .destFinal = wlr_box{100, 60, 1600, 280},
      .verticalStrip = true,
      .lead = 1.0,
  };
  const wlr_box viewport{0, 0, 2000, 2400};
  for (int step = 0; step <= 40; ++step) {
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, step / 40.0, viewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    const Footprint drawn = footprint(*box, focusTravelStrokeExtent(2, 2));
    CHECK(drawn.x >= viewport.x);
    CHECK(drawn.x + drawn.width <= viewport.x + viewport.width);
    CHECK(drawn.y >= viewport.y);
    CHECK(drawn.y + drawn.height <= viewport.y + viewport.height);
  }
  const std::optional<wlr_box> mid = constrainedFocusTravelBox(path, 0.5, viewport, 2, 2);
  CHECK(mid.has_value());
  if (mid) {
    CHECK_EQ(mid->y, viewport.y + focusTravelStrokeExtent(2, 2));
  }
}

UMBRIEL_TEST(constrainedFocusTravelWithZeroLeadStaysVisibleOnLongJourney) {
  // Lead zero pins the ring to the camera; even then a far journey must never lose the ring.
  const FocusTravelPath path{
      .origin = wlr_box{28, 50, 340, 500},
      .destPresented = wlr_box{30000, 50, 340, 500},
      .destFinal = wlr_box{1650, 50, 340, 500},
      .verticalStrip = false,
      .lead = 0.0,
  };
  const wlr_box viewport{0, 0, 2000, 700};
  for (int step = 0; step <= 40; ++step) {
    const std::optional<wlr_box> box = constrainedFocusTravelBox(path, step / 40.0, viewport, 2, 2);
    CHECK(box.has_value());
    if (!box) {
      continue;
    }
    const Footprint drawn = footprint(*box, focusTravelStrokeExtent(2, 2));
    CHECK(drawn.x >= viewport.x);
    CHECK(drawn.x + drawn.width <= viewport.x + viewport.width);
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

UMBRIEL_TEST(constrainedFocusTravelSkipsRingsTooLargeForTheViewport) {
  // Explicit skip: an origin wider than the viewport has no honest containment at any progress.
  FocusTravelPath path = horizontalJourney(0.6);
  path.origin = wlr_box{2000, 1200, 2400, 800};
  path.destPresented = wlr_box{5600, 1300, 2400, 900};
  path.destFinal = wlr_box{3400, 1300, 2400, 900};
  CHECK(!constrainedFocusTravelBox(path, 0.0, kViewport, 2, 2).has_value());
  CHECK(!constrainedFocusTravelBox(path, 0.5, kViewport, 2, 2).has_value());
  CHECK(!constrainedFocusTravelBox(path, 1.0, kViewport, 2, 2).has_value());
  // Same for an oversized final box, and for a ring the stroke pushes past the limit by one pixel.
  path.origin = kVisibleOrigin;
  path.destPresented = kFarRightPresented;
  path.destFinal = wlr_box{3400, 1300, kViewport.width - 2 * focusTravelStrokeExtent(2, 2) + 1, 900};
  CHECK(!constrainedFocusTravelBox(path, 1.0, kViewport, 2, 2).has_value());
  // Exactly fitting is honored, and the translation is the identity there.
  path.destFinal = wlr_box{1925, 1300, kViewport.width - 2 * focusTravelStrokeExtent(2, 2), 900};
  const std::optional<wlr_box> end = constrainedFocusTravelBox(path, 1.0, kViewport, 2, 2);
  CHECK(end.has_value());
  if (end) {
    CHECK(sameBox(*end, path.destFinal));
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

UMBRIEL_TEST(focusTravelEndpointsPreservedRequiresExactEndpointIdentity) {
  using umbriel::constrainedFocusTravelBox;
  using umbriel::focusTravelEndpointsPreserved;
  const wlr_box viewport{0, 0, 1000, 700};
  const FocusTravelPath inside{
      .origin = wlr_box{28, 50, 340, 500},
      .destPresented = wlr_box{30000, 50, 340, 500},
      .destFinal = wlr_box{330, 50, 340, 500},
      .verticalStrip = false,
      .lead = 0.6,
  };
  CHECK(focusTravelEndpointsPreserved(inside, viewport, 2, 2));

  // The destination's final box fits the viewport but overlaps its right edge: the constraint translates it, so
  // the journey must be refused rather than hide the ring offset from the border it promises to meet.
  FocusTravelPath edgeOverlap = inside;
  edgeOverlap.destFinal = wlr_box{900, 50, 340, 500};
  const std::optional<wlr_box> constrainedEnd = constrainedFocusTravelBox(edgeOverlap, 1.0, viewport, 2, 2);
  CHECK(constrainedEnd.has_value());
  if (constrainedEnd) {
    CHECK(constrainedEnd->x != edgeOverlap.destFinal.x); // translated, despite fitting dimensions
  }
  CHECK(!focusTravelEndpointsPreserved(edgeOverlap, viewport, 2, 2));

  // Same for an origin that overlaps the left edge.
  FocusTravelPath edgeOrigin = inside;
  edgeOrigin.origin = wlr_box{-100, 50, 340, 500};
  CHECK(!focusTravelEndpointsPreserved(edgeOrigin, viewport, 2, 2));

  // Stroke margins count: a box whose stroke, not content, crosses the edge is translated too.
  FocusTravelPath strokeOverlap = inside;
  strokeOverlap.destFinal = wlr_box{660, 50, 340, 500}; // content fits exactly; the stroke crosses
  CHECK(!focusTravelEndpointsPreserved(strokeOverlap, viewport, 2, 2));

  // Oversized or degenerate endpoints refuse as well.
  FocusTravelPath oversized = inside;
  oversized.destFinal = wlr_box{100, 50, 1200, 500};
  CHECK(!focusTravelEndpointsPreserved(oversized, viewport, 2, 2));
  FocusTravelPath degenerate = inside;
  degenerate.origin = wlr_box{28, 50, 0, 500};
  CHECK(!focusTravelEndpointsPreserved(degenerate, viewport, 2, 2));
}

int main() { return RUN_TESTS(); }

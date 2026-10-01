#include "layout/layout_motion.h"

#include "check.h"

#include <algorithm>
#include <span>
#include <vector>

using umbriel::interpolateBox;
using umbriel::isStripPan;
using umbriel::keepsSeparation;
using umbriel::MotionBox;

namespace {

  constexpr int kGap = 12;

  bool sameBox(const wlr_box& a, const wlr_box& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  }

  // True when the interiors intersect; touching edges do not count.
  bool overlaps(const wlr_box& a, const wlr_box& b) {
    if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) {
      return false;
    }
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
  }

  // The largest gap separating two boxes along one axis; negative when they overlap on both.
  int separation(const wlr_box& a, const wlr_box& b) {
    return std::max({b.x - (a.x + a.width), a.x - (b.x + b.width), b.y - (a.y + a.height), a.y - (b.y + b.height)});
  }

  // Every pair of members stays disjoint, at least `minGap` apart, at every shared progress step.
  void checkDisjointThroughout(std::span<const MotionBox> members, int minGap = 0) {
    for (int step = 0; step <= 20; ++step) {
      const double progress = step * 0.05;
      for (size_t i = 0; i < members.size(); ++i) {
        for (size_t j = i + 1; j < members.size(); ++j) {
          const wlr_box a = interpolateBox(members[i].from, members[i].to, progress);
          const wlr_box b = interpolateBox(members[j].from, members[j].to, progress);
          CHECK(!overlaps(a, b));
          CHECK(separation(a, b) >= minGap);
        }
      }
    }
  }

  void checkAllKeepSeparation(std::span<const MotionBox> members) {
    for (size_t i = 0; i < members.size(); ++i) {
      for (size_t j = i + 1; j < members.size(); ++j) {
        CHECK(keepsSeparation(members[i], members[j]));
      }
    }
  }

  constexpr wlr_box kColumnA{0, 0, 400, 800};
  constexpr wlr_box kColumnB{400 + kGap, 0, 400, 800};
  constexpr wlr_box kColumnC{2 * (400 + kGap), 0, 400, 800};

} // namespace

UMBRIEL_TEST(interpolateBoxEndpointsAreExact) {
  const wlr_box from{3, 7, 100, 50};
  const wlr_box to{-40, 12, 5, 0};
  CHECK(sameBox(interpolateBox(from, to, 0.0), from));
  CHECK(sameBox(interpolateBox(from, to, 1.0), to));
}

UMBRIEL_TEST(interpolateBoxEdgesMoveMonotonically) {
  const wlr_box from{0, 0, 7, 7};
  const wlr_box to{100, 0, 13, 7};
  int lastLeft = from.x;
  int lastRight = from.x + from.width;
  for (int step = 1; step <= 20; ++step) {
    const wlr_box box = interpolateBox(from, to, step * 0.05);
    CHECK(box.x >= lastLeft);
    CHECK(box.x + box.width >= lastRight);
    lastLeft = box.x;
    lastRight = box.x + box.width;
  }
}

UMBRIEL_TEST(establishedColumnsStaySeparateDuringInsertionReflow) {
  const std::vector<MotionBox> peers{
      {{0, 0, 600, 800}, kColumnA},
      {{600 + kGap, 0, 600, 800}, kColumnB},
  };
  checkDisjointThroughout(peers, kGap);
  checkAllKeepSeparation(peers);
}

UMBRIEL_TEST(establishedColumnsStaySeparateDuringClosingReflow) {
  const std::vector<MotionBox> peers{
      {kColumnA, {0, 0, 600, 800}},
      {kColumnC, {600 + kGap, 0, 600, 800}},
  };
  checkDisjointThroughout(peers, kGap);
  checkAllKeepSeparation(peers);
}

UMBRIEL_TEST(keepsSeparationFlagsRearrangements) {
  const int rowHeight = (800 - kGap) / 2;
  // A above B becomes A left of B (expel), C shifts right to make room.
  const MotionBox a{{0, 0, 400, rowHeight}, kColumnA};
  const MotionBox b{{0, rowHeight + kGap, 400, 800 - rowHeight - kGap}, kColumnB};
  const MotionBox c{kColumnB, kColumnC};
  CHECK(!keepsSeparation(a, b));
  CHECK(keepsSeparation(b, c));
  CHECK(keepsSeparation(a, c));
}

UMBRIEL_TEST(stripPanAcceptsUniformTranslationOnOneAxis) {
  const std::vector<MotionBox> pan{
      {{10, 0, 400, 800}, {70, 0, 400, 800}},
      {{420, 0, 400, 800}, {480, 0, 400, 800}},
      {{830, 0, 624, 700}, {890, 0, 624, 700}},
  };
  CHECK(isStripPan(pan, false));
  CHECK(!isStripPan(pan, true));

  const std::vector<MotionBox> vertical{
      {{0, 5, 1280, 300}, {0, 65, 1280, 300}},
      {{0, 315, 1280, 400}, {0, 375, 1280, 400}},
  };
  CHECK(isStripPan(vertical, true));
  CHECK(!isStripPan(vertical, false));
  CHECK(!isStripPan({}, false));
}

UMBRIEL_TEST(stripPanRejectsSameAxisColumnSwap) {
  // Two columns exchanging places: sizes and axis are fine, displacements disagree.
  const std::vector<MotionBox> swap{
      {{10, 0, 400, 800}, {646, 0, 400, 800}},
      {{646, 0, 400, 800}, {10, 0, 400, 800}},
  };
  CHECK(!isStripPan(swap, false));
}

UMBRIEL_TEST(stripPanRejectsResizeCrossAxisDriftAndMixedDisplacement) {
  const std::vector<MotionBox> resized{
      {{10, 0, 400, 800}, {70, 0, 340, 800}},
      {{420, 0, 400, 800}, {480, 0, 400, 800}},
  };
  CHECK(!isStripPan(resized, false));

  const std::vector<MotionBox> drifted{
      {{10, 0, 400, 800}, {70, 0, 400, 800}},
      {{420, 0, 400, 800}, {480, 12, 400, 800}},
  };
  CHECK(!isStripPan(drifted, false));

  // One member moving by a different amount is a rearrangement, not a pan.
  const std::vector<MotionBox> mixed{
      {{10, 0, 400, 800}, {70, 0, 400, 800}},
      {{420, 0, 400, 800}, {600, 0, 400, 800}},
  };
  CHECK(!isStripPan(mixed, false));
}

int main() { return RUN_TESTS(); }

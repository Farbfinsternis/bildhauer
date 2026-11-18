// The tool ring (src/app/tool_ring.h): its size rule, the geometry between
// closed ring and open column, and its states.

#include <cmath>
#include <cstdio>
#include <vector>

#include "app/tool_ring.h"

using namespace bh::viewer;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (ok) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

RingGeometry geometry(int count) {
  RingGeometry g;
  g.count = count;
  g.edge = 100;
  g.centerY = 400;
  g.diameter = 40;
  g.gap = 10;
  g.visible = 0.76f;
  g.margin = 6;
  return g;
}

void testSize() {
  // 11 tools: a column is 13.5 d high plus 4 mm.
  check(near(ringSize(50, 11).diameter, 7), "size: lower limit 7 mm");
  check(near(ringSize(4 + 13.5f * 8.5f, 11).diameter, 8.5f), "size: follows the height");
  check(near(ringSize(400, 11).diameter, 10), "size: upper limit 10 mm");
  check(near(ringSize(120, 11).gap, ringSize(120, 11).diameter / 4), "size: gap a quarter of the diameter");
  // Paintling's 14 tools: 9.33 mm at 165 mm.
  check(near(ringSize(165, 14).diameter, 9.333f, 0.01f), "size: 14 tools at 165 mm");
  RingSettings s;
  check(ringVisible(s, 7) * 7 >= 6 - 1e-4f, "visible: at least 6 mm at 7 mm");
  check(near(ringVisible(s, 10), 0.76f), "visible: default at 10 mm");
}

void testGeometry() {
  for (int count : {3, 11, 14}) {
    const RingGeometry g = geometry(count);
    const float a = g.diameter + g.gap, r = count * a / (2 * 3.14159265f);
    for (int ref = 0; ref < count; ++ref) {
      // Closed: all on one circle of radius r; the reference shows `visible`.
      const std::vector<RingPoint> closed = ringPoints(g, 0, ref);
      const float cx = closed[size_t(ref)].x - r, cy = closed[size_t(ref)].y;
      check(near(closed[size_t(ref)].x + g.diameter / 2 - g.edge, g.visible * g.diameter), "closed: visible part");
      check(near(cy, g.centerY), "closed: reference at the middle");
      for (const RingPoint& p : closed)
        check(near(std::hypot(p.x - cx, p.y - cy), r, 0.01f), "closed: on the circle");
      // Open: an upright, centered, ordered column; independent of the reference.
      const std::vector<RingPoint> open = ringPoints(g, 1, ref);
      const std::vector<RingPoint> open0 = ringPoints(g, 1, 0);
      for (int i = 0; i < count; ++i) {
        check(near(open[size_t(i)].x, g.edge + g.margin + g.diameter / 2), "open: upright at the edge");
        check(near(open[size_t(i)].y, g.centerY + (i - (count - 1) / 2.0f) * a, 0.01f), "open: centered, ordered");
        check(near(open[size_t(i)].x, open0[size_t(i)].x) && near(open[size_t(i)].y, open0[size_t(i)].y, 0.01f),
              "open: same for every reference");
        check(open[size_t(i)].x - g.diameter / 2 >= g.edge, "open: fully visible");
      }
      // Between: neighbors keep their arc distance (chord of the arc a).
      for (float e : {0.2f, 0.5f, 0.8f}) {
        const std::vector<RingPoint> p = ringPoints(g, e, ref);
        const float k = (1 - e) / r, chord = 2 * std::sin(k * a / 2) / k;
        for (int i = 1; i < count; ++i)
          check(near(std::hypot(p[size_t(i)].x - p[size_t(i - 1)].x, p[size_t(i)].y - p[size_t(i - 1)].y), chord, 0.01f),
                "between: equal spacing along the curve");
      }
    }
  }
}

// Runs `seconds` of frames of 1/60 s with the same input.
int run(RingState& st, const RingSettings& s, RingInput in, float seconds) {
  in.dt = 1.0f / 60;
  int picked = -1;
  for (float t = 0; t < seconds; t += in.dt) {
    const int p = st.update(s, in);
    if (p >= 0) picked = p;
    in.picked = -1;
    in.tapIcon = false;
    in.pressOutside = false;
  }
  return picked;
}

void testStates() {
  const RingSettings s;
  RingInput over;
  over.active = 2;
  over.overIcon = over.insideColumn = over.nearIcon = true;
  {
    RingState st;
    st.reset(2);
    run(st, s, over, 0.25f);
    check(st.closed(), "hover: not before the dwell");
    run(st, s, over, 0.1f);
    check(!st.closed(), "hover: opens after the dwell");
    run(st, s, over, 0.5f);
    check(st.open(), "hover: open after the animation");
    // Away less than the delay, then back: stays open.
    RingInput away;
    away.active = 2;
    run(st, s, away, 0.3f);
    run(st, s, over, 0.1f);
    check(st.open(), "away: back in time keeps it open");
    run(st, s, away, 0.5f);
    check(!st.open(), "away: closes after the delay");
    run(st, s, away, 0.5f);
    check(st.closed(), "away: closed after the animation");
  }
  {
    // No pointer (pen out of range) counts as away.
    RingState st;
    st.reset(2);
    run(st, s, over, 1);
    RingInput gone;
    gone.active = 2;
    gone.pointer = false;
    run(st, s, gone, 1);
    check(st.closed(), "pen out of range closes");
  }
  {
    // A tap opens at once and, without a pointer (lifted finger), stays open
    // until a pick; the picked tool becomes the reference while rolling in.
    RingState st;
    st.reset(2);
    RingInput tap;
    tap.active = 2;
    tap.tapIcon = true;
    tap.pointer = false;
    run(st, s, tap, 1.0f / 60);
    check(!st.closed(), "tap: opens at once");
    RingInput none;
    none.active = 2;
    none.pointer = false;
    run(st, s, none, 2);
    check(st.open(), "tap: stays open without a pointer");
    RingInput pick = none;
    pick.picked = 7;
    check(run(st, s, pick, 1.0f / 60) == 7, "pick: returns the tool");
    check(st.reference() == 7 && !st.open(), "pick: rolls in around it");
    none.active = 7;
    run(st, s, none, 1);
    check(st.closed() && st.reference() == 7, "pick: closed on the new tool");
  }
  {
    // Tap beside the sticky ring closes it.
    RingState st;
    st.reset(0);
    RingInput tap;
    tap.tapIcon = true;
    tap.pointer = false;
    run(st, s, tap, 1);
    RingInput beside;
    beside.pointer = false;
    beside.pressOutside = true;
    run(st, s, beside, 1);
    check(st.closed(), "tap beside closes");
  }
  {
    // Reversal mid-animation: progress turns, nothing jumps.
    RingState st;
    st.reset(2);
    RingInput tap;
    tap.active = 2;
    tap.tapIcon = true;
    run(st, s, tap, 0.2f);
    const float mid = st.progress();
    RingInput beside;
    beside.active = 2;
    beside.pressOutside = true;
    run(st, s, beside, 1.0f / 60);
    check(st.progress() < mid && mid - st.progress() < 0.05f, "reverse: continues from where it was");
    check(st.reference() == 2, "reverse: same reference");
  }
  {
    // A stroke near the icon: no opening, the icon fades out and stays
    // hidden until the pointer has left the near distance.
    RingState st;
    st.reset(2);
    RingInput stroke = over;
    stroke.stroke = true;
    run(st, s, stroke, 1);
    check(st.closed(), "stroke: never opens");
    check(st.hidden() && st.iconAlpha() == 0, "stroke: icon faded out");
    RingInput after = over;  // stroke over, pointer still near
    run(st, s, after, 1);
    check(st.closed() && st.hidden(), "stroke: hidden while still near");
    RingInput gone;
    gone.active = 2;
    run(st, s, gone, 0.2f);
    check(!st.hidden() && st.iconAlpha() == 1, "stroke: back after leaving");
    run(st, s, over, 0.4f);
    check(!st.closed(), "stroke: hover opens again afterwards");
  }
  {
    // Keys change the tool: the closed ring shows it.
    RingState st;
    st.reset(2);
    RingInput key;
    key.active = 5;
    run(st, s, key, 1.0f / 60);
    check(st.reference() == 5, "key: closed ring follows the tool");
    check(!st.busy(), "idle: no frames needed");
  }
}

}  // namespace

int main() {
  testSize();
  testGeometry();
  testStates();
  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("tool ring: all passed\n");
  return 0;
}

// The brush menu's geometry (src/app/brush_menu.h): zones and arc values.

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "app/brush_menu.h"

using namespace bh::viewer;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (ok) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// The offset at `angle` (menu degrees) and distance `d` from the center.
void at(float angle, float d, float& dx, float& dy) {
  const float a = angle * 3.14159265f / 180;
  dx = std::sin(a) * d;
  dy = -std::cos(a) * d;
}

MenuHit hitAt(float angle, float d, int segments) {
  float dx, dy;
  at(angle, d, dx, dy);
  return menuHit(dx, dy, MenuRadii(), segments);
}

void testAngles() {
  check(near(menuAngle(0, -1), 0), "angle: up is 0");
  check(near(menuAngle(1, 0), 90), "angle: right is 90");
  check(near(menuAngle(-1, 0), -90), "angle: left is -90");
  check(near(std::fabs(menuAngle(0, 1)), 180), "angle: down is 180");
}

void testZones() {
  const float mid = 120;
  check(hitAt(0, 30, 3).zone == MenuZone::Center, "zone: center");
  check(hitAt(0, 200, 3).zone == MenuZone::Outside, "zone: outside");
  check(hitAt(0, mid, 3).zone == MenuZone::Title, "zone: title");
  check(hitAt(75, mid, 3).zone == MenuZone::Strength, "zone: strength right");
  check(hitAt(-75, mid, 3).zone == MenuZone::Size, "zone: size left");
  check(hitAt(35, mid, 3).zone == MenuZone::None, "zone: gap");
  // Segments left to right along the bottom.
  const MenuHit left = hitAt(-130, mid, 3), middle = hitAt(180, mid, 3), right = hitAt(130, mid, 3);
  check(left.zone == MenuZone::Segment && left.segment == 0, "segment: leftmost");
  check(middle.zone == MenuZone::Segment && middle.segment == 1, "segment: middle");
  check(right.zone == MenuZone::Segment && right.segment == 2, "segment: rightmost");
  check(hitAt(180, mid, 0).zone == MenuZone::None, "segment: none without segments");
  // The angles of a segment contain its hit points.
  for (int n = 1; n <= 4; ++n)
    for (int i = 0; i < n; ++i) {
      float from, to;
      segmentAngles(i, n, from, to);
      const MenuHit h = hitAt((from + to) / 2, mid, n);
      check(h.zone == MenuZone::Segment && h.segment == i, "segment: angles match the hit");
    }
}

void testArcs() {
  for (const bool left : {true, false}) {
    const float s = left ? -1.0f : 1.0f;
    check(near(arcValue(s * kMenuArcTo, left), 0), "arc: bottom end is 0");
    check(near(arcValue(s * kMenuArcFrom, left), 1), "arc: top end is 1");
    check(near(arcValue(s * 75, left), 0.5f), "arc: middle is 0.5");
    check(near(arcValue(s * 10, left), 1), "arc: above the top stays 1");
    check(near(arcValue(s * 150, left), 0), "arc: below the bottom stays 0");
    check(near(arcValue(-s * 10, left), 1), "arc: past the top to the other side stays 1");
    check(near(arcValue(-s * 150, left), 0), "arc: around the bottom stays 0");
    for (float v : {0.0f, 0.3f, 1.0f}) check(near(arcValue(arcAngle(v, left), left), v), "arc: angle round trip");
  }
}

}  // namespace

int main() {
  testAngles();
  testZones();
  testArcs();
  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("brush menu: all passed\n");
  return 0;
}

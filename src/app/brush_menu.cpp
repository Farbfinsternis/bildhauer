#include "app/brush_menu.h"

#include <algorithm>
#include <cmath>

namespace bh::viewer {

float menuAngle(float dx, float dy) { return std::atan2(dx, -dy) * 57.2957795f; }

MenuHit menuHit(float dx, float dy, const MenuRadii& r, int segments, float slack) {
  MenuHit hit;
  const float d = std::hypot(dx, dy);
  if (d < r.inner) { hit.zone = MenuZone::Center; return hit; }
  if (d > r.outer + slack) { hit.zone = MenuZone::Outside; return hit; }
  const float a = menuAngle(dx, dy);
  if (std::fabs(a) <= kMenuTitleHalf) hit.zone = MenuZone::Title;
  else if (a >= kMenuArcFrom && a <= kMenuArcTo) hit.zone = MenuZone::Strength;
  else if (a <= -kMenuArcFrom && a >= -kMenuArcTo) hit.zone = MenuZone::Size;
  else if (std::fabs(a) >= kMenuBottomFrom && segments > 0) {
    // 120..240 clockwise; on screen the leftmost segment ends at 240.
    const float t = a < 0 ? a + 360 : a;
    const float span = (360 - 2 * kMenuBottomFrom) / segments;
    hit.zone = MenuZone::Segment;
    hit.segment = std::clamp(int((360 - kMenuBottomFrom - t) / span), 0, segments - 1);
  }
  return hit;
}

float arcValue(float angle, bool left) {
  // Mirror the left arc onto the right one.
  const float a = left ? -angle : angle;
  if (a < 0 || a > kMenuArcTo) return a < -90 || a > kMenuArcTo ? 0.0f : 1.0f;
  return std::clamp((kMenuArcTo - a) / (kMenuArcTo - kMenuArcFrom), 0.0f, 1.0f);
}

float arcAngle(float value, bool left) {
  const float a = kMenuArcTo - std::clamp(value, 0.0f, 1.0f) * (kMenuArcTo - kMenuArcFrom);
  return left ? -a : a;
}

void segmentAngles(int index, int count, float& from, float& to) {
  const float span = (360 - 2 * kMenuBottomFrom) / std::max(count, 1);
  from = 360 - kMenuBottomFrom - index * span;
  to = from - span;
}

}  // namespace bh::viewer

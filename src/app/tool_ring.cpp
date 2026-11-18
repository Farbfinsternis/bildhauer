#include "app/tool_ring.h"

#include <algorithm>
#include <cmath>

namespace bh::viewer {

RingSize ringSize(float viewHeightMm, int count) {
  const int n = std::max(count, 1);
  // A column of n icons with gap d/4 is (n - 1) * 1.25 d + d high.
  const float d = std::clamp((viewHeightMm - 4) / ((n - 1) * 1.25f + 1), 7.0f, 10.0f);
  return {d, d / 4};
}

float ringVisible(const RingSettings& s, float diameterMm, float minTargetMm) {
  return std::clamp(std::max(s.visible, minTargetMm / diameterMm), 0.0f, 1.0f);
}

std::vector<RingPoint> ringPoints(const RingGeometry& g, float e, int reference) {
  std::vector<RingPoint> points(size_t(std::max(g.count, 0)));
  if (g.count <= 0) return points;
  e = std::clamp(e, 0.0f, 1.0f);
  const float a = g.diameter + g.gap;  // arc between neighboring centers
  const float r = g.count * a / (2 * 3.14159265f);
  const float k = (1 - e) / r;
  // The reference icon: closed it shows `visible` of its diameter at the
  // middle of the view, open it has its place in the column.
  const float closedX = g.edge + g.visible * g.diameter - g.diameter / 2;
  const float openX = g.edge + g.margin + g.diameter / 2;
  const float top = g.centerY - (g.count - 1) * a / 2;
  const float x0 = closedX + (openX - closedX) * e;
  const float y0 = g.centerY + (top + reference * a - g.centerY) * e;
  for (int i = 0; i < g.count; ++i) {
    // Around the closed ring the icons go both ways from the reference, so
    // the far ones meet behind the edge; open, they are in order.
    float s = float(i - reference) * a;
    if (k > 1e-6f) {
      points[size_t(i)] = {x0 - (1 - std::cos(k * s)) / k, y0 + std::sin(k * s) / k};
    } else {
      points[size_t(i)] = {x0, y0 + s};
    }
  }
  return points;
}

float RingState::eased() const {
  const float t = progress_;
  return t * t * (3 - 2 * t);
}

bool RingState::busy() const {
  const bool moving = want_ ? progress_ < 1 : progress_ > 0;
  const bool fading = iconAlpha_ != (hidden_ ? 0.0f : 1.0f);
  return moving || fading || hover_ > 0 || (want_ && away_ > 0);
}

void RingState::reset(int active) {
  *this = RingState();
  reference_ = active;
}

void RingState::show(float progress, int reference) {
  progress_ = progress;
  want_ = progress > 0;
  sticky_ = true;
  reference_ = reference;
}

int RingState::update(const RingSettings& s, const RingInput& in) {
  int picked = -1;
  // Near a running stroke the icon fades out, so that one can paint where it
  // was; it comes back only once the pointer has left the near distance.
  if (in.stroke && in.nearIcon && !want_) hidden_ = true;
  if (hidden_ && !in.nearIcon && !in.stroke) hidden_ = false;
  const float fadeStep = s.fade > 0 ? in.dt / s.fade : 1;
  iconAlpha_ = hidden_ ? std::max(0.0f, iconAlpha_ - fadeStep) : std::min(1.0f, iconAlpha_ + fadeStep);

  if (!want_) {
    // Closed or rolling in: hovering long enough, or a tap, rolls it out.
    if (in.overIcon && !in.stroke && !hidden_) hover_ += in.dt;
    else hover_ = 0;
    const bool tap = in.tapIcon && !in.stroke && !hidden_;
    if (tap || hover_ >= s.dwell) {
      want_ = true;
      sticky_ = tap;
      away_ = 0;
      hover_ = 0;
      // From closed, the curve touches the line at the active icon; a ring
      // that is still rolling in keeps its reference, so nothing jumps.
      if (progress_ <= 0) reference_ = in.active;
    }
  } else {
    hover_ = 0;
    if (in.picked >= 0 && open()) {
      picked = in.picked;
      reference_ = in.picked;  // open, the column does not depend on it
      want_ = false;
    } else {
      if (in.pointer || !sticky_) away_ = in.insideColumn ? 0 : away_ + in.dt;
      if (away_ >= s.closeDelay || in.pressOutside) want_ = false;
    }
    if (!want_) away_ = 0;
  }

  const float step = s.duration > 0 ? in.dt / s.duration : 1;
  progress_ = std::clamp(progress_ + (want_ ? step : -step), 0.0f, 1.0f);
  // Fully open or closed, the reference follows the tool (keys change it too).
  if (progress_ <= 0 || progress_ >= 1) {
    if (picked < 0) reference_ = in.active;
    if (progress_ <= 0) sticky_ = false;
  }
  return picked;
}

}  // namespace bh::viewer

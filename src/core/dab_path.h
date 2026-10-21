#pragma once

// Dab path of a stroke (re/docs/brushes.md 2.3), shared by sculpt and paint
// strokes, and the falloff curve both use (brushes.md 3.2).

#include <algorithm>
#include <cmath>

#include "core/vec3.h"

namespace bh {

// smooth(x): 0 below 0, 1 above 1, a cosine ramp in between.
inline float smoothFalloff(float x) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  return (1 - std::cos(3.14159265f * x)) / 2;
}

// Dab path through the last two stroke samples C (older) and B and the new
// sample A: a cubic Bezier from the midpoint of C-B to the midpoint of B-A
// with its handles pulled toward B, close to the quadratic B-spline of the
// samples.
inline Vec3 dabCurve(const Vec3& c, const Vec3& b, const Vec3& a, float t) {
  const Vec3 k0 = c + (b - c) * 0.5f, k1 = c + (b - c) * 0.85f;
  const Vec3 k2 = a + (b - a) * 0.85f, k3 = a + (b - a) * 0.5f;
  const float u = 1 - t;
  return k0 * (u * u * u) + k1 * (3 * u * u * t) + k2 * (3 * u * t * t) + k3 * (t * t * t);
}

// Places dabs along dabCurve: the curve through the last samples is sampled
// finely, and a dab goes down whenever the path has moved on by `spacing`.
// The older sample only advances once a dab was placed, so slow movement
// accumulates.
class DabPath {
 public:
  explicit DabPath(const Vec3& start) : b_(start), c_(start) {}

  template <class Dab>
  void moveTo(const Vec3& a, float spacing, Dab&& dab) {
    if (first_) b_ = c_ = a;  // the stroke may start off the model
    const int n = std::min(100000, int(length(a - c_) * 10 / spacing));
    Vec3 last = c_;
    bool placed = false;
    for (int i = 0; i <= n; ++i) {
      Vec3 q = dabCurve(c_, b_, a, n > 0 ? float(i) / n : 0.0f);
      if (first_ || length(q - last) > spacing) {
        first_ = false;
        placed = true;
        last = q;
        dab(q);
      }
    }
    if (placed) c_ = b_;
    b_ = a;
  }

 private:
  Vec3 b_, c_;  // last stroke sample and the one before
  bool first_ = true;
};

}  // namespace bh

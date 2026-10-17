#pragma once

// Curved edge midpoint (re/docs/mesh-core.md 2.1): the edge A-B is treated as
// a cubic Bezier curve whose end tangents are the edge direction projected
// into the tangent planes of the vertex normals.

#include "core/vec3.h"

namespace bh {

struct EdgeCurve {
  Vec3 p0, p1, p2, p3;

  Vec3 at(float t) const {
    float u = 1 - t;
    return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
  }
};

inline EdgeCurve edgeCurve(const Vec3& a, const Vec3& na, const Vec3& b, const Vec3& nb) {
  Vec3 ab = b - a;
  float l = length(ab);
  Vec3 d = normalize(ab);
  Vec3 ta = normalize(d - na * dot(na, d));
  Vec3 tb = normalize(-d - nb * dot(nb, -d));
  return {a, a + ta * (l / 3), b + tb * (l / 3), b};
}

inline Vec3 curvedMidpoint(const Vec3& a, const Vec3& na, const Vec3& b, const Vec3& nb) {
  return edgeCurve(a, na, b, nb).at(0.5f);
}

// Normal at the curve midpoint: perpendicular to the curve tangent, in the
// plane spanned by the tangent and the mean normal, facing like the mean
// normal.
inline Vec3 curvedMidpointNormal(const Vec3& a, const Vec3& na, const Vec3& b, const Vec3& nb) {
  EdgeCurve c = edgeCurve(a, na, b, nb);
  Vec3 tangent = normalize(c.at(0.51f) - c.at(0.49f));
  Vec3 mean = normalize(na + nb);
  Vec3 n = normalize(cross(cross(tangent, mean), tangent));
  if (dot(n, mean) < 0) n = -n;
  return length(n) > 0 ? n : mean;
}

}  // namespace bh

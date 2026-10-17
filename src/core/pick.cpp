#include "core/pick.h"

#include <cmath>

namespace bh {

namespace {

// Moeller-Trumbore, both sides; returns t or -1.
float rayTriangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c) {
  Vec3 e1 = b - a, e2 = c - a;
  Vec3 p = cross(d, e2);
  float det = dot(e1, p);
  if (std::fabs(det) < 1e-12f) return -1;
  float inv = 1 / det;
  Vec3 s = o - a;
  float u = dot(s, p) * inv;
  if (u < 0 || u > 1) return -1;
  Vec3 q = cross(s, e1);
  float v = dot(d, q) * inv;
  if (v < 0 || u + v > 1) return -1;
  float t = dot(e2, q) * inv;
  return t > 0 ? t : -1;
}

void test(const Mesh& mesh, const Vec3& o, const Vec3& d, bool mirrored, PickHit& best) {
  for (int i = 0; i < mesh.triangleSlots(); ++i) {
    const Triangle& t = mesh.triangle(i);
    if (!t.active) continue;
    // Bounding sphere early out.
    Vec3 oc = t.centroid - o;
    float along = dot(oc, d);
    if (dot(oc, oc) - along * along > t.radius * t.radius * 1.0001f) continue;
    float hit = rayTriangle(o, d, mesh.vertex(t.v[0]).pos, mesh.vertex(t.v[1]).pos, mesh.vertex(t.v[2]).pos);
    if (hit < 0 || (best.hit && hit >= best.t)) continue;
    best.hit = true;
    best.t = hit;
    best.triangle = i;
    best.point = o + d * hit;
    best.normal = t.normal;
    best.mirrored = mirrored;
  }
}

}  // namespace

PickHit pick(const Mesh& mesh, const Vec3& origin, const Vec3& dir) {
  PickHit best;
  Vec3 d = normalize(dir);
  test(mesh, origin, d, false, best);
  if (mesh.symmetric) test(mesh, {-origin.x, origin.y, origin.z}, {-d.x, d.y, d.z}, true, best);
  return best;
}

}  // namespace bh

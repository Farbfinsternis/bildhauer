// The tree must not change what the brushes do: picking gives the result of a
// test against every triangle, after every frame of every brush, and the tree
// stays complete while the mesh changes under it.

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/brush.h"
#include "core/pick.h"
#include "core/relax.h"
#include "core/symmetry.h"

using namespace bh;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (ok) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

float rayTriangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c) {
  Vec3 e1 = b - a, e2 = c - a, p = cross(d, e2);
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

// The nearest hit of one ray, against every triangle, as pick did before.
float everyTriangle(const Mesh& m, const Vec3& o, const Vec3& d) {
  float best = -1;
  for (int i = 0; i < m.triangleSlots(); ++i) {
    const Triangle& t = m.triangle(i);
    if (!t.active) continue;
    Vec3 perp = cross(t.centroid - o, d);
    if (dot(perp, perp) > t.radius * t.radius * 1.0001f) continue;
    const float hit = rayTriangle(o, d, m.vertex(t.v[0]).pos, m.vertex(t.v[1]).pos, m.vertex(t.v[2]).pos);
    if (hit >= 0 && (best < 0 || hit < best)) best = hit;
  }
  return best;
}

Mesh sheet(bool symmetric) {
  Mesh m;
  const int n = 60;
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) {
      const float x = (symmetric ? 0 : -1) + (symmetric ? 1.0f : 2.0f) * i / n, y = -1 + 2.0f * j / n;
      m.addVertex({x, y, 0.1f * std::sin(5 * x) * std::cos(4 * y)});
    }
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const int a = j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1;
      m.addTriangle(a, b, d);
      m.addTriangle(a, d, c);
    }
  m.symmetric = symmetric;
  for (int v = 0; v < m.vertexSlots(); ++v) m.vertex(v).onSymmetryPlane = symmetric && m.vertex(v).pos.x == 0;
  m.updateGeometry();
  m.storeReference();
  m.clearDirty();
  return m;
}

// Rays over the sheet, straight and slanted, against both searches.
void comparePicks(const Mesh& m, const char* what) {
  int hits = 0;
  for (int j = 0; j < 12; ++j)
    for (int i = 0; i < 12; ++i) {
      const Vec3 o{-1.2f + 0.21f * i, -1.2f + 0.21f * j, 3}, d = normalize({0.15f * (i % 3 - 1), 0.1f * (j % 3 - 1), -1});
      const PickHit h = pick(m, o, d);
      float expected = everyTriangle(m, o, d);
      if (m.symmetric) {
        const float mirrored = everyTriangle(m, {-o.x, o.y, o.z}, {-d.x, d.y, d.z});
        if (mirrored >= 0 && (expected < 0 || mirrored < expected)) expected = mirrored;
      }
      if (h.hit != (expected >= 0) || (h.hit && std::fabs(h.t - expected) > 1e-4f)) {
        std::printf("%s: ray %d %d: tree %d %.6f, every triangle %.6f\n", what, i, j, int(h.hit), h.t, expected);
        check(false, "the tree finds the same hit");
        return;
      }
      hits += h.hit;
    }
  check(hits > 0, "the rays hit the sheet");
}

StrokeInput inputAt(const Mesh& m, float x, float y) {
  StrokeInput in;
  in.rayOrigin = {x, y, 5};
  in.rayDir = {0, 0, -1};
  in.viewDir = {0, 0, -1};
  in.camRight = {1, 0, 0};
  in.camUp = {0, 1, 0};
  in.mouseX = 100 + 200 * x;
  in.mouseY = 100 + 200 * y;
  const PickHit h = pick(m, in.rayOrigin, in.rayDir);
  in.point = h.visiblePoint();
  in.hit = h.hit;
  return in;
}

void testBrush(Tool tool, bool symmetric, bool global) {
  Mesh m = sheet(symmetric);
  BrushSettings s;
  s.tool = tool;
  s.radius = s.detailSize = 0.2f;
  s.strength = tool == Tool::Mask ? -1.0f : 1.0f;
  s.global = global;
  const float x0 = symmetric ? 0.1f : -0.5f, step = symmetric ? 0.02f : 0.04f;
  Stroke stroke(m, s, inputAt(m, x0, -0.3f));
  for (int i = 1; i <= 25; ++i) {
    stroke.moveTo(inputAt(m, x0 + step * i, -0.3f + 0.02f * i));
    beautifyPass(m);
    if (i % 5) continue;
    check(m.tree().validate(m), "every triangle is in one leaf inside its box");
    comparePicks(m, toolName(tool));
  }
  for (int i = 0; i < 40 && !m.dirtyVertices.empty(); ++i) beautifyPass(m);
  check(m.tree().validate(m), "the tree is complete after the stroke");
  comparePicks(m, toolName(tool));
}

// Undo keeps a copy of the mesh, tree included.
void testCopies() {
  Mesh m = sheet(false);
  comparePicks(m, "new mesh");
  const Mesh before = m;
  BrushSettings s;
  s.tool = Tool::Grab;
  s.radius = s.detailSize = 0.3f;
  s.strength = 1;
  {
    Stroke stroke(m, s, inputAt(m, 0, 0));
    for (int i = 1; i <= 10; ++i) stroke.moveTo(inputAt(m, 0.05f * i, 0));
  }
  comparePicks(m, "after Grab");
  m = before;
  check(m.tree().validate(m), "a restored copy has a complete tree");
  comparePicks(m, "after undo");
  m.updateGeometry();
  check(m.tree().validate(m), "updateGeometry rebuilds the tree");
}

// Region and tessellation see the tree: a stroke far from where the tree was
// built still works on what lies under it.
void testGrowth() {
  Mesh m = sheet(false);
  m.tree();
  const int before = m.numTriangles();
  BrushSettings s;
  s.tool = Tool::Draw;
  s.radius = 0.25f;
  s.detailSize = 0.03f;
  s.strength = 1;
  {
    Stroke stroke(m, s, inputAt(m, -0.6f, 0));
    for (int i = 1; i <= 40; ++i) {
      stroke.moveTo(inputAt(m, -0.6f + 0.03f * i, 0.01f * i));
      beautifyPass(m);
    }
  }
  check(m.numTriangles() > before * 2, "the stroke adds detail");
  check(m.tree().validate(m), "leaves split as the mesh grows");
  comparePicks(m, "after growth");
}

}  // namespace

int main() {
  for (int i = 0; i <= int(Tool::Reduce); ++i)
    for (bool symmetric : {false, true}) testBrush(Tool(i), symmetric, false);
  for (Tool tool : {Tool::Grab, Tool::Rotate, Tool::Scale}) testBrush(tool, false, true);
  testCopies();
  testGrowth();
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

// A stroke uploads only what changed, so a change the mesh does not report
// would stay wrong on screen. Here the partial copy must match a full rebuild
// after every frame of every brush, and after replacing the mesh.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "app/gpu_mesh.h"
#include "core/brush.h"
#include "core/pick.h"
#include "core/relax.h"
#include "core/symmetry.h"
#include "test_util.h"

using namespace bh;
using namespace bh::viewer;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (ok) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

// A wavy sheet, with the seam of a symmetric model when `symmetric`.
Mesh sheet(bool symmetric) {
  Mesh m;
  const int n = 48;
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
  m.clearDirty();  // as after loading
  return m;
}

// Whether the partial copy shows the mesh. Free slots hold no vertex anybody
// draws, so only their indices count.
bool matches(const GpuMesh& gpu, const Mesh& m, int* wrongVertex, int* wrongTriangle) {
  *wrongVertex = *wrongTriangle = -1;
  for (int v = 0; v < m.vertexSlots() && *wrongVertex < 0; ++v) {
    const Vertex& x = m.vertex(v);
    if (!x.active) continue;
    const GpuVertex& g = gpu.vertices[size_t(v)];
    if (std::memcmp(&g.position, &x.pos, sizeof(Vec3)) != 0 || std::memcmp(&g.normal, &x.normal, sizeof(Vec3)) != 0 ||
        g.weight != (x.hidden ? -1.0f : x.weight))
      *wrongVertex = v;
  }
  for (int t = 0; t < m.triangleSlots() && *wrongTriangle < 0; ++t) {
    const Triangle& tri = m.triangle(t);
    for (size_t k = 0; k < 3; ++k)
      if (gpu.indices[size_t(t) * 3 + k] != (tri.active ? uint32_t(tri.v[k]) : 0)) *wrongTriangle = t;
  }
  return *wrongVertex < 0 && *wrongTriangle < 0;
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
  GpuMesh gpu;
  gpu.update(m, false);
  BrushSettings s;
  s.tool = tool;
  s.radius = s.detailSize = 0.25f;
  s.strength = tool == Tool::Mask ? -1.0f : 1.0f;
  s.global = global;
  const float x0 = symmetric ? 0.1f : -0.5f, step = symmetric ? 0.02f : 0.04f;
  Stroke stroke(m, s, inputAt(m, x0, -0.3f));
  int frames = 0, partial = 0;
  for (int i = 1; i <= 30; ++i) {
    stroke.moveTo(inputAt(m, x0 + step * i, -0.3f + 0.015f * i));
    beautifyPass(m);
    gpu.update(m, false);
    ++frames;
    if (!gpu.vertexRanges.empty() && gpu.vertexRanges[0].count < gpu.vertices.size()) ++partial;
    int v, t;
    if (!matches(gpu, m, &v, &t)) {
      std::printf("%s%s%s: frame %d, vertex %d, triangle %d differ\n", toolName(tool), symmetric ? " symmetric" : "",
                  global ? " global" : "", i, v, t);
      check(false, "partial copy matches the mesh");
      return;
    }
  }
  // The frames after a stroke relax the mesh further.
  for (int i = 0; i < 40 && !m.dirtyVertices.empty(); ++i) {
    beautifyPass(m);
    gpu.update(m, false);
  }
  int v, t;
  check(matches(gpu, m, &v, &t), "partial copy matches after the stroke settled");
  check(tool == Tool::Reduce || global || partial > 0, "a local brush uploads only a part");
}

void testReplacedMesh() {
  Mesh m = sheet(false);
  GpuMesh gpu;
  gpu.update(m, false);
  check(gpu.vertexRanges.size() == 1 && gpu.vertexRanges[0].count == gpu.vertices.size(), "the first update is complete");
  gpu.update(m, false);
  check(gpu.vertexRanges.empty() && gpu.indexRanges.empty() && !gpu.topologyChanged, "nothing changed, nothing to send");

  const Mesh before = m;  // undo keeps such a copy
  BrushSettings s;
  s.tool = Tool::Draw;
  s.radius = s.detailSize = 0.25f;
  s.strength = 1;
  {
    Stroke stroke(m, s, inputAt(m, -0.2f, 0));
    stroke.moveTo(inputAt(m, 0.2f, 0));
  }
  gpu.update(m, false);
  check(gpu.topologyChanged, "new triangles report a topology change");
  m = before;
  gpu.update(m, false);
  int v, t;
  check(gpu.vertexRanges.size() == 1 && gpu.vertexRanges[0].count == gpu.vertices.size(), "undo renews everything");
  check(matches(gpu, m, &v, &t), "the copy shows the restored mesh");

  m.updateGeometry();
  gpu.update(m, false);
  check(gpu.vertexRanges.size() == 1 && gpu.vertexRanges[0].count == gpu.vertices.size(), "updateGeometry renews everything");

  m.vertex(3).hidden = true;
  gpu.update(m, true);
  check(gpu.vertices[3].weight < 0, "a hidden vertex has a negative weight");
}

}  // namespace

int main() {
  for (int i = 0; i <= int(Tool::Reduce); ++i)
    for (bool symmetric : {false, true}) testBrush(Tool(i), symmetric, false);
  for (Tool tool : {Tool::Grab, Tool::Rotate, Tool::Scale}) testBrush(tool, false, true);
  testReplacedMesh();
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

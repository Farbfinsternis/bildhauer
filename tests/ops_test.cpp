// Exercises the mesh operations on real meshes and checks after each one that
// the mesh is valid and its topology is unchanged:
//   - Mesh::validate() passes
//   - the full surface (mirrored, if symmetric) has the same Euler
//     characteristic and boundary as before, and no edge with > 2 triangles
//   - plane vertices sit exactly on x = 0 with normal.x = 0
// Operations: edge split, tessellation under a brush, edge collapse, area
// reduction, subdivision (smooth and flat), beautify.
//
// Usage: ops_test [file.sc1...]

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>

#include "core/mesh.h"
#include "core/reduce.h"
#include "core/relax.h"
#include "core/subdivide.h"
#include "core/tessellate.h"
#include "io/sc1.h"

using namespace bh;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("    %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

struct Topology {
  int vertices = 0, edges = 0, faces = 0, open = 0, overfull = 0;
  int euler() const { return vertices - edges + faces; }
};

// Topology of the full surface, built from indices only. With symmetry, plane
// vertices are shared and every triangle is mirrored.
Topology fullTopology(const Mesh& m, bool symmetric) {
  std::map<int, int> same, mirror;
  int next = 0;
  for (int v = 0; v < m.vertexSlots(); ++v) {
    if (!m.vertex(v).active) continue;
    same[v] = next++;
    mirror[v] = symmetric && !m.vertex(v).onSymmetryPlane ? next++ : same[v];
  }
  std::map<std::pair<int, int>, int> edgeUse;
  Topology topo;
  topo.vertices = next;
  auto addFace = [&](int a, int b, int c) {
    ++topo.faces;
    int f[3] = {a, b, c};
    for (int k = 0; k < 3; ++k) {
      int p = f[k], q = f[(k + 1) % 3];
      ++edgeUse[{std::min(p, q), std::max(p, q)}];
    }
  };
  for (int t = 0; t < m.triangleSlots(); ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active) continue;
    addFace(same[tri.v[0]], same[tri.v[1]], same[tri.v[2]]);
    if (symmetric) addFace(mirror[tri.v[2]], mirror[tri.v[1]], mirror[tri.v[0]]);
  }
  topo.edges = int(edgeUse.size());
  for (auto& [e, n] : edgeUse) {
    if (n == 1) ++topo.open;
    if (n > 2) ++topo.overfull;
  }
  return topo;
}

bool planeIsExact(const Mesh& m) {
  for (int v = 0; v < m.vertexSlots(); ++v) {
    const Vertex& vx = m.vertex(v);
    if (vx.active && vx.onSymmetryPlane && (vx.pos.x != 0 || vx.normal.x != 0)) return false;
  }
  return true;
}

bool finite(const Mesh& m) {
  for (int v = 0; v < m.vertexSlots(); ++v) {
    const Vertex& vx = m.vertex(v);
    if (vx.active && !(std::isfinite(vx.pos.x) && std::isfinite(vx.pos.y) && std::isfinite(vx.pos.z))) return false;
  }
  return true;
}

void checkMesh(const Mesh& m, bool symmetric, const Topology& before, const char* op) {
  std::string err;
  check(m.validate(&err), std::string(op) + ": invariants" + (err.empty() ? "" : " (" + err + ")"));
  Topology after = fullTopology(m, symmetric);
  check(after.overfull == 0 && after.euler() == before.euler() && (after.open == 0) == (before.open == 0),
        std::string(op) + ": topology kept (euler " + std::to_string(after.euler()) + ", " +
            std::to_string(after.open) + " open edges)");
  check(finite(m), std::string(op) + ": finite positions");
  if (symmetric) check(planeIsExact(m), std::string(op) + ": symmetry plane exact");
}

Vec3 bboxDiagonal(const Mesh& m, Vec3* center) {
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (int v = 0; v < m.vertexSlots(); ++v) {
    if (!m.vertex(v).active) continue;
    const Vec3& p = m.vertex(v).pos;
    lo = {std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
    hi = {std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
  }
  if (center) *center = (lo + hi) * 0.5f;
  return hi - lo;
}

void testFile(const std::string& path) {
  std::printf("%s\n", path.c_str());
  std::vector<uint8_t> data;
  Sc1File file;
  Mesh base;
  std::string err;
  if (!readBinaryFile(path, data) || !readSc1(data, file, &err) || !sc1ToMesh(file, base, &err)) {
    check(false, "load: " + err);
    return;
  }
  const bool sym = file.symmetry;
  const Topology before = fullTopology(base, sym);
  std::printf("  %d verts, %d tris, full euler %d, %d open edges\n", base.numVertices(), base.numTriangles(),
              before.euler(), before.open);

  {  // edge split
    Mesh m = base;
    int splits = 0, rejected = 0, wrongCounts = 0;
    for (int e = 0; e < m.edgeSlots() && splits < 200; e += 7) {
      if (!m.edge(e).active) continue;
      int faces = m.edge(e).numTris();
      int v0 = m.numVertices(), t0 = m.numTriangles(), e0 = m.numEdges();
      if (splitEdge(m, e) < 0) { ++rejected; continue; }
      ++splits;
      if (m.numVertices() != v0 + 1 || m.numTriangles() != t0 + faces || m.numEdges() != e0 + 1 + faces) ++wrongCounts;
    }
    std::printf("  split: %d splits, %d rejected\n", splits, rejected);
    check(splits > 0 && wrongCounts == 0, "split: element counts (+1 vertex, +n triangles, +1+n edges)");
    checkMesh(m, sym, before, "split");
  }

  Vec3 center;
  Vec3 diag = bboxDiagonal(base, &center);
  float size = length(diag);

  {  // tessellation under a brush on the surface
    Mesh m = base;
    int target = -1;
    float best = 1e30f;
    for (int v = 0; v < m.vertexSlots(); ++v) {  // vertex closest to the bbox center line, front side
      if (!m.vertex(v).active || m.vertex(v).onSymmetryPlane) continue;
      Vec3 d = m.vertex(v).pos - center;
      float score = std::fabs(d.y) + std::fabs(d.x - diag.x * 0.25f);
      if (score < best) { best = score; target = v; }
    }
    BrushDetail brush;
    brush.center = m.vertex(target).pos;
    brush.radius = size * 0.1f;
    brush.detailSize = size * 0.01f;
    int t0 = m.numTriangles();
    int splits = tessellateForBrush(m, brush);
    std::printf("  tessellate: %d splits, %d -> %d triangles\n", splits, t0, m.numTriangles());
    check(splits > 0, "tessellate: refines under the brush");
    checkMesh(m, sym, before, "tessellate");

    // Coarser detail: the loop must end on its own with nothing coarse left
    // whose longest edge is splittable (without the beautify pass, which may
    // make triangles coarse again).
    Mesh c = base;
    brush.detailSize = size * 0.04f;
    brush.beautify = false;
    int coarseSplits = tessellateForBrush(c, brush);
    float threshold = brush.detailSize / 2 / brush.detailFactor;
    int left = 0;
    for (int t = 0; t < c.triangleSlots(); ++t) {
      const Triangle& tri = c.triangle(t);
      if (!tri.active || tri.size <= threshold || length(brush.center - tri.centroid) > tri.radius + brush.radius)
        continue;
      int longest = tri.e[0];
      for (int e : tri.e)
        if (c.edge(e).length > c.edge(longest).length) longest = e;
      left += canSplitEdge(c, longest);
    }
    std::printf("  tessellate coarse: %d splits, %d splittable coarse triangles left\n", coarseSplits, left);
    if (coarseSplits < brush.maxSplits) check(left == 0, "tessellate: nothing coarse left in range");
  }

  {  // edge collapse
    Mesh m = base;
    int collapses = 0, rejected = 0, wrongCounts = 0;
    for (int e = 0; e < m.edgeSlots() && collapses < 200; e += 5) {
      if (!m.edge(e).active) continue;
      bool interior = m.edge(e).numTris() == 2;
      int v0 = m.numVertices(), t0 = m.numTriangles();
      if (collapseEdge(m, e) < 0) { ++rejected; continue; }
      ++collapses;
      if (m.numVertices() != v0 - 1 || m.numTriangles() != t0 - (interior ? 2 : 1)) ++wrongCounts;
    }
    std::printf("  collapse: %d collapses, %d rejected\n", collapses, rejected);
    check(collapses > 0 && wrongCounts == 0, "collapse: element counts (-1 vertex, -n triangles)");
    checkMesh(m, sym, before, "collapse");
  }

  {  // area reduction
    Mesh m = base;
    int t0 = m.numTriangles();
    int collapses = reduceMesh(m);
    std::printf("  reduce: %d collapses, %d -> %d triangles\n", collapses, t0, m.numTriangles());
    check(collapses > 0 && m.numTriangles() < t0, "reduce: fewer triangles");
    checkMesh(m, sym, before, "reduce");
  }

  for (bool smooth : {true, false}) {  // subdivision
    Mesh m = base;
    int v0 = m.numVertices(), e0 = m.numEdges(), t0 = m.numTriangles();
    subdivide(m, {smooth, sym});
    const char* op = smooth ? "subdivide smooth" : "subdivide flat";
    std::printf("  %s: %d -> %d vertices, %d -> %d triangles\n", op, v0, m.numVertices(), t0, m.numTriangles());
    check(m.numVertices() == v0 + e0 && m.numTriangles() == 4 * t0, std::string(op) + ": V + E vertices, 4F triangles");
    checkMesh(m, sym, before, op);
    if (!smooth) {
      // Old vertices come first and keep their positions; check one midpoint.
      bool kept = true;
      int idx = 0;
      for (int v = 0; v < base.vertexSlots(); ++v)
        if (base.vertex(v).active) kept &= m.vertex(idx++).pos == base.vertex(v).pos;
      check(kept, std::string(op) + ": original vertices unchanged");
    }
  }

  {  // beautify on everything
    Mesh m = base;
    for (int v = 0; v < m.vertexSlots(); ++v)
      if (m.vertex(v).active) { m.vertex(v).dirty = true; m.vertex(v).relaxCountdown = 3; }
    int moved = 0;
    for (int i = 0; i < 4; ++i) moved += beautifyPass(m);
    std::printf("  beautify: %d moves\n", moved);
    checkMesh(m, sym, before, "beautify");
  }
}

// A cube made of 6 quads, each stored as T1 = (v0, v1, v2), T2 = (v2, q, v0)
// with polygon-list markers 1, 2. No sample file has quads, so this covers
// quad subdivision.
void testQuadCube() {
  std::printf("quad cube\n");
  Mesh m;
  for (int i = 0; i < 8; ++i) m.addVertex({float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1)});
  const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (const auto& q : quads) {
    int t1 = m.addTriangle(q[0], q[1], q[2]);
    m.addTriangle(q[2], q[3], q[0]);
    m.edge(m.triangle(t1).e[2]).quadDiagonal = true;
    m.quadMarkers.push_back(1);
    m.quadMarkers.push_back(2);
  }
  m.updateGeometry();
  const Topology before = fullTopology(m, false);
  check(before.open == 0 && before.euler() == 2, "cube: closed, euler 2");

  for (bool smooth : {false, true}) {
    Mesh s = m;
    subdivide(s, {smooth, false});
    const char* op = smooth ? "quad subdivide smooth" : "quad subdivide flat";
    check(s.numVertices() == 8 + 18 && s.numTriangles() == 48, std::string(op) + ": 26 vertices, 24 quads");
    checkMesh(s, false, before, op);
    bool paired = int(s.quadMarkers.size()) == s.triangleSlots();
    int diagonals = 0;
    for (int t = 0; paired && t + 1 < s.triangleSlots(); t += 2) {
      paired &= s.quadMarkers[t] == 1 && s.quadMarkers[t + 1] == 2;
      const Edge& d = s.edge(s.triangle(t).e[2]);
      paired &= d.quadDiagonal && (d.tri[0] == t + 1 || d.tri[1] == t + 1);
      diagonals += d.quadDiagonal;
    }
    check(paired && diagonals == 24, std::string(op) + ": all faces are marked quads with their diagonal");
  }
}

}  // namespace

int main(int argc, char** argv) {
  testQuadCube();
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

// Exercises the mesh operations on real meshes and checks after each one that
// the mesh is valid and its topology is unchanged:
//   - Mesh::validate() passes
//   - the full surface (mirrored, if symmetric) has the same Euler
//     characteristic and boundary as before, and no edge with > 2 triangles
//   - plane vertices sit exactly on x = 0 with normal.x = 0
// Operations: edge split, tessellation under a brush, edge collapse, area
// reduction, subdivision (smooth and flat), beautify, switching symmetry off
// and on.
//
// Usage: ops_test [file.sc1...]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/mesh.h"
#include "core/reduce.h"
#include "core/relax.h"
#include "core/subdivide.h"
#include "core/symmetry.h"
#include "core/tessellate.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

namespace {

// Quad pairs are consecutive triangles marked 1, 2 that share T1's edge e2,
// which carries the diagonal flag; no other edge has it.
bool quadsCoherent(const Mesh& m) {
  int flagged = 0, pairs = 0;
  for (int e = 0; e < m.edgeSlots(); ++e) flagged += m.edge(e).active && m.edge(e).quadDiagonal;
  for (int t = 0; t < int(m.quadMarkers.size()); ++t) {
    const uint8_t q = m.quadMarkers[t];
    if (q == 0) continue;
    if (!m.triangle(t).active) return false;
    if (q == 2) {
      if (t == 0 || m.quadMarkers[t - 1] != 1) return false;
      continue;
    }
    if (t + 1 >= int(m.quadMarkers.size()) || m.quadMarkers[t + 1] != 2 || !m.triangle(t + 1).active) return false;
    const Edge& d = m.edge(m.triangle(t).e[2]);
    if (!d.quadDiagonal || (d.tri[0] != t + 1 && d.tri[1] != t + 1)) return false;
    ++pairs;
  }
  return flagged == pairs;
}

bool maskCleared(const Mesh& m) {
  for (int v = 0; v < m.vertexSlots(); ++v)
    if (m.vertex(v).active && m.vertex(v).weight != 1) return false;
  return true;
}

// Every vertex off the plane has a mirror vertex at (-x, y, z).
bool mirrorSymmetric(const Mesh& m) {
  std::vector<std::array<float, 3>> pts;
  for (int v = 0; v < m.vertexSlots(); ++v)
    if (m.vertex(v).active) pts.push_back({m.vertex(v).pos.x, m.vertex(v).pos.y, m.vertex(v).pos.z});
  std::sort(pts.begin(), pts.end());
  for (const auto& q : pts)
    if (!std::binary_search(pts.begin(), pts.end(), std::array<float, 3>{-q[0], q[1], q[2]})) return false;
  return true;
}

int trianglesLeftOfPlane(const Mesh& m) {
  int n = 0;
  for (int t = 0; t < m.triangleSlots(); ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active) continue;
    float x = 0;
    for (int v : tri.v) x += m.vertex(v).pos.x;
    n += x / 3 < 0;
  }
  return n;
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

  {  // symmetry off and on again
    Mesh m = base;
    for (int v = 0; v < m.vertexSlots(); ++v)
      if (m.vertex(v).active) m.vertex(v).weight = 0.5f;
    if (sym) {
      int planeMax = 0;
      Mesh limited = m;
      const int collapses = limitPlaneValence(limited);
      for (int v = 0; v < limited.vertexSlots(); ++v)
        if (limited.vertex(v).active && limited.vertex(v).onSymmetryPlane)
          planeMax = std::max(planeMax, limited.vertex(v).numEdges);
      std::printf("  plane valence: %d collapses, max plane valence %d\n", collapses, planeMax);
      check(planeMax <= 10, "plane valence: at most 10 edges per plane vertex");
      checkMesh(limited, true, before, "plane valence");

      check(symmetryOff(m), "symmetry off: mirrored");
      checkMesh(m, false, before, "symmetry off");
      bool flagsCleared = !m.symmetric;
      for (int v = 0; v < m.vertexSlots(); ++v) flagsCleared &= !m.vertex(v).onSymmetryPlane;
      check(flagsCleared && maskCleared(m), "symmetry off: no plane flags, mask cleared");
      check(mirrorSymmetric(m), "symmetry off: every vertex has its mirror image");
      const int full = m.numTriangles();
      const int converted = symmetryOn(m);
      std::printf("  symmetry: %d -> %d -> %d triangles, %d quads converted\n", base.numTriangles(), full,
                  m.numTriangles(), converted);
      checkMesh(m, true, before, "symmetry on again");
      {  // detail sculpted on the -x side of the full mesh is gone after the cut
        Mesh full2 = base;
        symmetryOff(full2);
        Mesh plain = full2;
        Vec3 c;
        const float extent = length(bboxDiagonal(full2, &c));
        for (int v = 0; v < full2.vertexSlots(); ++v) {
          Vertex& vx = full2.vertex(v);
          if (vx.active && vx.pos.x < -0.05f * extent) vx.pos += vx.normal * (0.01f * extent);
        }
        symmetryOn(full2);
        symmetryOn(plain);
        bool same = full2.numTriangles() == plain.numTriangles() && full2.vertexSlots() == plain.vertexSlots();
        for (int v = 0; same && v < full2.vertexSlots(); ++v)
          same = full2.vertex(v).active == plain.vertex(v).active &&
                 (!full2.vertex(v).active || full2.vertex(v).pos == plain.vertex(v).pos);
        check(same, "symmetry on: asymmetric detail on the -x side is discarded");
      }
      // A stored half may hold a few triangles left of the plane; the cut drops them.
      check(m.symmetric && trianglesLeftOfPlane(m) == 0 && full == 2 * base.numTriangles() &&
                m.numTriangles() == base.numTriangles() - trianglesLeftOfPlane(base),
            "symmetry on again: the +x half, less the stored triangles left of the plane (" +
                std::to_string(trianglesLeftOfPlane(base)) + ")");
    } else {
      const int converted = symmetryOn(m);
      std::string why;
      check(m.validate(&why), "symmetry on: invariants" + (why.empty() ? "" : " (" + why + ")"));
      check(m.symmetric && planeIsExact(m) && trianglesLeftOfPlane(m) == 0 && maskCleared(m),
            "symmetry on: +x half, plane exact, mask cleared");
      std::printf("  symmetry on: %d -> %d triangles, %d quads converted\n", base.numTriangles(), m.numTriangles(),
                  converted);
      check(symmetryOff(m) && m.validate(&why), "symmetry off again: valid");
    }
  }

  {  // beautify on everything
    Mesh m = base;
    for (int v = 0; v < m.vertexSlots(); ++v)
      if (m.vertex(v).active) { m.setDirty(v); m.vertex(v).relaxCountdown = 3; }
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

  // Centered on the plane: the four faces along x are cut in two, the faces
  // at x = +-0.5 keep or lose their quad as a whole.
  Mesh c = m;
  for (int v = 0; v < c.vertexSlots(); ++v) c.vertex(v).pos.x -= 0.5f;
  c.updateGeometry();
  const int converted = symmetryOn(c);
  std::string err;
  check(c.validate(&err) && quadsCoherent(c) && planeIsExact(c) && trianglesLeftOfPlane(c) == 0,
        "cube symmetry on: valid, quads coherent, plane exact (" + std::to_string(converted) + " quads converted)");
  int quadsLeft = 0;
  for (uint8_t q : c.quadMarkers) quadsLeft += q == 1;
  check(converted > 0 && quadsLeft >= 1, "cube symmetry on: cut quads became triangles, the +x face stays a quad");
  const Topology half = fullTopology(c, true);
  check(symmetryOff(c), "cube symmetry off: mirrored");
  checkMesh(c, false, half, "cube symmetry off");
  int quadsAfter = 0;
  for (uint8_t q : c.quadMarkers) quadsAfter += q == 1;
  check(quadsCoherent(c) && quadsAfter == 2 * quadsLeft, "cube symmetry off: quads mirrored with their diagonals");
}

}  // namespace

int main(int argc, char** argv) {
  testQuadCube();
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

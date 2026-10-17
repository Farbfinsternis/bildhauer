#pragma once

// Shared checks for the tests: validity, topology of the full (mirrored)
// surface, exact symmetry plane.

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>

#include "core/mesh.h"

namespace bhtest {

using namespace bh;

inline int failures = 0;

inline void check(bool ok, const std::string& what) {
  std::printf("    %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

struct Topology {
  int vertices = 0, edges = 0, faces = 0, open = 0, overfull = 0;
  int euler() const { return vertices - edges + faces; }
};

// Topology of the full surface, built from indices only. With symmetry, plane
// vertices are shared and every triangle is mirrored.
inline Topology fullTopology(const Mesh& m, bool symmetric) {
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

inline bool planeIsExact(const Mesh& m) {
  for (int v = 0; v < m.vertexSlots(); ++v) {
    const Vertex& vx = m.vertex(v);
    if (vx.active && vx.onSymmetryPlane && (vx.pos.x != 0 || vx.normal.x != 0)) return false;
  }
  return true;
}

inline bool finite(const Mesh& m) {
  for (int v = 0; v < m.vertexSlots(); ++v) {
    const Vertex& vx = m.vertex(v);
    if (vx.active && !(std::isfinite(vx.pos.x) && std::isfinite(vx.pos.y) && std::isfinite(vx.pos.z))) return false;
  }
  return true;
}

inline void checkMesh(const Mesh& m, bool symmetric, const Topology& before, const char* op) {
  std::string err;
  check(m.validate(&err), std::string(op) + ": invariants" + (err.empty() ? "" : " (" + err + ")"));
  Topology after = fullTopology(m, symmetric);
  check(after.overfull == 0 && after.euler() == before.euler() && (after.open == 0) == (before.open == 0),
        std::string(op) + ": topology kept (euler " + std::to_string(after.euler()) + ", " +
            std::to_string(after.open) + " open edges)");
  check(finite(m), std::string(op) + ": finite positions");
  if (symmetric) check(planeIsExact(m), std::string(op) + ": symmetry plane exact");
}

inline Vec3 bboxDiagonal(const Mesh& m, Vec3* center) {
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

}  // namespace bhtest

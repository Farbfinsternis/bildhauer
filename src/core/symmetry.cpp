#include "core/symmetry.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "core/reduce.h"

namespace bh {

namespace {

// Common tail of both switches (symmetry.md 2.3): mask cleared, geometry and
// normals updated (plane snapped when symmetric), reference state taken.
void finishSwitch(Mesh& m, bool symmetric) {
  m.symmetric = symmetric;
  for (int v = 0; v < m.vertexSlots(); ++v)
    if (m.vertex(v).active) m.vertex(v).weight = 1;
  m.updateGeometry();
  m.storeReference();
  m.clearDirty();
}

// Turns the quad containing triangle t (if any) into two triangles.
void dropQuad(Mesh& m, int t) {
  if (t >= int(m.quadMarkers.size()) || m.quadMarkers[t] == 0) return;
  const int first = m.quadMarkers[t] == 1 ? t : t - 1;
  for (int k : {first, first + 1}) {
    if (k < 0 || k >= int(m.quadMarkers.size())) continue;
    if (m.triangle(k).active) m.edge(m.triangle(k).e[2]).quadDiagonal = false;
    m.quadMarkers[k] = 0;
  }
}

}  // namespace

bool mirrorHalf(const Mesh& half, Mesh& full) {
  full = Mesh{};
  std::vector<int> same(half.vertexSlots(), -1), mirror(half.vertexSlots(), -1);
  for (int i = 0; i < half.vertexSlots(); ++i) {
    const Vertex& v = half.vertex(i);
    if (!v.active) continue;
    Vec3 p = v.pos;
    if (v.onSymmetryPlane) p.x = 0;
    else if (p.x == 0) p.x = 1e-5f;
    same[i] = full.addVertex(p);
    full.vertex(same[i]).weight = v.weight;
    if (v.onSymmetryPlane) {
      mirror[i] = same[i];
    } else {
      mirror[i] = full.addVertex({-p.x, p.y, p.z});
      full.vertex(mirror[i]).weight = v.weight;
    }
  }
  for (int i = 0; i < half.triangleSlots(); ++i) {
    const Triangle& t = half.triangle(i);
    if (!t.active) continue;
    if (full.addTriangle(same[t.v[0]], same[t.v[1]], same[t.v[2]]) < 0) return false;
    if (full.addTriangle(mirror[t.v[2]], mirror[t.v[1]], mirror[t.v[0]]) < 0) return false;
  }
  full.updateGeometry();
  return true;
}

void snapPlaneVertices(Mesh& mesh) {
  for (int i : mesh.dirtyVertices) {
    Vertex& v = mesh.vertex(i);
    if (!v.active || !v.dirty || !v.onSymmetryPlane) continue;
    v.pos.x = v.refPos.x = 0;
    v.normal.x = v.refNormal.x = 0;
    v.normal = normalize(v.normal);
    v.refNormal = normalize(v.refNormal);
  }
}

int limitPlaneValence(Mesh& m, int maxEdges) {
  int collapses = 0;
  for (int p = 0; p < m.vertexSlots(); ++p) {
    if (!m.vertex(p).active || !m.vertex(p).onSymmetryPlane || m.vertex(p).numEdges <= maxEdges) continue;
    for (int i = 0; i < m.vertex(p).numTris; ++i) dropQuad(m, m.vertex(p).tris[i]);
    std::set<std::pair<int, int>> tried;  // opposite edges by their vertices
    while (m.vertex(p).active && m.vertex(p).numEdges > maxEdges) {
      int best = -1;
      float bestLen = 0;
      const Vertex& v = m.vertex(p);
      for (int i = 0; i < v.numTris; ++i) {
        const Triangle& t = m.triangle(v.tris[i]);
        for (int k = 0; k < 3; ++k) {
          const Edge& e = m.edge(t.e[k]);
          if (e.v[0] == p || e.v[1] == p) continue;
          if (tried.count(std::minmax(e.v[0], e.v[1]))) continue;
          const float len = length(m.vertex(e.v[0]).pos - m.vertex(e.v[1]).pos);
          if (best < 0 || len < bestLen) {
            best = t.e[k];
            bestLen = len;
          }
        }
      }
      if (best < 0) break;
      // The collapse rebuilds the triangles around both ends: no quads there.
      for (int end : m.edge(best).v)
        for (int i = 0; i < m.vertex(end).numTris; ++i) dropQuad(m, m.vertex(end).tris[i]);
      tried.insert(std::minmax(m.edge(best).v[0], m.edge(best).v[1]));
      if (collapseEdge(m, best) >= 0) ++collapses;
    }
  }
  return collapses;
}

bool symmetryOff(Mesh& half) {
  limitPlaneValence(half);
  Mesh full;
  std::vector<int> same(half.vertexSlots(), -1), mirror(half.vertexSlots(), -1);
  for (int i = 0; i < half.vertexSlots(); ++i) {
    const Vertex& v = half.vertex(i);
    if (!v.active) continue;
    Vec3 p = v.pos;
    if (v.onSymmetryPlane) p.x = 0;
    else if (p.x == 0) p.x = 1e-5f;  // mirrored, not shared
    same[i] = full.addVertex(p);
    full.vertex(same[i]).hidden = v.hidden;
    mirror[i] = same[i];
    if (!v.onSymmetryPlane) {
      mirror[i] = full.addVertex({-p.x, p.y, p.z});
      full.vertex(mirror[i]).hidden = v.hidden;
    }
  }
  std::vector<int> order;  // active triangles in slot order
  for (int t = 0; t < half.triangleSlots(); ++t)
    if (half.triangle(t).active) order.push_back(t);
  for (int pass = 0; pass < 2; ++pass)
    for (int t : order) {
      const Triangle& tri = half.triangle(t);
      const int n = pass == 0 ? full.addTriangle(same[tri.v[0]], same[tri.v[1]], same[tri.v[2]])
                              : full.addTriangle(mirror[tri.v[2]], mirror[tri.v[1]], mirror[tri.v[0]]);
      if (n < 0) return false;
      full.triangle(n).fileFlag = tri.fileFlag;
    }
  // Original k is at slot k, its mirror at slot K + k; pairs stay consecutive.
  if (!half.quadMarkers.empty()) {
    const int count = int(order.size());
    full.quadMarkers.assign(full.triangleSlots(), 0);
    for (int pass = 0; pass < 2; ++pass)
      for (int k = 0; k < count; ++k) {
        const int t = order[k];
        full.quadMarkers[pass * count + k] = t < int(half.quadMarkers.size()) ? half.quadMarkers[t] : 0;
      }
    for (int k = 0; k + 1 < int(full.quadMarkers.size()); ++k)
      if (full.quadMarkers[k] == 1 && full.quadMarkers[k + 1] == 2)
        full.edge(full.triangle(k).e[2]).quadDiagonal = true;
  }
  half = std::move(full);
  finishSwitch(half, false);
  return true;
}

int symmetryOn(Mesh& m) {
  const int nt = m.triangleSlots();
  std::vector<uint8_t> partnerLost(nt, 0);
  auto removeQuadAware = [&](int t) {
    if (t < int(m.quadMarkers.size()) && m.quadMarkers[t] != 0) {
      const int partner = m.quadMarkers[t] == 1 ? t + 1 : t - 1;
      if (partner >= 0 && partner < nt) partnerLost[partner] = 1;
      m.quadMarkers[t] = 0;
    }
    m.removeTriangle(t, true);
  };

  for (int v = 0; v < m.vertexSlots(); ++v) m.vertex(v).onSymmetryPlane = false;
  std::vector<uint8_t> mark(m.vertexSlots(), 0);
  std::vector<int> cut;
  for (int t = 0; t < nt; ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active) continue;
    float x = 0;
    for (int v : tri.v) x += m.vertex(v).pos.x;
    if (x / 3 >= 0) continue;
    for (int v : tri.v) mark[v] = 1;
    cut.push_back(t);
  }
  for (int t : cut) removeQuadAware(t);
  for (int v = 0; v < m.vertexSlots(); ++v) {
    Vertex& vx = m.vertex(v);
    if (!vx.active || !mark[v]) continue;
    vx.pos.x = vx.refPos.x = 0;
    vx.onSymmetryPlane = true;
  }
  for (int t = 0; t < nt; ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active) continue;
    if (m.vertex(tri.v[0]).pos.x <= 0 && m.vertex(tri.v[1]).pos.x <= 0 && m.vertex(tri.v[2]).pos.x <= 0)
      removeQuadAware(t);
  }

  int converted = 0;
  for (int t = 0; t < nt; ++t) {
    if (!partnerLost[t] || !m.triangle(t).active || t >= int(m.quadMarkers.size()) || m.quadMarkers[t] == 0) continue;
    m.edge(m.triangle(t).e[2]).quadDiagonal = false;
    m.quadMarkers[t] = 0;
    ++converted;
  }
  finishSwitch(m, true);
  return converted;
}

}  // namespace bh

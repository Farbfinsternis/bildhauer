#include "core/tessellate.h"

#include <algorithm>
#include <queue>
#include <vector>

#include "core/curve.h"
#include "core/relax.h"
#include "core/symmetry.h"

namespace bh {

namespace {

int oppositeVertex(const Mesh& mesh, int t, int e) {
  const Triangle& tri = mesh.triangle(t);
  for (int k = 0; k < 3; ++k)
    if (tri.e[k] == e) return tri.v[(k + 2) % 3];
  return -1;
}

}  // namespace

bool canSplitEdge(const Mesh& mesh, int e) {
  const Edge& edge = mesh.edge(e);
  if (!edge.active) return false;
  for (int t : edge.tri)
    if (t >= 0 && mesh.vertex(oppositeVertex(mesh, t, e)).numEdges > 18) return false;
  return true;
}

int splitEdge(Mesh& mesh, int e) {
  if (!canSplitEdge(mesh, e)) return -1;
  if (!mesh.quadMarkers.empty()) mesh.clearQuads();

  const Edge edge = mesh.edge(e);
  const Vertex a = mesh.vertex(edge.v[0]);
  const Vertex b = mesh.vertex(edge.v[1]);

  int m = mesh.addVertex(curvedMidpoint(a.pos, a.normal, b.pos, b.normal));
  Vertex& mv = mesh.vertex(m);
  mv.refPos = curvedMidpoint(a.refPos, a.refNormal, b.refPos, b.refNormal);
  mv.normal = curvedMidpointNormal(a.pos, a.normal, b.pos, b.normal);
  mv.refNormal = curvedMidpointNormal(a.refPos, a.refNormal, b.refPos, b.refNormal);
  mv.weight = (a.weight + b.weight) / 2;
  mv.hidden = a.hidden || b.hidden;
  mv.onSymmetryPlane = a.onSymmetryPlane && b.onSymmetryPlane;
  mv.relaxCountdown = 5;

  // Per adjacent triangle (x0, x1, x2) with the edge as (xk, xk+1) and the
  // opposite vertex o: (xk, M, o) and (M, xk+1, o).
  struct Half { int from, to, opposite; };
  Half halves[2];
  int n = 0;
  for (int t : edge.tri) {
    if (t < 0) continue;
    const Triangle& tri = mesh.triangle(t);
    for (int k = 0; k < 3; ++k)
      if (tri.e[k] == e) halves[n++] = {tri.v[k], tri.v[(k + 1) % 3], tri.v[(k + 2) % 3]};
  }
  for (int t : edge.tri)
    if (t >= 0) mesh.removeTriangle(t, false);
  for (int i = 0; i < n; ++i) {
    const Half& h = halves[i];
    if (mesh.addTriangle(h.from, m, h.opposite) < 0 || mesh.addTriangle(m, h.to, h.opposite) < 0) return -1;
  }
  for (int i = 0; i < mv.numTris; ++i) mesh.updateTriangleGeometry(mv.tris[i]);
  markDirty(mesh, m);
  return m;
}

int tessellateForBrush(Mesh& mesh, const BrushDetail& brush, const std::vector<int>* regionVertices,
                       std::vector<int>* newVertices) {
  if (brush.detailFactor <= 0.15f) return 0;
  const float threshold = brush.detailSize / (2 * brush.scale) / brush.detailFactor;

  auto coarse = [&](int t) {
    const Triangle& tri = mesh.triangle(t);
    if (!tri.active || tri.size <= threshold) return false;
    bool editable = false;
    for (int v : tri.v) {
      if (mesh.vertex(v).hidden) return false;
      editable |= mesh.vertex(v).weight != 0;
    }
    return editable && length(brush.center - tri.centroid) <= tri.radius + brush.radius;
  };
  auto longestEdge = [&](int t) {
    const Triangle& tri = mesh.triangle(t);
    int longest = tri.e[0];
    for (int e : tri.e)
      if (mesh.edge(e).length > mesh.edge(longest).length) longest = e;
    return longest;
  };

  // Max-heap of (edge length, triangle, its longest edge). Entries go stale
  // when their triangle is removed by a split; they are checked on pop.
  struct Entry {
    float length;
    int tri, edge;
    bool operator<(const Entry& o) const { return length < o.length; }
  };
  std::priority_queue<Entry> heap;
  auto push = [&](int t) {
    if (!coarse(t)) return;
    int e = longestEdge(t);
    heap.push({mesh.edge(e).length, t, e});
  };

  if (regionVertices) {
    // Each triangle once, via a generation stamp that survives between calls.
    static thread_local std::vector<uint32_t> stamp;
    static thread_local uint32_t generation = 0;
    if (++generation == 0) {
      std::fill(stamp.begin(), stamp.end(), 0);
      generation = 1;
    }
    stamp.resize(mesh.triangleSlots(), 0);
    for (int v : *regionVertices) {
      const Vertex& vx = mesh.vertex(v);
      if (!vx.active) continue;
      for (int i = 0; i < vx.numTris; ++i) {
        int t = vx.tris[i];
        if (stamp[t] == generation) continue;
        stamp[t] = generation;
        push(t);
      }
    }
  } else {
    for (int t = 0; t < mesh.triangleSlots(); ++t) push(t);
  }

  int splits = 0;
  while (splits < brush.maxSplits && !heap.empty()) {
    Entry top = heap.top();
    heap.pop();
    if (!coarse(top.tri) || longestEdge(top.tri) != top.edge || mesh.edge(top.edge).length != top.length) continue;
    if (!canSplitEdge(mesh, top.edge)) continue;
    int m = splitEdge(mesh, top.edge);
    if (m < 0) break;
    ++splits;
    if (newVertices) newVertices->push_back(m);
    const Vertex& mv = mesh.vertex(m);
    for (int i = 0; i < mv.numTris; ++i) push(mv.tris[i]);
  }
  if (splits > 0) {
    if (brush.beautify) beautifyPass(mesh);
    snapPlaneVertices(mesh);
  }
  return splits;
}

}  // namespace bh

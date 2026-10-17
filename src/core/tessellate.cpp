#include "core/tessellate.h"

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
  mv.dirty = true;

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

int tessellateForBrush(Mesh& mesh, const BrushDetail& brush) {
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

  std::vector<int> candidates;
  for (int t = 0; t < mesh.triangleSlots(); ++t)
    if (coarse(t)) candidates.push_back(t);

  int splits = 0;
  std::vector<char> seen;
  while (splits < brush.maxSplits) {
    int best = -1;
    float bestLength = 0;
    for (int t : candidates) {
      const Triangle& tri = mesh.triangle(t);
      int longest = tri.e[0];
      for (int e : tri.e)
        if (mesh.edge(e).length > mesh.edge(longest).length) longest = e;
      if (mesh.edge(longest).length > bestLength && canSplitEdge(mesh, longest)) {
        best = longest;
        bestLength = mesh.edge(longest).length;
      }
    }
    if (best < 0) break;
    int m = splitEdge(mesh, best);
    if (m < 0) break;
    ++splits;

    seen.assign(mesh.triangleSlots(), 0);
    std::vector<int> next;
    const Vertex& mv = mesh.vertex(m);
    for (int i = 0; i < mv.numTris; ++i) {
      int t = mv.tris[i];
      if (coarse(t) && !seen[t]) { seen[t] = 1; next.push_back(t); }
    }
    for (int t : candidates)
      if (t < mesh.triangleSlots() && coarse(t) && !seen[t]) { seen[t] = 1; next.push_back(t); }
    candidates.swap(next);
  }
  if (splits > 0) {
    if (brush.beautify) beautifyPass(mesh);
    snapPlaneVertices(mesh);
  }
  return splits;
}

}  // namespace bh

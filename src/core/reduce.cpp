#include "core/reduce.h"

#include <array>
#include <vector>

#include "core/curve.h"
#include "core/relax.h"
#include "core/symmetry.h"

namespace bh {

namespace {

bool isOpposite(const Mesh& mesh, const Edge& edge, int v) {
  for (int t : edge.tri) {
    if (t < 0) continue;
    for (int x : mesh.triangle(t).v)
      if (x == v && x != edge.v[0] && x != edge.v[1]) return true;
  }
  return false;
}

}  // namespace

int collapseEdge(Mesh& mesh, int e) {
  const Edge edge = mesh.edge(e);
  if (!edge.active) return -1;
  const int v1 = edge.v[0], v2 = edge.v[1];
  Vertex& a = mesh.vertex(v1);
  const Vertex& b = mesh.vertex(v2);

  if (a.onSymmetryPlane != b.onSymmetryPlane) return -1;
  if (a.numEdges + b.numEdges - 4 > 18) return -1;

  // Link condition: common neighbors may only be the opposite vertices.
  for (int i = 0; i < a.numEdges; ++i) {
    int n = mesh.edge(a.edges[i]).other(v1);
    if (n == v2) continue;
    if (mesh.findEdge(n, v2) >= 0 && !isOpposite(mesh, edge, n)) return -1;
  }
  // Addition (not from the docs): an interior edge between two boundary
  // vertices would pinch the surface into a non-manifold point.
  if (edge.numTris() == 2 && isBoundaryVertex(mesh, v1) && isBoundaryVertex(mesh, v2)) return -1;

  Vec3 mid = curvedMidpoint(a.pos, a.normal, b.pos, b.normal);

  for (int t : edge.tri)
    if (t >= 0) mesh.removeTriangle(t, false);

  // Rebuild every remaining triangle of v2 with v1 in its place.
  struct Rebuild { std::array<int, 3> v; Vec3 normal; bool fileFlag; };
  std::vector<Rebuild> rebuild;
  const Vertex& vb = mesh.vertex(v2);
  for (int i = 0; i < vb.numTris; ++i) {
    const Triangle& t = mesh.triangle(vb.tris[i]);
    Rebuild r{t.v, t.normal, t.fileFlag};
    for (int& x : r.v)
      if (x == v2) x = v1;
    rebuild.push_back(r);
  }
  while (mesh.vertex(v2).numTris > 0) mesh.removeTriangle(mesh.vertex(v2).tris[0], false);
  mesh.removeIsolatedVertex(v2);

  mesh.vertex(v1).pos = mid;
  for (const Rebuild& r : rebuild) {
    int t = mesh.addTriangle(r.v[0], r.v[1], r.v[2], &r.normal);
    if (t >= 0) mesh.triangle(t).fileFlag = r.fileFlag;
  }

  Vertex& keep = mesh.vertex(v1);
  for (int i = 0; i < keep.numTris; ++i) mesh.updateTriangleGeometry(keep.tris[i]);
  std::vector<int> ring{v1};
  for (int i = 0; i < keep.numEdges; ++i) ring.push_back(mesh.edge(keep.edges[i]).other(v1));
  for (int v : ring) {
    Vertex& vx = mesh.vertex(v);
    for (int i = 0; i < vx.numTris; ++i) mesh.updateTriangleGeometry(vx.tris[i]);
  }
  for (int v : ring) updateVertexNormal(mesh, v);
  for (int v : ring)
    if (!isBoundaryVertex(mesh, v)) relaxTangential(mesh, v, 0.7f);
  for (int v : ring)
    if (mesh.vertex(v).onSymmetryPlane) mesh.vertex(v).pos.x = 0;
  mesh.vertex(v1).relaxCountdown = 10;
  markDirty(mesh, v1);
  return v1;
}

float reductionCost(const Mesh& mesh, int e, int pass) {
  const Edge& edge = mesh.edge(e);
  const Vertex& a = mesh.vertex(edge.v[0]);
  const Vertex& b = mesh.vertex(edge.v[1]);
  float c = 1200.0f / (1 + 0.5f * pass);
  float bend = 1 - dot(a.normal, b.normal);
  int val = a.numEdges + b.numEdges - 12;
  float cost = edge.length * (1 + c * bend);
  if (val > 0) cost *= 1 + 0.25f * val;
  return cost;
}

int reduceMesh(Mesh& mesh, const ReduceSettings& s) {
  if (!mesh.quadMarkers.empty()) mesh.clearQuads();
  auto workVertex = [&](int v) {
    const Vertex& vx = mesh.vertex(v);
    return vx.active && !vx.hidden && vx.weight != 0;
  };
  int total = 0;
  for (int pass = 0; pass < 3; ++pass) {
    double sumW = 0, sumCW = 0;  // weights 1 / cost^3
    for (int e = 0; e < mesh.edgeSlots(); ++e) {
      const Edge& edge = mesh.edge(e);
      if (!edge.active || !workVertex(edge.v[0]) || !workVertex(edge.v[1])) continue;
      double c = reductionCost(mesh, e, pass);
      if (c <= 0) continue;
      double w = 1 / (c * c * c);
      sumW += w;
      sumCW += c * w;
    }
    if (sumW <= 0) break;
    double k = 1 + 0.5 * pass;
    float threshold = float((0.7 + 1.4 * k * s.strength) * (sumCW / sumW));

    int collapses = 0;
    for (int v = 0; v < mesh.vertexSlots(); ++v) {
      if (!workVertex(v)) continue;
      const Vertex& vx = mesh.vertex(v);
      for (int i = 0; i < vx.numEdges; ++i) {
        int e = vx.edges[i];
        const Edge& edge = mesh.edge(e);
        if (!workVertex(edge.other(v)) || reductionCost(mesh, e, pass) >= threshold) continue;
        if (collapseEdge(mesh, e) >= 0) {
          ++collapses;
          break;
        }
      }
    }
    total += collapses;
    if (collapses >= 20) break;
  }
  mesh.updateGeometry();
  for (int i = 0; i < s.beautifyPasses; ++i) beautifyPass(mesh);
  snapPlaneVertices(mesh);
  mesh.updateGeometry();
  return total;
}

}  // namespace bh

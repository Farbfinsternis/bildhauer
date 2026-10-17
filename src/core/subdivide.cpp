#include "core/subdivide.h"

#include <array>
#include <vector>

#include "core/curve.h"
#include "core/symmetry.h"

namespace bh {

void subdivide(Mesh& mesh, const SubdivideSettings& s) {
  std::vector<int> vertexIndex(mesh.vertexSlots(), -1);
  std::vector<Vec3> positions;
  for (int v = 0; v < mesh.vertexSlots(); ++v) {
    if (!mesh.vertex(v).active) continue;
    vertexIndex[v] = int(positions.size());
    positions.push_back(mesh.vertex(v).pos);
  }

  std::vector<int> mid(mesh.edgeSlots(), -1);
  for (int e = 0; e < mesh.edgeSlots(); ++e) {
    const Edge& edge = mesh.edge(e);
    if (!edge.active) continue;
    const Vertex& a = mesh.vertex(edge.v[0]);
    const Vertex& b = mesh.vertex(edge.v[1]);
    Vec3 p = s.smooth ? curvedMidpoint(a.pos, a.normal, b.pos, b.normal) : (a.pos + b.pos) * 0.5f;
    if (a.onSymmetryPlane && b.onSymmetryPlane) p.x = 0;
    mid[e] = int(positions.size());
    positions.push_back(p);
  }

  // New faces: triangles have d = -1.
  struct Face { std::array<int, 4> v; };
  std::vector<Face> faces;
  const bool quads = !mesh.quadMarkers.empty();
  auto marker = [&](int t) { return t < int(mesh.quadMarkers.size()) ? mesh.quadMarkers[t] : 0; };

  for (int t = 0; t < mesh.triangleSlots(); ++t) {
    const Triangle& t1 = mesh.triangle(t);
    if (!t1.active) continue;
    const int v0 = vertexIndex[t1.v[0]], v1 = vertexIndex[t1.v[1]], v2 = vertexIndex[t1.v[2]];
    const int m0 = mid[t1.e[0]], m1 = mid[t1.e[1]], m2 = mid[t1.e[2]];

    // Quad (v0, v1, v2, q): T1 = (v0, v1, v2), T2 = (v2, q, v0) in the next
    // slot, diagonal v2-v0. Pairs that do not have this shape are split as two
    // triangles.
    if (quads && marker(t) == 1 && t + 1 < mesh.triangleSlots() && marker(t + 1) == 2) {
      const Triangle& t2 = mesh.triangle(t + 1);
      if (t2.active && t2.v[0] == t1.v[2] && t2.v[2] == t1.v[0]) {
        const int q = vertexIndex[t2.v[1]];
        const int n0 = mid[t2.e[0]], n1 = mid[t2.e[1]], c = m2;
        faces.push_back({{v0, m0, c, n1}});
        faces.push_back({{m0, v1, m1, c}});
        faces.push_back({{m1, v2, n0, c}});
        faces.push_back({{c, n0, q, n1}});
        ++t;
        continue;
      }
    }
    faces.push_back({{v0, m0, m2, -1}});
    faces.push_back({{m0, v1, m1, -1}});
    faces.push_back({{m1, v2, m2, -1}});
    faces.push_back({{m0, m1, m2, -1}});
  }

  Mesh out;
  out.symmetric = s.symmetry;
  for (const Vec3& p : positions) {
    int v = out.addVertex(p);
    if (s.symmetry && p.x == 0) out.vertex(v).onSymmetryPlane = true;
  }
  std::vector<uint8_t> markers;
  for (const Face& f : faces) {
    if (f.v[3] < 0) {
      out.addTriangle(f.v[0], f.v[1], f.v[2]);
      markers.push_back(0);
      continue;
    }
    // Quad (a, b, c, d) -> (a, b, c) and (c, d, a); their shared edge c-a is
    // the diagonal.
    int first = out.addTriangle(f.v[0], f.v[1], f.v[2]);
    out.addTriangle(f.v[2], f.v[3], f.v[0]);
    if (first >= 0) out.edge(out.triangle(first).e[2]).quadDiagonal = true;
    markers.push_back(1);
    markers.push_back(2);
  }
  if (quads) out.quadMarkers = std::move(markers);

  out.updateGeometry();
  for (int v = 0; v < out.vertexSlots(); ++v) out.vertex(v).dirty = true;
  if (s.symmetry) snapPlaneVertices(out);
  out.updateGeometry();
  out.storeReference();
  for (int v = 0; v < out.vertexSlots(); ++v) out.vertex(v).dirty = false;
  mesh = std::move(out);
}

}  // namespace bh

#include "core/symmetry.h"

#include <vector>

namespace bh {

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

}  // namespace bh

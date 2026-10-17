#include "core/relax.h"

#include <algorithm>

namespace bh {

bool isBoundaryVertex(const Mesh& mesh, int v) {
  const Vertex& vx = mesh.vertex(v);
  for (int i = 0; i < vx.numEdges; ++i)
    if (mesh.edge(vx.edges[i]).numTris() < 2) return true;
  return false;
}

void markDirty(Mesh& mesh, int v) {
  Vertex& vx = mesh.vertex(v);
  vx.dirty = true;
  int spread = vx.relaxCountdown - 2;
  for (int i = 0; i < vx.numEdges; ++i) {
    Vertex& n = mesh.vertex(mesh.edge(vx.edges[i]).other(v));
    n.dirty = true;
    if (spread > n.relaxCountdown) n.relaxCountdown = uint8_t(spread);
  }
}

void relaxTangential(Mesh& mesh, int v, float factor) {
  Vertex& vx = mesh.vertex(v);
  bool boundary = isBoundaryVertex(mesh, v);
  Vec3 sum;
  int count = 0;
  for (int i = 0; i < vx.numEdges; ++i) {
    const Edge& e = mesh.edge(vx.edges[i]);
    if (boundary && e.numTris() == 2) continue;
    sum += mesh.vertex(e.other(v)).pos;
    ++count;
  }
  if (count == 0) return;
  Vec3 d = sum / float(count) - vx.pos;
  d -= vx.normal * dot(vx.normal, d);
  vx.pos += d * factor;
  if (vx.onSymmetryPlane) vx.pos.x = 0;
}

void updateVertexNormal(Mesh& mesh, int v) {
  Vertex& vx = mesh.vertex(v);
  Vec3 sum;
  for (int i = 0; i < vx.numTris; ++i) sum += mesh.triangle(vx.tris[i]).normal;
  if (mesh.symmetric && vx.onSymmetryPlane) sum.x = 0;
  vx.normal = normalize(sum);
}

int beautifyPass(Mesh& mesh) {
  int moved = 0;
  for (int v = 0; v < mesh.vertexSlots(); ++v) {
    Vertex& vx = mesh.vertex(v);
    if (!vx.active || !vx.dirty) continue;
    if (vx.relaxCountdown == 0) {
      vx.dirty = false;
      continue;
    }
    --vx.relaxCountdown;
    if (vx.hidden || isBoundaryVertex(mesh, v)) continue;
    Vec3 meanNormal;
    for (int i = 0; i < vx.numEdges; ++i) meanNormal += mesh.vertex(mesh.edge(vx.edges[i]).other(v)).normal;
    if (vx.numEdges == 0) continue;
    float flat = length(meanNormal / float(vx.numEdges));
    float f = std::clamp((flat - 0.5f) * 2.1f, 0.0f, 1.0f);
    f *= f;
    if (f <= 0) continue;
    relaxTangential(mesh, v, f);
    markDirty(mesh, v);
    ++moved;
  }
  mesh.updateGeometry();
  return moved;
}

}  // namespace bh

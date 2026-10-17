#pragma once

// Surface picking (re/docs/brushes.md 5): the pointer ray against all
// triangles, nearest hit wins. For a symmetric half the mirrored ray is tested
// too, so the mirrored (rendered) half can be picked; the returned point is
// then mirrored back onto the stored half.

#include "core/mesh.h"

namespace bh {

struct PickHit {
  bool hit = false;
  float t = 0;            // ray parameter
  int triangle = -1;
  Vec3 point;             // on the stored mesh
  Vec3 normal;            // face normal on the stored mesh
  bool mirrored = false;  // hit on the mirrored half
};

PickHit pick(const Mesh& mesh, const Vec3& origin, const Vec3& dir);

}  // namespace bh

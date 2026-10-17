#pragma once

// Subdivision of the whole mesh (re/docs/mesh-core.md 6): every edge gets a
// midpoint, every triangle becomes 4 triangles and every quad 4 quads.
// Original vertices keep their positions.

#include "core/mesh.h"

namespace bh {

struct SubdivideSettings {
  bool smooth = true;     // "Smooth subdivide": curved midpoints; off = straight
  bool symmetry = false;  // mesh is a stored symmetric half
};

// Rebuilds the mesh. Afterwards: everything unhidden, mask cleared (weight 1),
// reference state = current state, plane vertices (x == 0) flagged and
// snapped when symmetry is on.
void subdivide(Mesh& mesh, const SubdivideSettings& settings = {});

}  // namespace bh

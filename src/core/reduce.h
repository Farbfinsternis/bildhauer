#pragma once

// Edge collapse and area reduction (re/docs/mesh-core.md 4).

#include "core/mesh.h"

namespace bh {

// Collapses edge v1-v2 into v1 at the curved midpoint. Rejected if v1 and v2
// share a neighbor other than the vertices opposite the edge, if their
// symmetry-plane flags differ, or if valence(v1) + valence(v2) - 4 > 18.
// Returns the surviving vertex, or -1.
int collapseEdge(Mesh& mesh, int e);

struct ReduceSettings {
  float strength = 0.4f;  // global 0x4B08D8 ("Reduce strength")
  int beautifyPasses = 10;
};

// Reduces the unmasked part of the mesh (all vertices with weight != 0).
// Returns the number of collapses.
int reduceMesh(Mesh& mesh, const ReduceSettings& settings = {});

// Edge cost used by reduceMesh for the given pass (0..2).
float reductionCost(const Mesh& mesh, int e, int pass);

}  // namespace bh

#pragma once

#include "core/mesh.h"

namespace bh {

// Builds the full mesh from a stored +x half (re/docs/symmetry.md 2.2):
// plane vertices are shared, every other vertex gets a mirror (-x, y, z), and
// every triangle is added a second time with mirrored vertices and reversed
// winding. A vertex at x == 0 that is not a plane vertex is moved to x = 1e-5
// first so it is mirrored instead of shared. The plane-valence reduction of
// the original is not part of this. Returns false if a triangle could not be
// added.
bool mirrorHalf(const Mesh& half, Mesh& full);

}  // namespace bh

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

// Keeps the seam exact (symmetry.md 3): every dirty vertex on the symmetry
// plane gets x = 0 for position and reference position, and normal.x = 0 for
// both normals (renormalized).
void snapPlaneVertices(Mesh& mesh);

// Lowers the valence of plane vertices to at most `maxEdges` before
// mirroring (symmetry.md 2.2 step 1): around each such vertex, quads become
// triangles, and the shortest edge opposite the vertex in a triangle not yet
// tried is collapsed, until the valence fits or no candidate is left.
// Returns the number of collapses.
int limitPlaneValence(Mesh& mesh, int maxEdges = 10);

// Symmetry off (symmetry.md 2.2): limits the plane valence, then replaces the
// stored +x half by the full mesh. The mirrored triangles follow the
// originals in the same order with reversed winding, so quad pairs stay
// consecutive and keep their diagonals. Plane flags and the mask are
// cleared. Returns false if a triangle could not be added; the mesh then
// keeps its half (with the reduced plane valence).
bool symmetryOff(Mesh& mesh);

// Symmetry on (symmetry.md 2.1): removes every triangle whose centroid lies
// at x < 0, puts the vertices they leave behind on the plane (x = 0), and
// removes triangles that end up flat on it; asymmetric detail on the -x side
// is lost. A quad that lost its partner becomes a triangle. The mask is
// cleared. Returns the number of quads converted to triangles.
int symmetryOn(Mesh& mesh);

}  // namespace bh

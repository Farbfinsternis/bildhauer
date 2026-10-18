#pragma once

// Neighborhood helpers, tangential relaxation and the beautify pass
// (re/docs/mesh-core.md section 5).

#include <vector>

#include "core/mesh.h"

namespace bh {

// A vertex is on the boundary if one of its edges has a single triangle.
bool isBoundaryVertex(const Mesh& mesh, int v);

// Marks v dirty and gives its neighbors a relax countdown of at least
// v.relaxCountdown - 2 (Vertex_MarkDirty).
void markDirty(Mesh& mesh, int v);

// Moves v toward the mean of its neighbors (boundary vertex: boundary
// neighbors only), keeping only the tangential part: p += f * (d - n dot(n, d)).
void relaxTangential(Mesh& mesh, int v, float factor);

// Vertex normal from the current face normals of its triangles.
void updateVertexNormal(Mesh& mesh, int v);

// One beautify pass (mesh-core.md 5.2) over the dirty vertices with relax
// level >= 2 that are not on the boundary: a tangential step of
// 0.9 f toward the neighbor centroid (quad diagonals left out), weighted by
// flatness f = clamp((|mean neighbor normal| - 0.5) * 2.1, 0, 1)^2. A vertex
// whose step is below 4 % of its mean edge length has converged and stops.
// Moved vertices spread level - 2 to their neighbors. Meant to run once per
// frame until mesh.dirtyVertices is empty. Returns the number of moved
// vertices.
int beautifyPass(Mesh& mesh);

}  // namespace bh

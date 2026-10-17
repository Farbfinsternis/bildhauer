#pragma once

// Edge split and detail under the brush (re/docs/mesh-core.md 2 and 3).

#include "core/mesh.h"

namespace bh {

// An edge may be split if each vertex opposite it has at most 18 edges.
bool canSplitEdge(const Mesh& mesh, int e);

// Splits the edge at its curved midpoint: 2 triangles become 4 (1 become 2 at
// the boundary), keeping their orientation. The new vertex gets the curved
// midpoint of the reference positions too, the mean weight, hidden = A or B,
// onSymmetryPlane = A and B, relax countdown 5. Drops the quad pairing.
// Returns the new vertex, or -1 if the edge cannot be split.
int splitEdge(Mesh& mesh, int e);

struct BrushDetail {
  Vec3 center;
  float radius = 0;        // brush radius (stroke +0xAC)
  float detailSize = 0;    // stroke +0xB0
  float scale = 1;         // call argument s (1 for all brushes but Reduce)
  float detailFactor = 1.1f;  // global G ("Detail")
  int maxSplits = 3000;
  bool beautify = true;  // "Beautify/relax mesh (always used when adding detail)"
};

// Splits the longest splittable edge among the coarse triangles under the
// brush until none is left (or maxSplits is reached), then runs one beautify
// pass (if enabled). A triangle is coarse if size > detailSize / (2 s) / G.
// Returns the number of splits.
int tessellateForBrush(Mesh& mesh, const BrushDetail& brush);

}  // namespace bh

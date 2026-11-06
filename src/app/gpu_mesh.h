#pragma once

// The mesh as the graphics card gets it: a vertex per vertex slot and three
// indices per triangle slot, so that a changed element keeps its place in the
// buffers and a stroke uploads only what it touched. No OpenGL here, so that
// the Windows and the Android renderer can share it and a test can check it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/mesh.h"

namespace bh::viewer {

struct GpuVertex {
  Vec3 position, normal;
  float weight;  // mask weight; below 0 for a hidden vertex
};

struct GpuRange {
  size_t first, count;  // in elements of the array they refer to
};

struct GpuMesh {
  std::vector<GpuVertex> vertices;
  std::vector<uint32_t> indices;  // free triangle slots: 0 0 0, which draws nothing
  // What update changed, to be sent to the graphics card.
  std::vector<GpuRange> vertexRanges, indexRanges;
  bool topologyChanged = false;

  // Takes over the mesh's changes; with `all`, renews everything.
  void update(Mesh& mesh, bool all);

 private:
  std::vector<int> changedVertices_, changedTriangles_;
};

}  // namespace bh::viewer

#include "app/gpu_mesh.h"

#include <algorithm>

namespace bh::viewer {

namespace {

// Elements between two changes that are sent along rather than starting a new
// upload call, and the calls worth making before one span is cheaper.
constexpr size_t kGap = 32, kMaxRanges = 256;

// `slots` in order, as ranges of `scale` elements per slot.
void toRanges(std::vector<int>& slots, size_t scale, std::vector<GpuRange>& ranges) {
  ranges.clear();
  if (slots.empty()) return;
  std::sort(slots.begin(), slots.end());
  size_t first = size_t(slots[0]), last = first;
  for (int s : slots) {
    if (size_t(s) > last + kGap) {
      ranges.push_back({first * scale, (last - first + 1) * scale});
      first = size_t(s);
    }
    last = size_t(s);
  }
  ranges.push_back({first * scale, (last - first + 1) * scale});
  if (ranges.size() > kMaxRanges) {
    const size_t start = ranges.front().first, end = ranges.back().first + ranges.back().count;
    ranges.assign(1, {start, end - start});
  }
}

GpuVertex gpuVertex(const Vertex& v) {
  if (!v.active) return {};
  return {v.pos, v.normal, v.hidden ? -1.0f : v.weight};
}

void setTriangle(const Mesh& m, int t, std::vector<uint32_t>& indices) {
  const Triangle& tri = m.triangle(t);
  for (size_t k = 0; k < 3; ++k) indices[size_t(t) * 3 + k] = tri.active ? uint32_t(tri.v[k]) : 0;
}

}  // namespace

void GpuMesh::update(Mesh& mesh, bool all) {
  all = mesh.takeChanges(changedVertices_, changedTriangles_) || all;
  vertices.resize(size_t(mesh.vertexSlots()));
  indices.resize(size_t(mesh.triangleSlots()) * 3);
  if (all) {
    for (int v = 0; v < mesh.vertexSlots(); ++v) vertices[size_t(v)] = gpuVertex(mesh.vertex(v));
    for (int t = 0; t < mesh.triangleSlots(); ++t) setTriangle(mesh, t, indices);
    vertexRanges.assign(1, {0, vertices.size()});
    indexRanges.assign(1, {0, indices.size()});
    topologyChanged = true;
    return;
  }
  for (int v : changedVertices_) vertices[size_t(v)] = gpuVertex(mesh.vertex(v));
  for (int t : changedTriangles_) setTriangle(mesh, t, indices);
  topologyChanged = !changedTriangles_.empty();
  toRanges(changedVertices_, 1, vertexRanges);
  toRanges(changedTriangles_, 3, indexRanges);
}

}  // namespace bh::viewer

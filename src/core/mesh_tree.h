#pragma once

// A bounding box tree over the triangles of a mesh, so that picking and the
// brushes cost what lies under the pointer, not what the model holds. The
// leaves hold about a hundred triangles each. The tree follows the mesh: it
// grows with the triangles the mesh recomputes, places new ones on the next
// query, splits a leaf that has filled up, and is copied along with the mesh, so
// undo does not have to rebuild it.

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

#include "core/vec3.h"

namespace bh {

class Mesh;

class MeshTree {
 public:
  // From the mesh; they cost nothing until the first query builds the tree.
  void triangleAdded(int t) {
    if (built_) added_.push_back(t);
  }
  void triangleRemoved(int t);
  // A triangle with new geometry: its leaf grows at once if it has to. Done here
  // because the mesh has just computed the triangle: a second pass over what a
  // large brush moved costs more than the search saves.
  void triangleChanged(int t, const Vec3& lo, const Vec3& hi, float radius) {
    if (!built_ || size_t(t) >= leafOf_.size() || leafOf_[size_t(t)] < 0) return;
    const Node& n = nodes_[size_t(leafOf_[size_t(t)])];
    if (lo.x < n.lo.x || lo.y < n.lo.y || lo.z < n.lo.z || hi.x > n.hi.x || hi.y > n.hi.y || hi.z > n.hi.z ||
        radius > n.triangleRadius)
      grow(leafOf_[size_t(t)], lo, hi, radius);
  }
  void invalidate();

  // Applies what the mesh reported, or builds the tree. Mesh::tree() calls it.
  void update(const Mesh& mesh);

  // Visits the triangle lists of the leaves along the ray, nearest box first.
  // limit() is the ray parameter beyond which nothing matters any more.
  template <class Limit, class Visit>
  void ray(const Vec3& origin, const Vec3& dir, Limit limit, Visit visit) const;

  // Visits the triangle lists of the leaves within `radius` of `center`. With
  // `triangleRadius`, the radius of each leaf's largest triangle counts too,
  // for tests against a triangle's centroid and bounding sphere.
  template <class Visit>
  void sphere(const Vec3& center, float radius, bool triangleRadius, Visit visit) const;

  size_t storageBytes() const;

  // For tests: false if a triangle is missing, doubled or outside its box.
  bool validate(const Mesh& mesh) const;

 private:
  struct Node {
    Vec3 lo, hi;
    float triangleRadius = 0;
    int parent = -1, left = -1, right = -1;  // left < 0: a leaf
    bool loose = false;
    std::vector<int> triangles;
  };

  void build(const Mesh& mesh);
  int buildNode(const Mesh& mesh, std::vector<int>& triangles, std::vector<Vec3>& centers, size_t first, size_t last,
                int parent);
  void place(int leaf, int triangle);
  void insert(const Mesh& mesh, int triangle);
  void split(const Mesh& mesh, int leaf);
  void grow(int leaf, const Vec3& lo, const Vec3& hi, float radius);
  void fit(const Mesh& mesh, Node& leaf);
  void fitParents(int node);

  bool built_ = false;
  std::vector<Node> nodes_;
  std::vector<int> leafOf_, placeIn_;  // per triangle slot: its leaf (-1: none), its index there
  std::vector<int> added_;
  std::vector<int> loose_;  // leaves that grew: their boxes are tightened a few at a time
};

template <class Limit, class Visit>
void MeshTree::ray(const Vec3& origin, const Vec3& dir, Limit limit, Visit visit) const {
  if (nodes_.empty()) return;
  const float inf = std::numeric_limits<float>::infinity();
  const Vec3 inv{dir.x != 0 ? 1 / dir.x : inf, dir.y != 0 ? 1 / dir.y : inf, dir.z != 0 ? 1 / dir.z : inf};
  // The ray parameter where it enters the box, or infinity if it misses.
  auto enter = [&](const Node& n) {
    if (n.lo.x > n.hi.x) return inf;  // an emptied leaf
    float entry = 0, exit = inf;
    const float o[3] = {origin.x, origin.y, origin.z}, d[3] = {dir.x, dir.y, dir.z}, i[3] = {inv.x, inv.y, inv.z};
    const float lo[3] = {n.lo.x, n.lo.y, n.lo.z}, hi[3] = {n.hi.x, n.hi.y, n.hi.z};
    for (int k = 0; k < 3; ++k) {
      if (d[k] == 0) {
        if (o[k] < lo[k] || o[k] > hi[k]) return inf;
        continue;
      }
      float a = (lo[k] - o[k]) * i[k], b = (hi[k] - o[k]) * i[k];
      if (a > b) std::swap(a, b);
      entry = std::max(entry, a);
      exit = std::min(exit, b);
    }
    return entry <= exit ? entry : inf;
  };
  struct Entry {
    int node;
    float t;
  };
  std::vector<Entry> stack;
  stack.reserve(64);
  stack.push_back({0, enter(nodes_[0])});
  while (!stack.empty()) {
    const Entry e = stack.back();
    stack.pop_back();
    if (e.t == inf || e.t > limit()) continue;
    const Node& n = nodes_[size_t(e.node)];
    if (n.left < 0) {
      visit(n.triangles);
      continue;
    }
    Entry a{n.left, enter(nodes_[size_t(n.left)])}, b{n.right, enter(nodes_[size_t(n.right)])};
    if (a.t < b.t) std::swap(a, b);  // the nearer one on top of the stack
    stack.push_back(a);
    stack.push_back(b);
  }
}

template <class Visit>
void MeshTree::sphere(const Vec3& center, float radius, bool triangleRadius, Visit visit) const {
  if (nodes_.empty()) return;
  std::vector<int> stack;
  stack.reserve(64);
  stack.push_back(0);
  while (!stack.empty()) {
    const Node& n = nodes_[size_t(stack.back())];
    stack.pop_back();
    if (n.lo.x > n.hi.x) continue;
    const float dx = std::max({n.lo.x - center.x, 0.0f, center.x - n.hi.x});
    const float dy = std::max({n.lo.y - center.y, 0.0f, center.y - n.hi.y});
    const float dz = std::max({n.lo.z - center.z, 0.0f, center.z - n.hi.z});
    const float reach = radius + (triangleRadius ? n.triangleRadius : 0);
    if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
    if (n.left < 0) {
      visit(n.triangles);
    } else {
      stack.push_back(n.left);
      stack.push_back(n.right);
    }
  }
}

}  // namespace bh

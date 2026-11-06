#include "core/mesh_tree.h"

#include "core/mesh.h"

namespace bh {

namespace {

constexpr size_t kLeafSize = 128;  // triangles per leaf; a leaf splits at twice that

Vec3 centroid(const Mesh& m, int t) {
  const Triangle& tri = m.triangle(t);
  return (m.vertex(tri.v[0]).pos + m.vertex(tri.v[1]).pos + m.vertex(tri.v[2]).pos) / 3;
}

Vec3 lowest(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
Vec3 highest(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

constexpr float kHuge = 3e38f;

int longestAxis(const Vec3& extent) { return extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2; }
float axis(const Vec3& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }

}  // namespace

void MeshTree::invalidate() {
  built_ = false;
  nodes_.clear();
  leafOf_.clear();
  placeIn_.clear();
  added_.clear();
  loose_.clear();
}

void MeshTree::place(int leaf, int triangle) {
  Node& n = nodes_[size_t(leaf)];
  if (size_t(triangle) >= leafOf_.size()) {
    leafOf_.resize(size_t(triangle) + 1 + size_t(triangle) / 4, -1);
    placeIn_.resize(leafOf_.size(), -1);
  }
  leafOf_[size_t(triangle)] = leaf;
  placeIn_[size_t(triangle)] = int(n.triangles.size());
  n.triangles.push_back(triangle);
}

void MeshTree::triangleRemoved(int t) {
  if (!built_ || size_t(t) >= leafOf_.size() || leafOf_[size_t(t)] < 0) return;
  const int leaf = leafOf_[size_t(t)];
  Node& n = nodes_[size_t(leaf)];
  const int at = placeIn_[size_t(t)], last = n.triangles.back();
  n.triangles[size_t(at)] = last;
  placeIn_[size_t(last)] = at;
  n.triangles.pop_back();
  leafOf_[size_t(t)] = -1;
}

// With a little room to spare, so that the next small move does not come here
// again, and up the tree as far as the boxes do not hold it yet.
void MeshTree::grow(int leaf, const Vec3& lo, const Vec3& hi, float radius) {
  Node& n = nodes_[size_t(leaf)];
  const bool empty = n.lo.x > n.hi.x;
  const Vec3 extent = (empty ? hi - lo : n.hi - n.lo);
  const float margin = 0.05f * std::max({extent.x, extent.y, extent.z}) + 1e-6f;
  const Vec3 room{margin, margin, margin};
  n.lo = lowest(n.lo, lo - room);
  n.hi = highest(n.hi, hi + room);
  n.triangleRadius = std::max(n.triangleRadius, radius);
  if (!n.loose) {
    n.loose = true;
    loose_.push_back(leaf);
  }
  const Vec3 boxLo = n.lo, boxHi = n.hi;
  for (int p = n.parent; p >= 0; p = nodes_[size_t(p)].parent) {
    Node& up = nodes_[size_t(p)];
    const Vec3 newLo = lowest(up.lo, boxLo), newHi = highest(up.hi, boxHi);
    if (newLo == up.lo && newHi == up.hi && radius <= up.triangleRadius) break;
    up.lo = newLo;
    up.hi = newHi;
    up.triangleRadius = std::max(up.triangleRadius, radius);
  }
}

// The box of a leaf, a little larger than its triangles, so that rounding in
// the ray test cannot lose a hit on the very surface of the box.
void MeshTree::fit(const Mesh& mesh, Node& leaf) {
  Vec3 lo{kHuge, kHuge, kHuge}, hi{-kHuge, -kHuge, -kHuge};
  float radius = 0;
  for (int t : leaf.triangles) {
    const Triangle& tri = mesh.triangle(t);
    for (int v : tri.v) {
      lo = lowest(lo, mesh.vertex(v).pos);
      hi = highest(hi, mesh.vertex(v).pos);
    }
    radius = std::max(radius, tri.radius);
  }
  if (!leaf.triangles.empty()) {
    const Vec3 extent = hi - lo;
    const float margin = 1e-4f * std::max({extent.x, extent.y, extent.z}) + 1e-6f;
    lo -= Vec3{margin, margin, margin};
    hi += Vec3{margin, margin, margin};
  }
  leaf.lo = lo;
  leaf.hi = hi;
  leaf.triangleRadius = radius;
}

void MeshTree::fitParents(int node) {
  for (int p = nodes_[size_t(node)].parent; p >= 0; p = nodes_[size_t(p)].parent) {
    Node& n = nodes_[size_t(p)];
    const Node &a = nodes_[size_t(n.left)], &b = nodes_[size_t(n.right)];
    const Vec3 lo = lowest(a.lo, b.lo), hi = highest(a.hi, b.hi);
    const float radius = std::max(a.triangleRadius, b.triangleRadius);
    if (lo == n.lo && hi == n.hi && radius == n.triangleRadius) break;
    n.lo = lo;
    n.hi = hi;
    n.triangleRadius = radius;
  }
}

int MeshTree::buildNode(const Mesh& mesh, std::vector<int>& triangles, std::vector<Vec3>& centers, size_t first,
                        size_t last, int parent) {
  const int index = int(nodes_.size());
  nodes_.emplace_back();
  nodes_[size_t(index)].parent = parent;
  if (last - first <= kLeafSize) {
    for (size_t i = first; i < last; ++i) place(index, triangles[i]);
    fit(mesh, nodes_[size_t(index)]);
    return index;
  }
  Vec3 lo{kHuge, kHuge, kHuge}, hi{-kHuge, -kHuge, -kHuge};
  for (size_t i = first; i < last; ++i) {
    lo = lowest(lo, centers[size_t(triangles[i])]);
    hi = highest(hi, centers[size_t(triangles[i])]);
  }
  const int k = longestAxis(hi - lo);
  const size_t middle = first + (last - first) / 2;
  std::nth_element(triangles.begin() + std::ptrdiff_t(first), triangles.begin() + std::ptrdiff_t(middle),
                   triangles.begin() + std::ptrdiff_t(last),
                   [&](int a, int b) { return axis(centers[size_t(a)], k) < axis(centers[size_t(b)], k); });
  const int left = buildNode(mesh, triangles, centers, first, middle, index);
  const int right = buildNode(mesh, triangles, centers, middle, last, index);
  Node& n = nodes_[size_t(index)];
  n.left = left;
  n.right = right;
  const Node &a = nodes_[size_t(left)], &b = nodes_[size_t(right)];
  n.lo = lowest(a.lo, b.lo);
  n.hi = highest(a.hi, b.hi);
  n.triangleRadius = std::max(a.triangleRadius, b.triangleRadius);
  return index;
}

void MeshTree::build(const Mesh& mesh) {
  invalidate();
  std::vector<int> triangles;
  triangles.reserve(size_t(mesh.numTriangles()));
  std::vector<Vec3> centers(size_t(mesh.triangleSlots()));
  for (int t = 0; t < mesh.triangleSlots(); ++t) {
    if (!mesh.triangle(t).active) continue;
    triangles.push_back(t);
    centers[size_t(t)] = centroid(mesh, t);
  }
  leafOf_.assign(size_t(mesh.triangleSlots()), -1);
  placeIn_.assign(size_t(mesh.triangleSlots()), -1);
  nodes_.reserve(triangles.size() / (kLeafSize / 2) * 2 + 2);
  buildNode(mesh, triangles, centers, 0, triangles.size(), -1);
  built_ = true;
}

void MeshTree::split(const Mesh& mesh, int leaf) {
  std::vector<int> triangles;
  triangles.swap(nodes_[size_t(leaf)].triangles);
  std::vector<std::pair<float, int>> order;
  Vec3 lo{kHuge, kHuge, kHuge}, hi{-kHuge, -kHuge, -kHuge};
  for (int t : triangles) {
    const Vec3 c = centroid(mesh, t);
    lo = lowest(lo, c);
    hi = highest(hi, c);
  }
  const int k = longestAxis(hi - lo);
  order.reserve(triangles.size());
  for (int t : triangles) order.push_back({axis(centroid(mesh, t), k), t});
  std::sort(order.begin(), order.end());
  const int left = int(nodes_.size()), right = left + 1;
  nodes_.resize(nodes_.size() + 2);
  nodes_[size_t(left)].parent = nodes_[size_t(right)].parent = leaf;
  nodes_[size_t(leaf)].left = left;
  nodes_[size_t(leaf)].right = right;
  for (size_t i = 0; i < order.size(); ++i) place(i < order.size() / 2 ? left : right, order[i].second);
  nodes_[size_t(leaf)].loose = false;  // an inner node now; its entry in loose_ is skipped
  fit(mesh, nodes_[size_t(left)]);
  fit(mesh, nodes_[size_t(right)]);
}

void MeshTree::insert(const Mesh& mesh, int triangle) {
  const Vec3 c = centroid(mesh, triangle);
  int node = 0;
  // Down the side that has to grow less for the new triangle.
  auto growth = [&](const Node& n) {
    if (n.lo.x > n.hi.x) return 0.0f;
    const Vec3 lo = lowest(n.lo, c), hi = highest(n.hi, c), e = hi - lo, f = n.hi - n.lo;
    return (e.x + e.y + e.z) - (f.x + f.y + f.z);
  };
  while (nodes_[size_t(node)].left >= 0) {
    const Node& n = nodes_[size_t(node)];
    node = growth(nodes_[size_t(n.left)]) <= growth(nodes_[size_t(n.right)]) ? n.left : n.right;
  }
  place(node, triangle);
  const Triangle& tri = mesh.triangle(triangle);
  const Vec3 &a = mesh.vertex(tri.v[0]).pos, &b = mesh.vertex(tri.v[1]).pos, &d = mesh.vertex(tri.v[2]).pos;
  grow(node, lowest(a, lowest(b, d)), highest(a, highest(b, d)), tri.radius);
  if (nodes_[size_t(node)].triangles.size() > 2 * kLeafSize) split(mesh, node);
}

void MeshTree::update(const Mesh& mesh) {
  if (!built_) {
    build(mesh);
    return;
  }
  for (int t : added_) {
    const bool placed = size_t(t) < leafOf_.size() && leafOf_[size_t(t)] >= 0;
    if (mesh.triangle(t).active && !placed) insert(mesh, t);
  }
  added_.clear();
  // Boxes only ever grow while the mesh changes, which is always correct, but
  // looser with time. A few per query is enough to catch up.
  for (int i = 0; i < 16 && !loose_.empty(); ++i) {
    const int leaf = loose_.back();
    loose_.pop_back();
    Node& n = nodes_[size_t(leaf)];
    if (!n.loose) continue;
    n.loose = false;
    fit(mesh, n);
    fitParents(leaf);
  }
}

size_t MeshTree::storageBytes() const {
  size_t bytes = nodes_.capacity() * sizeof(Node) + (leafOf_.capacity() + placeIn_.capacity()) * sizeof(int);
  for (const Node& n : nodes_) bytes += n.triangles.capacity() * sizeof(int);
  return bytes;
}

bool MeshTree::validate(const Mesh& mesh) const {
  int found = 0;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const Node& n = nodes_[i];
    if (n.left >= 0 && !n.triangles.empty()) return false;
    for (int t : n.triangles) {
      const Triangle& tri = mesh.triangle(t);
      if (!tri.active || leafOf_[size_t(t)] != int(i)) return false;
      for (int v : tri.v) {
        const Vec3 p = mesh.vertex(v).pos;
        if (p.x < n.lo.x || p.y < n.lo.y || p.z < n.lo.z || p.x > n.hi.x || p.y > n.hi.y || p.z > n.hi.z) return false;
      }
      // Every box up to the root holds the leaf.
      for (int a = n.parent; a >= 0; a = nodes_[size_t(a)].parent) {
        const Node& up = nodes_[size_t(a)];
        if (n.lo.x < up.lo.x || n.lo.y < up.lo.y || n.lo.z < up.lo.z || n.hi.x > up.hi.x || n.hi.y > up.hi.y || n.hi.z > up.hi.z)
          return false;
      }
      ++found;
    }
  }
  return found == mesh.numTriangles();
}

}  // namespace bh

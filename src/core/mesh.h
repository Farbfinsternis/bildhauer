#pragma once

// Mesh kernel: explicit vertex/edge/triangle stores with back references, as
// described in re/docs/mesh-core.md section 1. Elements live in slot arrays
// with an active flag; freed slots are reused. All references are slot
// indices, -1 = none.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/vec3.h"

namespace bh {

constexpr int kMaxValence = 24;  // hard limit of edges/triangles per vertex

struct Vertex {
  bool active = false;
  bool dirty = false;
  bool onSymmetryPlane = false;
  bool hidden = false;
  uint8_t relaxCountdown = 0;
  float weight = 1.0f;  // mask weight: 1 = editable, 0 = masked
  Vec3 pos, refPos;     // refPos: state at stroke start
  Vec3 normal, refNormal;
  std::array<int, kMaxValence> tris{};
  std::array<int, kMaxValence> edges{};
  int numTris = 0;
  int numEdges = 0;
};

struct Edge {
  bool active = false;
  bool quadDiagonal = false;
  std::array<int, 2> v{-1, -1};
  std::array<int, 2> tri{-1, -1};  // one -1 = boundary edge
  float length = 0;

  int numTris() const { return (tri[0] >= 0) + (tri[1] >= 0); }
  int other(int vert) const { return v[0] == vert ? v[1] : v[0]; }
};

struct Triangle {
  bool active = false;
  bool fileFlag = false;  // triangle flag from the .sc1 file
  std::array<int, 3> v{-1, -1, -1};
  std::array<int, 3> e{-1, -1, -1};  // e0 = (v0,v1), e1 = (v1,v2), e2 = (v2,v0)
  Vec3 normal;
  Vec3 centroid;
  float radius = 0;  // max distance centroid -> vertex
  float size = 0;    // 3 x longest edge
};

class Mesh {
 public:
  int addVertex(const Vec3& pos);

  // Builds triangle (a, b, c) and creates missing edges. If orientHint is given
  // and the face normal points away from it, v0 and v2 are swapped. Returns the
  // slot, or -1 (degenerate, valence limit, or an edge already has 2
  // triangles); the mesh is left unchanged on failure.
  int addTriangle(int a, int b, int c, const Vec3* orientHint = nullptr);

  // Deregisters the triangle from its vertices and edges; edges without
  // triangles are removed, and with removeOrphans also vertices without any.
  void removeTriangle(int t, bool removeOrphans);

  int findEdge(int a, int b) const;

  // Deactivates a vertex that no longer has triangles or edges.
  bool removeIsolatedVertex(int v);

  // Face normals, centroids, radius, size; then vertex normals as the
  // normalized, unweighted sum of the adjacent face normals. With symmetric,
  // plane vertices are snapped first and their normals get x = 0.
  void updateGeometry();
  void updateTriangleGeometry(int t) { updateTriangle(triangles_[t]); }

  int vertexSlots() const { return int(vertices_.size()); }
  int edgeSlots() const { return int(edges_.size()); }
  int triangleSlots() const { return int(triangles_.size()); }
  int numVertices() const { return numVertices_; }
  int numEdges() const { return numEdges_; }
  int numTriangles() const { return numTriangles_; }

  Vertex& vertex(int i) { return vertices_[i]; }
  const Vertex& vertex(int i) const { return vertices_[i]; }
  Edge& edge(int i) { return edges_[i]; }
  const Edge& edge(int i) const { return edges_[i]; }
  Triangle& triangle(int i) { return triangles_[i]; }
  const Triangle& triangle(int i) const { return triangles_[i]; }

  // Reference state = current state (stroke start, Job_StoreReference).
  void storeReference();

  // Drops the quad pairing: markers and diagonal flags.
  void clearQuads();

  // Checks all back references and limits; returns false with a message.
  bool validate(std::string* error) const;

  // Polygon list: per triangle slot 0 = triangle, 1 = first and 2 = second
  // triangle of a quad (slots t and t + 1). Empty = no quads.
  std::vector<uint8_t> quadMarkers;

  // Stored +x half of a symmetric mesh: updateGeometry keeps plane vertices
  // on x = 0 with normal.x = 0 (symmetry.md 3).
  bool symmetric = false;

 private:
  int newEdge(int a, int b);
  void removeEdge(int e);
  void removeVertex(int v);
  void updateTriangle(Triangle& t);

  std::vector<Vertex> vertices_;
  std::vector<Edge> edges_;
  std::vector<Triangle> triangles_;
  std::vector<int> freeVertices_, freeEdges_, freeTriangles_;
  int numVertices_ = 0, numEdges_ = 0, numTriangles_ = 0;
};

}  // namespace bh

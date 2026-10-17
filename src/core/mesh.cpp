#include "core/mesh.h"

#include <algorithm>

namespace bh {

namespace {

template <typename T>
int takeSlot(std::vector<T>& store, std::vector<int>& freeList) {
  if (!freeList.empty()) {
    int i = freeList.back();
    freeList.pop_back();
    store[i] = T{};
    return i;
  }
  store.emplace_back();
  return int(store.size()) - 1;
}

template <size_t N>
bool removeFrom(std::array<int, N>& list, int& count, int value) {
  for (int i = 0; i < count; ++i) {
    if (list[i] == value) {
      list[i] = list[--count];
      return true;
    }
  }
  return false;
}

}  // namespace

int Mesh::addVertex(const Vec3& pos) {
  int i = takeSlot(vertices_, freeVertices_);
  Vertex& v = vertices_[i];
  v.active = true;
  v.dirty = true;
  v.pos = v.refPos = pos;
  ++numVertices_;
  return i;
}

int Mesh::findEdge(int a, int b) const {
  const Vertex& va = vertices_[a];
  for (int i = 0; i < va.numEdges; ++i) {
    const Edge& e = edges_[va.edges[i]];
    if ((e.v[0] == a && e.v[1] == b) || (e.v[0] == b && e.v[1] == a)) return va.edges[i];
  }
  return -1;
}

int Mesh::newEdge(int a, int b) {
  int i = takeSlot(edges_, freeEdges_);
  Edge& e = edges_[i];
  e.active = true;
  e.v = {a, b};
  e.length = length(vertices_[b].pos - vertices_[a].pos);
  Vertex& va = vertices_[a];
  Vertex& vb = vertices_[b];
  va.edges[va.numEdges++] = i;
  vb.edges[vb.numEdges++] = i;
  ++numEdges_;
  return i;
}

int Mesh::addTriangle(int a, int b, int c, const Vec3* orientHint) {
  std::array<int, 3> vs{a, b, c};
  for (int v : vs)
    if (v < 0 || v >= vertexSlots() || !vertices_[v].active) return -1;
  if (a == b || b == c || c == a) return -1;

  Vec3 n = normalize(cross(vertices_[b].pos - vertices_[a].pos, vertices_[c].pos - vertices_[a].pos));
  if (orientHint && dot(n, *orientHint) < 0) std::swap(vs[0], vs[2]);

  // Check every limit before touching anything.
  std::array<int, 3> existing{};
  std::array<int, 3> gained{};  // new edges per vertex
  for (int k = 0; k < 3; ++k) {
    int p = vs[k], q = vs[(k + 1) % 3];
    existing[k] = findEdge(p, q);
    if (existing[k] >= 0) {
      if (edges_[existing[k]].numTris() >= 2) return -1;
    } else {
      ++gained[k];
      ++gained[(k + 1) % 3];
    }
  }
  for (int k = 0; k < 3; ++k) {
    const Vertex& v = vertices_[vs[k]];
    if (v.numTris >= kMaxValence || v.numEdges + gained[k] > kMaxValence) return -1;
  }

  int ti = takeSlot(triangles_, freeTriangles_);
  Triangle& t = triangles_[ti];
  t.active = true;
  t.v = vs;
  for (int k = 0; k < 3; ++k) {
    Vertex& v = vertices_[vs[k]];
    v.tris[v.numTris++] = ti;
    v.dirty = true;
  }
  for (int k = 0; k < 3; ++k) {
    int e = existing[k] >= 0 ? existing[k] : newEdge(vs[k], vs[(k + 1) % 3]);
    Edge& edge = edges_[e];
    edge.tri[edge.tri[0] < 0 ? 0 : 1] = ti;
    triangles_[ti].e[k] = e;
  }
  updateTriangle(triangles_[ti]);
  ++numTriangles_;
  return ti;
}

void Mesh::removeEdge(int e) {
  Edge& edge = edges_[e];
  for (int v : edge.v) removeFrom(vertices_[v].edges, vertices_[v].numEdges, e);
  edge.active = false;
  freeEdges_.push_back(e);
  --numEdges_;
}

void Mesh::removeVertex(int v) {
  vertices_[v].active = false;
  freeVertices_.push_back(v);
  --numVertices_;
}

void Mesh::removeTriangle(int ti, bool removeOrphans) {
  Triangle& t = triangles_[ti];
  if (!t.active) return;
  for (int v : t.v) {
    removeFrom(vertices_[v].tris, vertices_[v].numTris, ti);
    vertices_[v].dirty = true;
  }
  for (int e : t.e) {
    Edge& edge = edges_[e];
    if (edge.tri[0] == ti) edge.tri[0] = edge.tri[1];
    edge.tri[1] = -1;
    if (edge.tri[0] < 0) removeEdge(e);
  }
  if (removeOrphans)
    for (int v : t.v)
      if (vertices_[v].active && vertices_[v].numTris == 0 && vertices_[v].numEdges == 0) removeVertex(v);
  t.active = false;
  freeTriangles_.push_back(ti);
  --numTriangles_;
}

void Mesh::updateTriangle(Triangle& t) {
  const Vec3& p0 = vertices_[t.v[0]].pos;
  const Vec3& p1 = vertices_[t.v[1]].pos;
  const Vec3& p2 = vertices_[t.v[2]].pos;
  t.normal = normalize(cross(p1 - p0, p2 - p0));
  t.centroid = (p0 + p1 + p2) / 3.0f;
  t.radius = std::max({length(p0 - t.centroid), length(p1 - t.centroid), length(p2 - t.centroid)});
  float longest = 0;
  for (int e : t.e) {
    Edge& edge = edges_[e];
    edge.length = length(vertices_[edge.v[1]].pos - vertices_[edge.v[0]].pos);
    longest = std::max(longest, edge.length);
  }
  t.size = 3.0f * longest;
}

void Mesh::updateGeometry() {
  for (Triangle& t : triangles_)
    if (t.active) updateTriangle(t);
  for (Vertex& v : vertices_) {
    if (!v.active) continue;
    Vec3 sum;
    for (int i = 0; i < v.numTris; ++i) sum += triangles_[v.tris[i]].normal;
    v.normal = normalize(sum);
  }
}

bool Mesh::validate(std::string* error) const {
  auto fail = [&](std::string msg) {
    if (error) *error = std::move(msg);
    return false;
  };
  int nv = 0, ne = 0, nt = 0;
  for (int vi = 0; vi < vertexSlots(); ++vi) {
    const Vertex& v = vertices_[vi];
    if (!v.active) continue;
    ++nv;
    if (v.numTris > kMaxValence || v.numEdges > kMaxValence)
      return fail("vertex " + std::to_string(vi) + " exceeds valence limit");
    for (int i = 0; i < v.numTris; ++i) {
      const Triangle& t = triangles_[v.tris[i]];
      if (!t.active || std::find(t.v.begin(), t.v.end(), vi) == t.v.end())
        return fail("vertex " + std::to_string(vi) + " has a stale triangle reference");
    }
    for (int i = 0; i < v.numEdges; ++i) {
      const Edge& e = edges_[v.edges[i]];
      if (!e.active || (e.v[0] != vi && e.v[1] != vi))
        return fail("vertex " + std::to_string(vi) + " has a stale edge reference");
      for (int j = i + 1; j < v.numEdges; ++j)
        if (e.other(vi) == edges_[v.edges[j]].other(vi))
          return fail("vertex " + std::to_string(vi) + " has a duplicate edge");
    }
  }
  for (int ei = 0; ei < edgeSlots(); ++ei) {
    const Edge& e = edges_[ei];
    if (!e.active) continue;
    ++ne;
    if (e.numTris() == 0) return fail("edge " + std::to_string(ei) + " has no triangle");
    if (e.tri[0] < 0) return fail("edge " + std::to_string(ei) + " has a gap in its triangle list");
    for (int t : e.tri) {
      if (t < 0) continue;
      const Triangle& tri = triangles_[t];
      if (!tri.active || std::find(tri.e.begin(), tri.e.end(), ei) == tri.e.end())
        return fail("edge " + std::to_string(ei) + " has a stale triangle reference");
    }
  }
  for (int ti = 0; ti < triangleSlots(); ++ti) {
    const Triangle& t = triangles_[ti];
    if (!t.active) continue;
    ++nt;
    for (int k = 0; k < 3; ++k) {
      int p = t.v[k], q = t.v[(k + 1) % 3];
      const Edge& e = edges_[t.e[k]];
      if (!((e.v[0] == p && e.v[1] == q) || (e.v[0] == q && e.v[1] == p)))
        return fail("triangle " + std::to_string(ti) + " edge " + std::to_string(k) + " does not match its vertices");
      const Vertex& v = vertices_[p];
      if (std::find(v.tris.begin(), v.tris.begin() + v.numTris, ti) == v.tris.begin() + v.numTris)
        return fail("triangle " + std::to_string(ti) + " missing from vertex " + std::to_string(p));
    }
  }
  if (nv != numVertices_ || ne != numEdges_ || nt != numTriangles_) return fail("element counters out of sync");
  return true;
}

}  // namespace bh

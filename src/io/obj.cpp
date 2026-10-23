#include "io/obj.h"

#include <array>
#include <cstdio>
#include <vector>

#include "io/sc1.h"

namespace bh {

namespace {

// One face corner: vertex slot and texvert (-1 = none).
struct Corner {
  int vertex, texvert;
};

// The faces of the mesh, quads (polygon list) joined into four corners in
// the winding of their first triangle.
std::vector<std::vector<Corner>> faces(const Mesh& m, const TextureMapping* uvs) {
  auto texvert = [&](int t, int k) {
    return uvs && t < int(uvs->corners.size()) ? uvs->corners[t][k] : -1;
  };
  std::vector<std::vector<Corner>> out;
  const bool quads = !m.quadMarkers.empty();
  for (int t = 0; t < m.triangleSlots(); ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active) continue;
    std::vector<Corner> f;
    for (int k = 0; k < 3; ++k) f.push_back({tri.v[k], texvert(t, k)});
    if (quads && t + 1 < m.triangleSlots() && t < int(m.quadMarkers.size()) && m.quadMarkers[t] == 1 &&
        m.triangle(t + 1).active) {
      // The partner shares an edge (x, y); its third corner goes between them.
      const Triangle& other = m.triangle(t + 1);
      for (int k = 0; k < 3; ++k) {
        const int x = tri.v[k], y = tri.v[(k + 1) % 3];
        for (int j = 0; j < 3; ++j) {
          if (other.v[j] != y || other.v[(j + 1) % 3] != x) continue;
          const int d = (j + 2) % 3;
          f.insert(f.begin() + k + 1, Corner{other.v[d], texvert(t + 1, d)});
          ++t;  // the partner is part of this face
          k = 3;
          break;
        }
      }
    }
    out.push_back(std::move(f));
  }
  return out;
}

}  // namespace

std::string meshToObj(const Mesh& m, const ObjExport& o) {
  std::string s;
  char line[160];
  auto add = [&](const char* fmt, auto... args) {
    std::snprintf(line, sizeof line, fmt, args...);
    s += line;
  };
  const bool mirror = m.symmetric;
  const bool uv = o.uvs && !o.uvs->empty();
  add("# OBJ exported from bildhauer\n# symmetry %d\n", mirror ? 1 : 0);
  add("usemtl (null)\ns 0\n");

  // OBJ indices (from 1): pass 1 numbers every active vertex, pass 2 the
  // mirror images of the vertices off the plane.
  std::vector<int> index(m.vertexSlots(), 0), mirrored(m.vertexSlots(), 0);
  int next = 1;
  auto vertex = [&](const Vec3& p, const Vec3& n) {
    const Vec3 q = p * o.scale + o.offset;
    add("v %.6f %.6f %.6f\n", q.x, q.y, q.z);
    if (o.normals) add("vn %.6f %.6f %.6f\n", n.x, n.y, n.z);
  };
  for (int v = 0; v < m.vertexSlots(); ++v) {
    const Vertex& vx = m.vertex(v);
    if (!vx.active) continue;
    index[v] = mirrored[v] = next++;
    vertex(vx.pos, vx.normal);
  }
  if (mirror)
    for (int v = 0; v < m.vertexSlots(); ++v) {
      const Vertex& vx = m.vertex(v);
      if (!vx.active || vx.onSymmetryPlane) continue;
      mirrored[v] = next++;
      vertex({-vx.pos.x, vx.pos.y, vx.pos.z}, {-vx.normal.x, vx.normal.y, vx.normal.z});
    }
  if (uv)
    for (const TexVert& t : o.uvs->texverts) add("vt %.6f %.6f\n", t.u, t.v);

  const auto list = faces(m, uv ? o.uvs : nullptr);
  auto face = [&](const std::vector<Corner>& f, const std::vector<int>& idx, bool reversed) {
    s += "f";
    for (size_t i = 0; i < f.size(); ++i) {
      const Corner& c = f[reversed ? f.size() - 1 - i : i];
      const int v = idx[c.vertex];
      if (uv && c.texvert >= 0 && o.normals) add(" %d/%d/%d", v, c.texvert + 1, v);
      else if (uv && c.texvert >= 0) add(" %d/%d", v, c.texvert + 1);
      else if (o.normals) add(" %d//%d", v, v);
      else add(" %d", v);
    }
    s += "\n";
  };
  for (const auto& f : list) face(f, index, false);
  if (mirror)
    for (const auto& f : list) face(f, mirrored, true);
  return s;
}

bool writeObj(const std::string& path, const Mesh& mesh, const ObjExport& options, std::string* error) {
  const std::string text = meshToObj(mesh, options);
  if (writeBinaryFile(path, std::vector<uint8_t>(text.begin(), text.end()))) return true;
  if (error) *error = "cannot write " + path;
  return false;
}

}  // namespace bh

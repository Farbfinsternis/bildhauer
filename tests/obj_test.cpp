// OBJ export (re/docs/obj.md) on every sample: the file is read back and
// checked for counts, the export transform, a closed and consistently
// oriented surface (both halves with symmetry), normals that line up with
// their vertices, texture coordinates of painted files; quads on a cube.
//
// Usage: obj_test <file.sc1>...

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "io/obj.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

namespace {

struct ObjFile {
  std::vector<Vec3> v;
  int vn = 0, vt = 0;
  struct Corner {
    int v, vt, vn;
  };
  std::vector<std::vector<Corner>> f;
};

ObjFile parse(const std::string& text) {
  ObjFile o;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string kind;
    ls >> kind;
    if (kind == "v") {
      Vec3 p;
      ls >> p.x >> p.y >> p.z;
      o.v.push_back(p);
    } else if (kind == "vn") {
      ++o.vn;
    } else if (kind == "vt") {
      ++o.vt;
    } else if (kind == "f") {
      std::vector<ObjFile::Corner> face;
      std::string c;
      while (ls >> c) {
        ObjFile::Corner k{0, 0, 0};
        const size_t a = c.find('/'), b = a == std::string::npos ? a : c.find('/', a + 1);
        k.v = std::stoi(c.substr(0, a));
        if (a != std::string::npos && b != a + 1) k.vt = std::stoi(c.substr(a + 1, b - a - 1));
        if (b != std::string::npos) k.vn = std::stoi(c.substr(b + 1));
        face.push_back(k);
      }
      o.f.push_back(face);
    }
  }
  return o;
}

// Every edge used by exactly two faces, once in each direction.
bool closedAndOriented(const ObjFile& o, int* badEdges) {
  std::map<std::pair<int, int>, int> directed;
  for (const auto& f : o.f)
    for (size_t i = 0; i < f.size(); ++i) ++directed[{f[i].v, f[(i + 1) % f.size()].v}];
  int bad = 0;
  for (const auto& [e, n] : directed)
    if (n != 1 || directed.count({e.second, e.first}) == 0 || directed.at({e.second, e.first}) != 1) ++bad;
  *badEdges = bad;
  return bad == 0;
}

void testFile(const std::string& path) {
  std::printf("%s\n", path.c_str());
  std::vector<uint8_t> data;
  Sc1File file;
  Mesh m;
  std::string err;
  if (!readBinaryFile(path, data) || !readSc1(data, file, &err) || !sc1ToMesh(file, m, &err)) {
    check(false, "load: " + err);
    return;
  }
  int active = 0, offPlane = 0, tris = 0, quads = 0;
  int first = -1;
  for (int v = 0; v < m.vertexSlots(); ++v)
    if (m.vertex(v).active) {
      ++active;
      offPlane += !m.vertex(v).onSymmetryPlane;
      if (first < 0) first = v;
    }
  for (int t = 0; t < m.triangleSlots(); ++t) tris += m.triangle(t).active;
  for (size_t t = 0; t < m.quadMarkers.size(); ++t) quads += m.quadMarkers[t] == 1;
  const int halves = m.symmetric ? 2 : 1;

  ObjExport opt;
  opt.scale = 0.5f;
  opt.offset = {1, 2, 3};
  ObjFile o = parse(meshToObj(m, opt));
  check(int(o.v.size()) == active + (m.symmetric ? offPlane : 0),
        "vertices: " + std::to_string(o.v.size()) + (m.symmetric ? " (with the mirrored half)" : ""));
  check(int(o.f.size()) == (tris - quads) * halves,
        "faces: " + std::to_string(o.f.size()) + (quads ? " (" + std::to_string(quads) + " quads per half)" : ""));
  const Vec3 expect = m.vertex(first).pos * 0.5f + Vec3{1, 2, 3};
  check(length(o.v[0] - expect) < 1e-4f, "positions: scale and offset applied");
  int outOfRange = 0;
  for (const auto& f : o.f)
    for (const auto& c : f) outOfRange += c.v < 1 || c.v > int(o.v.size());
  check(outOfRange == 0, "faces: indices in range");
  const Topology topo = fullTopology(m, m.symmetric);
  if (topo.open == 0) {
    int bad = 0;
    check(closedAndOriented(o, &bad), "surface closed and consistently oriented (" + std::to_string(bad) + " bad edges)");
  }

  opt.normals = true;
  ObjFile n = parse(meshToObj(m, opt));
  bool lined = n.vn == int(n.v.size());
  for (const auto& f : n.f)
    for (const auto& c : f) lined = lined && c.vn == c.v;
  check(lined, "normals: one per vertex, same index");

  if (file.hasTexturing) {
    Sc1Texturing tex;
    TextureMapping map;
    if (!decodeTexturing(file, tex, &err) || !texturingToMapping(tex, m, map, &err)) {
      check(false, "texturing: " + err);
      return;
    }
    opt.normals = false;
    opt.uvs = &map;
    ObjFile t = parse(meshToObj(m, opt));
    bool uvOk = t.vt == int(map.texverts.size());
    for (const auto& f : t.f)
      for (const auto& c : f) uvOk = uvOk && c.vt >= 1 && c.vt <= t.vt;
    check(uvOk, "texture coordinates: " + std::to_string(t.vt) + ", every corner has one");
  }
}

// A cube of 6 quads (polygon list), as in ops_test: each quad is written as
// one four-corner face in its own order.
void testQuadCube() {
  std::printf("quad cube\n");
  Mesh m;
  for (int i = 0; i < 8; ++i) m.addVertex({float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1)});
  const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (const auto& q : quads) {
    m.addTriangle(q[0], q[1], q[2]);
    m.addTriangle(q[2], q[3], q[0]);
    m.quadMarkers.push_back(1);
    m.quadMarkers.push_back(2);
  }
  m.updateGeometry();
  ObjFile o = parse(meshToObj(m, {}));
  bool same = o.f.size() == 6;
  for (size_t i = 0; same && i < 6; ++i)
    for (int k = 0; same && k < 4; ++k) same = o.f[i].size() == 4 && o.f[i][k].v == quads[i][k] + 1;
  check(same, "quads: six four-corner faces in the quads' own order");
  int bad = 0;
  check(closedAndOriented(o, &bad), "quads: closed and consistently oriented");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: obj_test <file.sc1>...\n");
    return 2;
  }
  testQuadCube();
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

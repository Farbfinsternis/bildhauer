// Validates the .sc1 reader/writer and the mesh kernel against real files:
//   - the file is read to the last byte
//   - version 8 files are written back byte-identical; older files survive a
//     write/read cycle with the same content
//   - the stored mesh builds without rejected triangles and passes validate()
//   - with symmetry: the mirrored full mesh is a valid 2-manifold whose seam is
//     closed; closed meshes have Euler characteristic 2
//   - all vertex normals are unit length
//
// Usage: sc1_test <file.sc1>...

#include <cmath>
#include <cstdio>
#include <string>

#include "core/mesh.h"
#include "core/symmetry.h"
#include "io/sc1.h"

using namespace bh;

namespace {

int failures = 0;

void check(bool ok, const std::string& file, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "%s: %s\n", file.c_str(), what.c_str());
  }
}

bool sameContent(const Sc1File& a, const Sc1File& b) {
  if (a.symmetry != b.symmetry || a.vertices.size() != b.vertices.size() || a.triangles.size() != b.triangles.size())
    return false;
  for (size_t i = 0; i < a.vertices.size(); ++i) {
    const Sc1Vertex &x = a.vertices[i], &y = b.vertices[i];
    if (!(x.pos == y.pos) || x.flags != y.flags || std::fabs(x.weight - y.weight) > 0.5f / 255) return false;
  }
  for (size_t i = 0; i < a.triangles.size(); ++i)
    if (a.triangles[i].v != b.triangles[i].v || a.triangles[i].flag != b.triangles[i].flag) return false;
  return a.polyList == b.polyList;
}

bool normalsAreUnit(const Mesh& m) {
  for (int i = 0; i < m.vertexSlots(); ++i) {
    const Vertex& v = m.vertex(i);
    if (v.active && v.numTris > 0 && std::fabs(length(v.normal) - 1.0f) > 1e-4f) return false;
  }
  return true;
}

void testFile(const std::string& path) {
  std::printf("%s\n", path.c_str());
  std::vector<uint8_t> data;
  if (!readBinaryFile(path, data)) return check(false, path, "open file");

  Sc1File file;
  std::string err;
  bool read = readSc1(data, file, &err);
  check(read, path, "read" + (read ? "" : ": " + err));
  if (!read) return;
  std::printf("  v%d ext=%d verts=%zu tris=%zu symmetry=%d quads=%s texturing=%d\n", file.version,
              file.hasExtension ? file.extVersion : -1, file.vertices.size(), file.triangles.size(),
              int(file.symmetry), file.hasPolyList ? "yes" : "no", int(file.hasTexturing));

  std::vector<uint8_t> written;
  bool wrote = writeSc1(file, written, &err);
  check(wrote, path, "write" + (wrote ? "" : ": " + err));
  if (wrote) {
    if (file.version == 8 && file.extVersion == 8) {
      check(written == data, path, "byte-identical round trip");
    } else {
      Sc1File again;
      check(readSc1(written, again, &err) && sameContent(file, again), path, "content survives upgrade to v8");
    }
  }

  Mesh mesh;
  bool built = sc1ToMesh(file, mesh, &err);
  check(built, path, "build mesh" + (built ? "" : ": " + err));
  if (!built) return;
  check(mesh.validate(&err), path, "mesh invariants" + (err.empty() ? "" : ": " + err));
  check(normalsAreUnit(mesh), path, "unit vertex normals");

  int open = 0, openOffPlane = 0;
  for (int i = 0; i < mesh.edgeSlots(); ++i) {
    const Edge& e = mesh.edge(i);
    if (!e.active || e.numTris() == 2) continue;
    ++open;
    if (!mesh.vertex(e.v[0]).onSymmetryPlane || !mesh.vertex(e.v[1]).onSymmetryPlane) ++openOffPlane;
  }
  int quads = 0;
  for (int i = 0; i < mesh.edgeSlots(); ++i) quads += mesh.edge(i).active && mesh.edge(i).quadDiagonal;
  std::printf("  stored mesh: %d verts, %d edges, %d tris, %d open edges, %d quads\n", mesh.numVertices(),
              mesh.numEdges(), mesh.numTriangles(), open, quads);

  Mesh closed;
  const Mesh* full = &mesh;
  if (file.symmetry) {
    bool mirrored = mirrorHalf(mesh, closed);
    check(mirrored, path, "mirror to full mesh");
    if (!mirrored) return;
    check(closed.validate(&err), path, "full mesh invariants" + (err.empty() ? "" : ": " + err));
    full = &closed;
  }
  int fullOpen = 0;
  for (int i = 0; i < full->edgeSlots(); ++i) fullOpen += full->edge(i).active && full->edge(i).numTris() != 2;
  int euler = full->numVertices() - full->numEdges() + full->numTriangles();
  std::printf("  full mesh: %d verts, %d edges, %d tris, euler %d\n", full->numVertices(), full->numEdges(),
              full->numTriangles(), euler);
  if (file.symmetry)
    check(fullOpen == 2 * openOffPlane, path, "seam closes (full boundary = 2 x off-plane boundary of the half)");
  if (fullOpen == 0) check(euler == 2, path, "closed mesh has Euler characteristic 2");
  else std::printf("  (open surface: %d boundary edges)\n", fullOpen);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: sc1_test <file.sc1>...\n");
    return 2;
  }
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

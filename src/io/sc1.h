#pragma once

// Sculptris .sc1 files, as described in re/docs/sc1-format.md. Reading
// accepts every known version; writing always produces version 8 (like
// Sculptris Alpha 6).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.h"
#include "core/vec3.h"

namespace bh {

struct Sc1Vertex {
  Vec3 pos;
  float weight = 1.0f;  // v >= 6: stored as round(weight * 255); older files: 1
  uint8_t flags = 0;    // bit 0: on the symmetry plane
};

struct Sc1Triangle {
  std::array<int32_t, 3> v{};
  uint8_t flag = 0;  // only stored from version 7 on
};

struct Sc1File {
  int version = 8;  // 0 = file without magic
  bool symmetry = false;
  std::vector<Sc1Vertex> vertices;
  std::vector<Sc1Triangle> triangles;

  bool hasExtension = false;  // false: file ended after the triangles
  int32_t extVersion = 8;
  float extraA = 1.0f;  // meaning open
  Vec3 extraVec;        // meaning open
  bool hasPolyList = false;
  std::vector<uint8_t> polyList;  // one byte per triangle; pairs triangles into quads
  uint8_t hasTexturing = 0;
  std::vector<uint8_t> texturing;  // raw texturing block; layout depends on extVersion
};

bool readSc1(const std::vector<uint8_t>& data, Sc1File& out, std::string* error);
bool writeSc1(const Sc1File& file, std::vector<uint8_t>& out, std::string* error);

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out);
bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data);

// Builds the stored mesh (with symmetry: only the +x half, as Sculptris keeps
// it in memory). Vertex and triangle slots equal the file indices. Quad
// diagonals from the polygon list are marked on the edges.
bool sc1ToMesh(const Sc1File& file, Mesh& mesh, std::string* error);

// Replaces the geometry in `file` with the mesh (active elements in slot
// order). Extension values are kept; paint data is dropped, since it no
// longer matches the topology.
void meshToSc1(const Mesh& mesh, Sc1File& file);

}  // namespace bh

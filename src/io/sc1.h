#pragma once

// Sculptris .sc1 files, as described in re/docs/sc1-format.md. Reading
// accepts every known version; writing always produces version 8 (like
// Sculptris Alpha 6).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.h"
#include "core/paint.h"
#include "core/texmap.h"
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

// The texturing block (paint mode) decoded; sc1-format.md "Texturing block".
struct Sc1Texturing {
  struct Patch {
    Vec3 uvOrigin, center;
    std::array<Vec3, 3> axes;
  };
  struct TexVert {
    float u = 0, v = 0;      // in [0, 1] of the texture
    int32_t packedNormal = 0;
    int32_t vertex = -1;
  };
  struct Image {
    int32_t w = 0, h = 0;
    std::vector<int32_t> pixels;  // color: 0xAABBGGRR; bump: heights, flat = 1 << 28
  };
  struct Material {
    int32_t slot = 0;
    std::array<char, 256> name{};
    std::array<uint8_t, 3> flags{};  // colored, additive, hidden (sc1 slot +0x249..+0x24B)
    int32_t w = 0, h = 0;
    std::vector<uint8_t> mask;
  };
  std::vector<Patch> patches;
  std::vector<TexVert> texverts;
  std::vector<std::array<int32_t, 3>> triTexverts;  // per file triangle
  std::vector<int32_t> triPatch;
  Image color, bump;
  std::vector<Material> materials;
};

// Decodes file.texturing (any extVersion). Fails on truncated data.
bool decodeTexturing(const Sc1File& file, Sc1Texturing& out, std::string* error);

// The stored UVs as a TextureMapping for a mesh built by sc1ToMesh (triangle
// slot = file triangle; patch = chart).
bool texturingToMapping(const Sc1Texturing& tex, const Mesh& mesh, TextureMapping& out, std::string* error);

// Puts the stored images and material slots into a canvas of the images'
// size.
bool texturingToCanvas(const Sc1Texturing& tex, Canvas& canvas, std::string* error);

// The texturing block in the layout of extension version 8.
void encodeTexturing(const Sc1Texturing& tex, std::vector<uint8_t>& out);

// Paint data of a mesh as meshToSc1 writes it: texverts refer to file
// vertices, triangles in file order, charts as patches, packed vertex
// normals.
void paintToTexturing(const Mesh& mesh, const TextureMapping& map, const Canvas& canvas, Sc1Texturing& out);

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
// The same with paint data.
void meshToSc1(const Mesh& mesh, Sc1File& file, const TextureMapping& map, const Canvas& canvas);

}  // namespace bh

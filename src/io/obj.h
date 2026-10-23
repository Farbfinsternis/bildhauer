#pragma once

// Wavefront OBJ export as Sculptris writes it (re/docs/obj.md): positions
// scaled and moved by the file's export transform, the mirrored half added
// with symmetry, texture coordinates in paint mode, optional vertex normals.
// Unlike Sculptris, normals always line up with their vertices.

#include <string>

#include "core/mesh.h"
#include "core/texmap.h"

namespace bh {

struct ObjExport {
  float scale = 1.0f;    // p' = scale * p + offset (.sc1 extraA, extraVec)
  Vec3 offset;
  bool normals = false;  // "Export vertex normals in OBJ"
  const TextureMapping* uvs = nullptr;  // paint mode: write vt and v/vt faces
};

std::string meshToObj(const Mesh& mesh, const ObjExport& options);
bool writeObj(const std::string& path, const Mesh& mesh, const ObjExport& options, std::string* error);

}  // namespace bh

#include "io/sc1.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <type_traits>

namespace bh {

namespace {

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}
  size_t pos() const { return p_; }
  void seek(size_t p) { p_ = p; }
  bool eof() const { return p_ >= d_.size(); }
  bool ok() const { return ok_; }
  size_t remaining() const { return p_ < d_.size() ? d_.size() - p_ : 0; }

  bool has(size_t n) {
    if (remaining() < n) ok_ = false;
    return ok_;
  }
  uint8_t u8() {
    if (!has(1)) return 0;
    return d_[p_++];
  }
  int32_t i32() {
    int32_t v = 0;
    if (has(4)) { std::memcpy(&v, &d_[p_], 4); p_ += 4; }
    return v;
  }
  float f32() {
    float v = 0;
    if (has(4)) { std::memcpy(&v, &d_[p_], 4); p_ += 4; }
    return v;
  }
  Vec3 vec3() {
    float x = f32(), y = f32(), z = f32();
    return {x, y, z};
  }
  std::vector<uint8_t> rest() {
    std::vector<uint8_t> r(d_.begin() + std::min(p_, d_.size()), d_.end());
    p_ = d_.size();
    return r;
  }

 private:
  const std::vector<uint8_t>& d_;
  size_t p_ = 0;
  bool ok_ = true;
};

class Writer {
 public:
  explicit Writer(std::vector<uint8_t>& out) : o_(out) {}
  void u8(uint8_t v) { o_.push_back(v); }
  void i32(int32_t v) { raw(&v, 4); }
  void f32(float v) { raw(&v, 4); }
  void vec3(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
  void raw(const void* p, size_t n) {
    auto b = static_cast<const uint8_t*>(p);
    o_.insert(o_.end(), b, b + n);
  }

 private:
  std::vector<uint8_t>& o_;
};

bool fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}

}  // namespace

bool readSc1(const std::vector<uint8_t>& data, Sc1File& out, std::string* error) {
  out = Sc1File{};
  Reader r(data);

  // Magic "SC1Fv00N". Without it, or for versions below 6, the file is read
  // from the start in the old layout.
  int version = 0;
  if (data.size() >= 8 && std::memcmp(data.data(), "SC1Fv", 5) == 0) {
    version = data[7] - '0';
    if (version > 8) return fail(error, "unsupported file version " + std::to_string(version));
    r.seek(version < 6 ? 0 : 8);
  }
  out.version = version;
  bool oldFormat = version < 6;

  int32_t numVerts = r.i32();
  int32_t numTris = r.i32();
  out.symmetry = r.u8() != 0;
  if (!r.ok() || numVerts < 0 || numTris < 0) return fail(error, "truncated header");
  size_t vertSize = oldFormat ? 17 : 14;
  size_t triSize = version >= 7 ? 13 : 12;
  if (r.remaining() < size_t(numVerts) * vertSize + size_t(numTris) * triSize)
    return fail(error, "file too short for its vertex/triangle counts");

  out.vertices.resize(numVerts);
  for (Sc1Vertex& v : out.vertices) {
    v.pos = r.vec3();
    // The old f32 field is not a usable mask weight (0 in the samples).
    v.weight = oldFormat ? (r.f32(), 1.0f) : r.u8() / 255.0f;
    v.flags = r.u8();
  }
  out.triangles.resize(numTris);
  for (Sc1Triangle& t : out.triangles) {
    for (int32_t& i : t.v) {
      i = r.i32();
      if (i < 0 || i >= numVerts) return fail(error, "triangle references vertex " + std::to_string(i));
    }
    if (version >= 7) t.flag = r.u8();
  }

  if (r.eof()) return true;  // old file without extensions
  out.hasExtension = true;
  out.extVersion = r.i32();
  if (out.extVersion >= 3) {
    out.extraA = r.f32();
    out.extraVec = r.vec3();
  }
  if (out.extVersion == 6) r.u8();
  if (out.extVersion >= 7) {
    out.hasPolyList = r.u8() != 0;
    if (out.hasPolyList) {
      int32_t count = r.i32();
      if (count < 0 || !r.has(size_t(count))) return fail(error, "truncated polygon list");
      out.polyList.resize(count);
      for (uint8_t& b : out.polyList) b = r.u8();
    }
  }
  out.hasTexturing = r.u8();
  if (!r.ok()) return fail(error, "truncated extension block");
  if (out.hasTexturing) out.texturing = r.rest();
  else if (!r.eof()) return fail(error, std::to_string(r.remaining()) + " trailing bytes");
  return true;
}

bool decodeTexturing(const Sc1File& f, Sc1Texturing& out, std::string* error) {
  out = Sc1Texturing{};
  if (!f.hasTexturing) return fail(error, "no texturing block");
  Reader r(f.texturing);
  const int ev = f.extVersion;
  // ReadBufferRLE: literal and run segments alternate, each led by a u8
  // count; a run repeats the last literal element.
  auto rle = [&](auto& dst, size_t count, bool wide) {
    dst.resize(count);
    size_t i = 0;
    int32_t last = 0;
    bool literal = true;
    while (i < count && r.ok() && !r.eof()) {
      int n = r.u8();
      for (int k = 0; k < n && i < count; ++k) {
        if (literal) last = wide ? r.i32() : r.u8();
        dst[i++] = static_cast<typename std::decay_t<decltype(dst)>::value_type>(last);
      }
      literal = !literal;
    }
    return i == count;
  };

  int32_t numPatches = r.i32();
  if (numPatches < 0 || !r.has(size_t(numPatches) * 60)) return fail(error, "truncated patches");
  out.patches.resize(numPatches);
  for (auto& p : out.patches) {
    p.uvOrigin = r.vec3();
    p.center = r.vec3();
    for (Vec3& a : p.axes) a = r.vec3();
  }
  int32_t numTexverts = r.i32();
  if (numTexverts < 0 || !r.has(size_t(numTexverts) * (ev >= 6 ? 16 : 28))) return fail(error, "truncated texverts");
  out.texverts.resize(numTexverts);
  for (auto& tv : out.texverts) {
    if (ev >= 6) {
      tv.u = r.f32();
      tv.v = r.f32();
      tv.packedNormal = r.i32();
    } else {
      Vec3 uvw = r.vec3();
      r.vec3();
      tv.u = uvw.x;
      tv.v = uvw.y;
    }
    tv.vertex = r.i32();
  }
  const size_t numTris = f.triangles.size();
  out.triTexverts.resize(numTris);
  out.triPatch.resize(numTris);
  for (size_t t = 0; t < numTris; ++t) {
    for (int32_t& i : out.triTexverts[t]) i = r.i32();
    if (ev >= 6) {
      uint8_t lo = r.u8(), hi = r.u8();
      out.triPatch[t] = lo | (hi << 8);
    } else {
      out.triPatch[t] = r.i32();
    }
  }
  for (Sc1Texturing::Image* im : {&out.color, &out.bump}) {
    im->w = r.i32();
    im->h = r.i32();
    if (!r.ok() || im->w < 0 || im->h < 0 || int64_t(im->w) * im->h > (1 << 26)) return fail(error, "bad image size");
    size_t n = size_t(im->w) * im->h;
    if (ev < 4) {
      im->pixels.resize(n);
      for (int32_t& px : im->pixels) px = r.i32();
    } else if (!rle(im->pixels, n, true)) {
      return fail(error, "truncated image");
    }
  }
  if (ev >= 8) {
    int32_t numMaterials = r.i32();
    if (numMaterials < 0 || numMaterials > 16) return fail(error, "bad material count");
    out.materials.resize(numMaterials);
    for (auto& m : out.materials) {
      m.slot = r.i32();
      for (char& c : m.name) c = char(r.u8());
      for (uint8_t& b : m.flags) b = r.u8();
      m.w = r.i32();
      m.h = r.i32();
      if (!r.ok() || m.w < 0 || m.h < 0 || int64_t(m.w) * m.h > (1 << 26)) return fail(error, "bad material mask size");
      if (!rle(m.mask, size_t(m.w) * m.h, false)) return fail(error, "truncated material mask");
    }
  }
  if (!r.ok()) return fail(error, "truncated texturing block");
  for (size_t t = 0; t < numTris; ++t)
    for (int32_t i : out.triTexverts[t])
      if (i < 0 || i >= numTexverts) return fail(error, "triangle " + std::to_string(t) + " references texvert " + std::to_string(i));
  for (const auto& tv : out.texverts)
    if (tv.vertex < 0 || tv.vertex >= int32_t(f.vertices.size())) return fail(error, "texvert references a missing vertex");
  return true;
}

bool texturingToMapping(const Sc1Texturing& tex, const Mesh& mesh, TextureMapping& out, std::string* error) {
  out = TextureMapping{};
  const int nt = int(tex.triTexverts.size());
  if (nt > mesh.triangleSlots()) return fail(error, "texturing has more triangles than the mesh");
  out.size = tex.color.w;
  out.texverts.reserve(tex.texverts.size());
  for (const auto& tv : tex.texverts) out.texverts.push_back({tv.u, tv.v, tv.vertex});
  out.corners.assign(mesh.triangleSlots(), {-1, -1, -1});
  out.chart.assign(mesh.triangleSlots(), -1);
  for (int t = 0; t < nt; ++t) {
    if (!mesh.triangle(t).active) continue;
    for (int k = 0; k < 3; ++k) out.corners[t][k] = tex.triTexverts[t][k];
    out.chart[t] = tex.triPatch[t];
    out.numCharts = std::max(out.numCharts, tex.triPatch[t] + 1);
  }
  return true;
}

bool writeSc1(const Sc1File& f, std::vector<uint8_t>& out, std::string* error) {
  if (f.hasTexturing && f.extVersion != 8)
    return fail(error, "texturing block of extension version " + std::to_string(f.extVersion) +
                           " cannot be written as version 8");
  out.clear();
  Writer w(out);
  w.raw("SC1Fv008", 8);
  w.i32(int32_t(f.vertices.size()));
  w.i32(int32_t(f.triangles.size()));
  w.u8(f.symmetry ? 1 : 0);
  for (const Sc1Vertex& v : f.vertices) {
    w.vec3(v.pos);
    float c = std::fmin(std::fmax(v.weight, 0.0f), 1.0f);
    w.u8(uint8_t(std::lround(c * 255.0f)));
    w.u8(v.flags);
  }
  for (const Sc1Triangle& t : f.triangles) {
    for (int32_t i : t.v) w.i32(i);
    w.u8(t.flag);
  }
  w.i32(8);
  w.f32(f.extraA);
  w.vec3(f.extraVec);
  w.u8(f.hasPolyList ? 1 : 0);
  if (f.hasPolyList) {
    w.i32(int32_t(f.polyList.size()));
    w.raw(f.polyList.data(), f.polyList.size());
  }
  w.u8(f.hasTexturing);
  if (f.hasTexturing) w.raw(f.texturing.data(), f.texturing.size());
  return true;
}

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
  std::ofstream o(path, std::ios::binary);
  o.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
  return bool(o);
}

bool sc1ToMesh(const Sc1File& f, Mesh& mesh, std::string* error) {
  mesh = Mesh{};
  mesh.symmetric = f.symmetry;
  for (const Sc1Vertex& v : f.vertices) {
    int i = mesh.addVertex(v.pos);
    Vertex& mv = mesh.vertex(i);
    mv.weight = v.weight;
    mv.onSymmetryPlane = (v.flags & 1) != 0;
  }
  for (size_t i = 0; i < f.triangles.size(); ++i) {
    const Sc1Triangle& t = f.triangles[i];
    int ti = mesh.addTriangle(t.v[0], t.v[1], t.v[2]);
    if (ti < 0) return fail(error, "triangle " + std::to_string(i) + " could not be added");
    if (t.flag) {
      mesh.triangle(ti).fileFlag = true;
      for (int v : t.v) mesh.vertex(v).hidden = true;
    }
  }
  // A quad is a triangle marked 1 followed by one marked 2; T1 = (v0, v1, v2),
  // T2 = (v2, q, v0), and the shared diagonal is T1's edge e2.
  if (f.hasPolyList) {
    size_t n = std::min(f.polyList.size(), f.triangles.size());
    mesh.quadMarkers.assign(f.triangles.size(), 0);
    for (size_t i = 0; i + 1 < n; ++i) {
      if (f.polyList[i] != 1 || f.polyList[i + 1] != 2) continue;
      int diag = mesh.triangle(int(i)).e[2];
      const Edge& e = mesh.edge(diag);
      if (e.tri[0] != int(i + 1) && e.tri[1] != int(i + 1)) continue;
      mesh.edge(diag).quadDiagonal = true;
      mesh.quadMarkers[i] = 1;
      mesh.quadMarkers[i + 1] = 2;
    }
  }
  mesh.updateGeometry();
  mesh.storeReference();
  mesh.clearDirty();
  return true;
}

void meshToSc1(const Mesh& mesh, Sc1File& f) {
  f.version = 8;
  f.symmetry = mesh.symmetric;
  f.vertices.clear();
  f.triangles.clear();
  std::vector<int32_t> index(mesh.vertexSlots(), -1);
  for (int v = 0; v < mesh.vertexSlots(); ++v) {
    const Vertex& vx = mesh.vertex(v);
    if (!vx.active) continue;
    index[v] = int32_t(f.vertices.size());
    f.vertices.push_back({vx.pos, vx.weight, uint8_t(vx.onSymmetryPlane ? 1 : 0)});
  }
  std::vector<uint8_t> polyList;
  const bool quads = !mesh.quadMarkers.empty();
  for (int t = 0; t < mesh.triangleSlots(); ++t) {
    const Triangle& tri = mesh.triangle(t);
    if (!tri.active) continue;
    f.triangles.push_back({{index[tri.v[0]], index[tri.v[1]], index[tri.v[2]]}, uint8_t(tri.fileFlag ? 1 : 0)});
    if (quads) polyList.push_back(t < int(mesh.quadMarkers.size()) ? mesh.quadMarkers[t] : 0);
  }
  f.hasExtension = true;
  f.extVersion = 8;
  f.hasPolyList = quads;
  f.polyList = std::move(polyList);
  f.hasTexturing = 0;
  f.texturing.clear();
}

}  // namespace bh

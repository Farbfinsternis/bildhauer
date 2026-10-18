// Texture mapping: bildhauer's own atlas on every sample (all triangles
// mapped, UVs in range, no flipped triangles, no overlapping charts, a usable
// share of the texture), and the stored UVs of painted files.
//
// Usage: texmap_test <file.sc1>...

#include <chrono>
#include <cstdio>
#include <string>

#include "core/subdivide.h"
#include "core/texmap.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

namespace {

void report(const char* what, const AtlasStats& st) {
  std::printf("  %s: %d charts, coverage %.0f%%, density spread x%.2f, %d unmapped, %d out of range, %d flipped, %d overlaps\n",
              what, st.charts, st.coverage * 100, st.maxStretch, st.unmapped, st.outOfRange, st.flipped, st.overlaps);
}

void checkAtlas(const Mesh& m, const char* name) {
  auto t0 = std::chrono::steady_clock::now();
  TextureMapping map = buildAtlas(m);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  AtlasStats st = atlasStats(m, map);
  report(name, st);
  std::printf("    %d triangles in %.1f ms\n", m.numTriangles(), ms);
  const std::string n = name;
  check(st.unmapped == 0, n + ": every triangle mapped");
  check(st.outOfRange == 0, n + ": UVs in [0, 1]");
  check(st.flipped == 0, n + ": no flipped triangles");
  check(st.overlaps == 0, n + ": charts do not overlap");
  check(st.coverage > 0.2f, n + ": covers a usable share of the texture");
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
  checkAtlas(m, "atlas");
  Mesh fine = m;
  subdivide(fine, {true, fine.symmetric});
  checkAtlas(fine, "atlas after subdivision");

  if (file.hasTexturing) {
    Sc1Texturing tex;
    TextureMapping map;
    bool ok = decodeTexturing(file, tex, &err) && texturingToMapping(tex, m, map, &err);
    check(ok, "stored texturing decodes" + (err.empty() ? "" : " (" + err + ")"));
    if (!ok) return;
    AtlasStats st = atlasStats(m, map);
    report("stored UVs", st);
    check(st.unmapped == 0 && st.outOfRange == 0, "stored UVs: every triangle mapped, in range");
    check(tex.color.w > 0 && int(tex.color.pixels.size()) == tex.color.w * tex.color.h &&
              int(tex.bump.pixels.size()) == tex.bump.w * tex.bump.h,
          "stored images complete");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: texmap_test <file.sc1>...\n");
    return 2;
  }
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

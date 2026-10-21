// Paint mode: texel baking and the range query on every sample, then color,
// bump and flatten dabs, normals, undo and symmetry against the formulas of
// re/docs/paint.md 3; stored images of painted files load unchanged.
//
// Usage: paint_test <file.sc1>...

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#include "core/paint.h"
#include "core/texmap.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

namespace {

constexpr int kSize = 512;

uint8_t channel(uint32_t c, int k) { return uint8_t(c >> (8 * k) & 0xFF); }

bool sameColor(uint32_t a, uint32_t b, int tolerance) {
  for (int k = 0; k < 3; ++k)
    if (std::abs(int(channel(a, k)) - int(channel(b, k))) > tolerance) return false;
  return true;
}

// A covered texel whose surroundings within `radius` are covered texels on
// one side of the model (stored half, away from the symmetry plane).
int centerTexel(const Mesh& m, const Texels& tx, float radius) {
  int best = -1;
  float bestScore = -1;
  for (size_t i = 0; i < tx.triangle.size(); i += 97) {
    if (tx.triangle[i] < 0) continue;
    const Vec3& p = tx.pos[i];
    if (m.symmetric && p.x < 2 * radius) continue;
    float score = tx.normal[i].z;  // prefer a front-facing spot
    if (score > bestScore) {
      bestScore = score;
      best = int(i);
    }
  }
  return best;
}

void testBake(const Mesh& m, const TextureMapping& map, const Canvas& canvas) {
  const Texels& tx = canvas.texels();
  AtlasStats st = atlasStats(m, map);
  std::printf("  %d covered texels (%.0f%% of the texture, UV coverage %.0f%%)\n", tx.covered,
              100.0 * tx.covered / (kSize * kSize), st.coverage * 100);
  check(tx.covered >= st.coverage * kSize * kSize, "bake: covers at least the UV area");
  int badNormal = 0, offTriangle = 0;
  for (size_t i = 0; i < tx.triangle.size(); ++i) {
    int t = tx.triangle[i];
    if (t < 0) continue;
    if (std::fabs(length(tx.normal[i]) - 1) > 1e-3f) ++badNormal;
    const Triangle& tri = m.triangle(t);
    if (length(tx.pos[i] - tri.centroid) > tri.radius * 1.001f + 1e-6f) ++offTriangle;
  }
  check(badNormal == 0, "bake: unit normals");
  check(offTriangle == 0, "bake: positions lie on their triangle");
}

void testGrid(const Mesh& m, const Texels& tx) {
  TexelGrid grid;
  grid.build(tx);
  Vec3 center;
  const float diag = length(bboxDiagonal(m, &center));
  int mismatches = 0, total = 0;
  for (int q = 0; q < 12; ++q) {
    const int i = int((size_t(q) * 7919 * 104729) % tx.triangle.size());
    const Vec3 c = tx.triangle[i] >= 0 ? tx.pos[i] : center;
    const float r = diag * (0.005f + 0.03f * q);
    std::vector<int> found;
    grid.query(c, r, found);
    std::sort(found.begin(), found.end());
    std::vector<int> brute;
    for (size_t k = 0; k < tx.triangle.size(); ++k)
      if (tx.triangle[k] >= 0 && length(tx.pos[k] - c) <= r) brute.push_back(int(k));
    total += int(brute.size());
    // Float rounding at the boundary may differ by a texel or two.
    std::vector<int> diff;
    std::set_symmetric_difference(found.begin(), found.end(), brute.begin(), brute.end(), std::back_inserter(diff));
    mismatches += int(diff.size());
  }
  check(mismatches <= total / 1000 + 2, "grid: query matches brute force (" + std::to_string(mismatches) + " of " +
                                            std::to_string(total) + " differ)");
}

void testDabs(const Mesh& m, const TextureMapping& map) {
  Canvas canvas(m, map, kSize, kSize);
  const Texels& tx = canvas.texels();
  Vec3 center;
  const float diag = length(bboxDiagonal(m, &center));
  PaintSettings s;
  s.radius = diag * 0.04f;
  s.strength = 1.97f;  // k = 1
  s.hardness = 1.0f;   // H = 0.95: full strength out to x = 0.05
  const int c = centerTexel(m, tx, s.radius * 1.3f);
  if (c < 0) {
    check(false, "dabs: found a spot to paint");
    return;
  }
  const Vec3 p = tx.pos[c];
  const std::vector<uint32_t> before = canvas.colorImage();
  const std::vector<int32_t> heightsBefore = canvas.heights();

  canvas.beginUndoStep();
  canvas.dab(p, s);
  check(sameColor(canvas.colorImage()[c], s.primary, 1), "color: full strength reaches the primary color");
  int outside = 0, inside = 0, changed = 0;
  for (size_t i = 0; i < tx.triangle.size(); ++i) {
    if (tx.triangle[i] < 0) continue;
    float d = length(tx.pos[i] - p);
    if (m.symmetric) d = std::min(d, length(Vec3{-tx.pos[i].x, tx.pos[i].y, tx.pos[i].z} - p));
    bool differs = canvas.colorImage()[i] != before[i];
    changed += differs;
    if (d > 1.3f * s.radius * 1.01f && differs) ++outside;
    if (d < 0.05f * 1.5f * s.radius && !sameColor(canvas.colorImage()[i], s.primary, 1)) ++inside;
  }
  check(changed > 10, "color: the dab changes " + std::to_string(changed) + " texels");
  check(outside == 0, "color: nothing changes beyond 1.3 r");
  check(inside == 0, "color: the hard core is fully painted");

  PaintSettings inv = s;
  inv.strength = -s.strength;
  canvas.dab(p, inv);
  check(sameColor(canvas.colorImage()[c], s.secondary, 1), "color: inverted paints the secondary color");

  // Half strength, soft: a step of about |k| f toward the target.
  PaintSettings half = s;
  half.strength = 0.5f / 0.6f;  // k = 0.5
  const uint32_t from = canvas.colorImage()[c];
  canvas.dab(p, half);
  const uint32_t to = canvas.colorImage()[c];
  bool halfway = true;
  for (int k = 0; k < 3; ++k) {
    float expect = channel(from, k) + (channel(s.primary, k) - channel(from, k)) * 0.5f;
    if (std::fabs(channel(to, k) - expect) > 1.5f) halfway = false;
  }
  check(halfway, "color: k = 0.5 goes half the way");

  // Bump: about 24480 k f per dab, negative k lowers; flatten moves toward 2^28.
  PaintSettings bump = s;
  bump.tool = PaintTool::Bump;
  canvas.dab(p, bump);
  const int64_t raised = int64_t(canvas.heights()[c]) - kNeutralHeight;
  check(raised == 24576 * 255 / 256, "bump: one full dab raises by 24480 (" + std::to_string(raised) + ")");
  canvas.dab(p, bump);
  canvas.dab(p, bump);
  bump.strength = -bump.strength;
  canvas.dab(p, bump);
  check(int64_t(canvas.heights()[c]) - kNeutralHeight == 2 * (24576 * 255 / 256), "bump: inverted lowers");
  canvas.takeChangedTiles();
  int tilted = 0, flatFar = 1;
  for (size_t i = 0; i < tx.triangle.size(); ++i) {
    const uint8_t* n = &canvas.normalImage()[i * 3];
    bool flat = n[0] == 128 && n[1] == 128 && n[2] == 255;
    if (canvas.heights()[i] != kNeutralHeight && !flat) ++tilted;
    if (tx.triangle[i] >= 0 && length(tx.pos[i] - p) > 3 * s.radius && !m.symmetric && !flat) flatFar = 0;
  }
  check(tilted > 0, "normals: the bump tilts " + std::to_string(tilted) + " normals");
  check(flatFar == 1, "normals: flat away from the bump");

  PaintSettings flatten = s;
  flatten.tool = PaintTool::FlattenBump;
  const int64_t h0 = int64_t(canvas.heights()[c]) - kNeutralHeight;
  canvas.dab(p, flatten);
  const int64_t h1 = int64_t(canvas.heights()[c]) - kNeutralHeight;
  check(h1 == h0 + ((-255 * h0) >> 8), "flatten: moves 255/256 of the way to neutral");

  // Undo restores the start state exactly (one step for all dabs above).
  canvas.takeChangedTiles();
  check(canvas.undo(), "undo: a step to undo");
  std::vector<int> tiles = canvas.takeChangedTiles();
  check(canvas.colorImage() == before && canvas.heights() == heightsBefore, "undo: images restored exactly");
  check(!tiles.empty(), "undo: restored tiles are reported");
  check(!canvas.undo(), "undo: nothing left");

  // A stroke paints along its path.
  PaintStroke stroke(canvas, s, p);
  Vec3 dir = normalize(cross(tx.normal[c], {0, 1, 0}));
  if (length(dir) == 0) dir = {1, 0, 0};
  int along = 0;
  for (int k = 1; k <= 4; ++k) stroke.moveTo(p + dir * (s.radius * 0.5f * k), true);
  for (size_t i = 0; i < tx.triangle.size(); ++i) along += canvas.colorImage()[i] != before[i];
  check(along > changed, "stroke: paints more than one dab (" + std::to_string(along) + " texels)");

  if (m.symmetric) {
    // One texture for both halves: a dab on the mirror image paints the same.
    Canvas a(m, map, kSize, kSize), b(m, map, kSize, kSize);
    a.dab(p, s);
    b.dab({-p.x, p.y, p.z}, s);
    check(a.colorImage() == b.colorImage(), "symmetry: a mirrored dab paints the same texels");
    // A dab on the plane reaches both sides evenly.
    Vec3 onPlane;
    for (size_t i = 0; i < tx.triangle.size(); ++i)
      if (tx.triangle[i] >= 0 && std::fabs(tx.pos[i].x) < s.radius * 0.05f && tx.normal[i].z > 0.5f) {
        onPlane = tx.pos[i];
        break;
      }
    Canvas plane(m, map, kSize, kSize);
    plane.dab(onPlane, s);
    int painted = 0;
    for (size_t i = 0; i < tx.triangle.size(); ++i) painted += plane.colorImage()[i] != before[i];
    check(painted > 0, "symmetry: a dab on the plane paints");
  }
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
  TextureMapping map = buildAtlas(m);
  auto t0 = std::chrono::steady_clock::now();
  Canvas canvas(m, map, kSize, kSize);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  canvas %dx%d in %.1f ms\n", kSize, kSize, ms);
  testBake(m, map, canvas);
  testGrid(m, canvas.texels());
  testDabs(m, map);

  if (file.hasTexturing) {
    Sc1Texturing tex;
    TextureMapping stored;
    if (!decodeTexturing(file, tex, &err) || !texturingToMapping(tex, m, stored, &err)) {
      check(false, "stored texturing decodes: " + err);
      return;
    }
    Canvas painted(m, stored, tex.color.w, tex.color.h);
    bool ok = tex.bump.w == tex.color.w && tex.bump.h == tex.color.h &&
              painted.loadImages(tex.color.pixels, tex.bump.pixels, &err);
    check(ok, "stored images load" + (err.empty() ? "" : " (" + err + ")"));
    if (!ok) return;
    std::printf("  stored canvas %dx%d, %d covered texels\n", painted.width(), painted.height(),
                painted.texels().covered);
    bool same = true;
    for (size_t i = 0; i < tex.color.pixels.size(); ++i)
      if (painted.colorImage()[i] != uint32_t(tex.color.pixels[i])) same = false;
    check(same && painted.heights() == tex.bump.pixels, "stored images kept unchanged");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: paint_test <file.sc1>...\n");
    return 2;
  }
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

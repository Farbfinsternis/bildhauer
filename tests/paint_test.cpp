// Paint mode: texel baking and the range query on every sample, then color,
// bump and flatten dabs, normals, undo and symmetry against the formulas of
// re/docs/paint.md 3; stamps (brush mask and texture); Fill/Clear; material masks; tangents; saving paint data (a stored
// texturing block re-encodes byte-identically, painted canvases survive a
// save and load); stored images of painted files load unchanged.
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
  s.toViewer = tx.normal[c];  // the view looks straight at the spot
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
    PaintSettings mirrored = s;
    mirrored.toViewer.x = -mirrored.toViewer.x;
    a.dab(p, s);
    b.dab({-p.x, p.y, p.z}, mirrored);
    check(a.colorImage() == b.colorImage(), "symmetry: a mirrored dab paints the same texels");
    // A dab on the plane reaches both sides evenly.
    Vec3 onPlane, onPlaneNormal{0, 0, 1};
    for (size_t i = 0; i < tx.triangle.size(); ++i)
      if (tx.triangle[i] >= 0 && std::fabs(tx.pos[i].x) < s.radius * 0.05f) {
        onPlane = tx.pos[i];
        onPlaneNormal = tx.normal[i];
        break;
      }
    Canvas plane(m, map, kSize, kSize);
    PaintSettings front = s;
    front.toViewer = onPlaneNormal;
    plane.dab(onPlane, front);
    int painted = 0;
    for (size_t i = 0; i < tx.triangle.size(); ++i) painted += plane.colorImage()[i] != before[i];
    check(painted > 0, "symmetry: a dab on the plane paints");
  }
}

void testMasks(const Mesh& m, const TextureMapping& map) {
  Canvas canvas(m, map, kSize, kSize);
  const Texels& tx = canvas.texels();
  Vec3 center;
  const float diag = length(bboxDiagonal(m, &center));
  PaintSettings s;
  s.radius = diag * 0.04f;
  s.strength = 1.97f;
  s.hardness = 1.0f;
  const int c = centerTexel(m, tx, s.radius * 1.3f);
  if (c < 0) return;
  s.toViewer = tx.normal[c];
  check(canvas.material(0).used && canvas.material(0).mask[c] == 255, "masks: slot 0 covers everything");
  const int slot = canvas.addLayer("layer");
  check(slot == 1 && canvas.material(1).mask[c] == 0, "masks: a new layer starts empty");
  canvas.takeChangedMasks();
  const std::vector<uint32_t> color = canvas.colorImage();
  s.target = slot;
  canvas.beginUndoStep();
  canvas.dab(tx.pos[c], s);
  check(canvas.material(slot).mask[c] == 255 && canvas.colorImage() == color,
        "masks: Paint color with a target paints the mask, not the color");
  check(canvas.takeChangedMasks() == 1u << slot, "masks: the changed mask is reported");
  s.strength = -s.strength;
  canvas.dab(tx.pos[c], s);
  check(canvas.material(slot).mask[c] == 0, "masks: inverted erases the mask");
  canvas.undo();
  check(canvas.material(slot).mask[c] == 0 && canvas.takeChangedMasks() == 1u << slot,
        "masks: undo restores the mask");
}

void testStampBasics() {
  std::printf("stamps\n");
  const Stamp quad{2, 2, {0xFF000000u, 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u}};
  check(quad.sample(0, 0) == 0xFF000000u && quad.sample(1, 0) == 0xFF0000FFu && quad.sample(0, 1) == 0xFF00FF00u &&
            quad.sample(1, 1) == 0xFFFF0000u,
        "stamp: corners sample their pixels (row 0 at v = 0)");
  check(quad.sample(0.5f, 0.5f) == 0xFF404040u, "stamp: the center is the bilinear mean");
  check(quad.sample(-1, 2) == quad.sample(0, 1), "stamp: u and v are clamped");

  const Stamp gray = grayStamp({1, 1, {0x80FF8040u}});  // A 128, B 255, G 128, R 64
  const uint32_t g = (2 * 64 + 3 * 128 + 255) / 6;
  check(gray.rgba[0] == ((((128 + 1) * g) >> 8) << 24 | 0xFFFFFFu), "stamp: gray = (2R + 3G + B) / 6, times alpha");

  // A mask that is opaque on the left half only.
  Stamp mask{4, 1, {0xFFFFFFFFu, 0xFFFFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu}};
  Stamps st = buildStamps(nullptr, &mask, false);
  check(st.color.width == 4 && (st.color.rgba[0] >> 24) == 255 && (st.color.rgba[3] >> 24) == 0 &&
            (st.color.rgba[0] & 0xFFFFFF) == 0xFFFFFF,
        "stamps: without a texture white, shaped by the mask");
  Stamps inv = buildStamps(nullptr, &mask, true);
  check((inv.color.rgba[0] >> 24) == 0 && (inv.color.rgba[3] >> 24) == 255, "stamps: an inverted mask");
  const Stamp texture{1, 1, {0xFF20C040u}};
  Stamps tex = buildStamps(&texture, nullptr, false);
  check(tex.color.rgba[0] == 0xFF20C040u && (tex.bump.rgba[0] & 0xFFFFFF) == 0xFFFFFF,
        "stamps: the texture as is for color, gray for bump");

  const StampFrame f = fixedStampFrame({0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0);
  check(length(f.a - Vec3{1, 0, 0}) < 1e-6f && length(f.b - Vec3{0, -1, 0}) < 1e-6f && f.n == Vec3(0, 0, 1),
        "frame: at angle 0 the stamp stands upright in the screen plane");
  const StampFrame q = fixedStampFrame({0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 3.14159265f / 2);
  check(length(q.b - Vec3{1, 0, 0}) < 1e-5f, "frame: the angle turns it");
  const StampFrame d = directedStampFrame({0, 0, 1}, {3, 4, 5});
  check(length(d.b - Vec3{0.6f, 0.8f, 0}) < 1e-5f && std::fabs(dot(d.a, d.b)) < 1e-6f && std::fabs(dot(d.a, d.n)) < 1e-6f,
        "frame: a direction is projected into the screen plane");
}

void testStampDabs(const Mesh& m, const TextureMapping& map) {
  Vec3 center;
  const float diag = length(bboxDiagonal(m, &center));
  Canvas probe(m, map, kSize, kSize);
  const Texels& tx = probe.texels();
  PaintSettings s;
  s.radius = diag * 0.04f;
  s.strength = 1.97f;
  s.hardness = 1.0f;
  const int c = centerTexel(m, tx, s.radius * 1.3f);
  if (c < 0) return;
  const Vec3 p = tx.pos[c];
  s.toViewer = tx.normal[c];
  s.frame = directedStampFrame(s.toViewer, cross(s.toViewer, {0, 1, 0}).x != 0 ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
  const std::vector<uint32_t> before = probe.colorImage();
  auto changed = [&](const Canvas& cv) {
    int n = 0;
    for (size_t i = 0; i < before.size(); ++i) n += cv.colorImage()[i] != before[i];
    return n;
  };

  // A white stamp paints the full color at the center.
  const Stamps white = buildStamps(nullptr, nullptr, false);
  s.stamps = &white;
  Canvas a(m, map, kSize, kSize);
  a.dab(p, s);
  check(sameColor(a.colorImage()[c], s.primary, 1) && changed(a) > 10, "stamp dab: a white stamp paints like a full dab");

  // A mask opaque on one side (u < 0.5) leaves the other side alone.
  Stamp halfMask{8, 1, {}};
  for (int x = 0; x < 8; ++x) halfMask.rgba.push_back(x < 4 ? 0xFFFFFFFFu : 0x00FFFFFFu);
  const Stamps half = buildStamps(nullptr, &halfMask, false);
  s.stamps = &half;
  Canvas b(m, map, kSize, kSize);
  b.dab(p, s);
  int left = 0, right = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    if (b.colorImage()[i] == before[i]) continue;
    const float u = dot(tx.pos[i] - p, s.frame.a) / (3 * s.radius);  // 0 at the center
    if (u < -0.3f) ++left;
    if (u > 0.3f) ++right;  // beyond the blend of the mask edge
  }
  check(left > 10 && right == 0, "stamp dab: the mask keeps one side unpainted (" + std::to_string(left) + " / " +
                                     std::to_string(right) + ")");

  // Brush texture: its colors are painted.
  const Stamp green{1, 1, {0xFF20C040u}};
  const Stamps tex = buildStamps(&green, nullptr, false);
  s.stamps = &tex;
  s.stampTexture = true;
  Canvas t(m, map, kSize, kSize);
  t.dab(p, s);
  check(sameColor(t.colorImage()[c], 0x20C040u, 1), "stamp dab: the brush texture's color is painted");
  s.stampTexture = false;

  // Bump: the stamp's gray value scales the height.
  const Stamp mid{1, 1, {0xFF808080u}};
  const Stamps midStamps = buildStamps(&mid, nullptr, false);
  PaintSettings bump = s;
  bump.tool = PaintTool::Bump;
  bump.stamps = &midStamps;
  Canvas h(m, map, kSize, kSize);
  h.dab(p, bump);
  const int64_t alpha = midStamps.bump.rgba[0] >> 24;
  const int64_t expect = std::lround(alpha / 255.0f * 24576) * 255 / 256;
  check(int64_t(h.heights()[c]) - kNeutralHeight == expect,
        "stamp dab: bump scales with the stamp's gray (" + std::to_string(int64_t(h.heights()[c]) - kNeutralHeight) +
            ", expected " + std::to_string(expect) + ")");

  // Depth: the stamp reaches 0.45 r along its normal (fully up to 0.3 r).
  s.stamps = &white;
  int tooDeep = 0, painted = 0;
  for (float lift : {0.2f, 0.6f}) {
    Canvas cv(m, map, kSize, kSize);
    const Vec3 c2 = p + s.frame.n * (lift * s.radius);
    cv.dab(c2, s);
    for (size_t i = 0; i < before.size(); ++i) {
      if (cv.colorImage()[i] == before[i]) continue;
      ++painted;
      if (std::fabs(dot(tx.pos[i] - c2, s.frame.n)) > 0.45f * s.radius * 1.001f) ++tooDeep;
    }
  }
  check(painted > 10 && tooDeep == 0, "stamp dab: only texels within 0.45 r along the stamp normal (" +
                                          std::to_string(painted) + " painted)");
}

void testFill(const Mesh& m, const TextureMapping& map) {
  Canvas canvas(m, map, kSize, kSize);
  const Texels& tx = canvas.texels();
  const std::vector<uint32_t> before = canvas.colorImage();
  PaintSettings s;
  canvas.fill(s, 1.0f, false);
  int wrong = 0, touchedUncovered = 0;
  for (size_t i = 0; i < tx.triangle.size(); ++i) {
    if (tx.triangle[i] < 0) touchedUncovered += canvas.colorImage()[i] != before[i];
    else wrong += !sameColor(canvas.colorImage()[i], s.primary, 1);
  }
  check(wrong == 0 && touchedUncovered == 0, "fill: strength 1 paints every covered texel, nothing else");
  canvas.fill(s, 1.0f, true);
  wrong = 0;
  for (size_t i = 0; i < tx.triangle.size(); ++i) wrong += tx.triangle[i] >= 0 && !sameColor(canvas.colorImage()[i], s.secondary, 1);
  check(wrong == 0, "fill: inverted fills with the secondary color");
  canvas.undo();
  canvas.undo();
  check(canvas.colorImage() == before, "fill: undo restores the color layer");

  const int c = centerTexel(m, tx, 0);
  const uint32_t from = canvas.colorImage()[c];
  canvas.fill(s, 0.5f, false);
  bool halfway = true;
  for (int k = 0; k < 3; ++k) {
    float expect = channel(from, k) + (channel(s.primary, k) - channel(from, k)) * 0.5f;
    if (std::fabs(channel(canvas.colorImage()[c], k) - expect) > 1.5f) halfway = false;
  }
  check(halfway, "fill: strength 0.5 goes half the way");

  // Clear: the bump tools pull the heights toward neutral.
  Vec3 center;
  PaintSettings bump;
  bump.tool = PaintTool::Bump;
  bump.radius = length(bboxDiagonal(m, &center)) * 0.05f;
  bump.strength = 1.97f;
  bump.toViewer = tx.normal[c];
  canvas.dab(tx.pos[c], bump);
  canvas.dab(tx.pos[c], bump);
  const int64_t h0 = int64_t(canvas.heights()[c]) - kNeutralHeight;
  const std::vector<uint32_t> beforeClear = canvas.colorImage();
  canvas.fill(bump, 1.0f, false);
  const int64_t h1 = int64_t(canvas.heights()[c]) - kNeutralHeight;
  check(h0 > 0 && h1 == h0 + ((-255 * h0) >> 8), "clear: the bump moves 255/256 of the way to neutral");
  check(canvas.colorImage() == beforeClear, "clear: colors untouched");

  // With a layer as target, Paint color fills its mask.
  const int slot = canvas.addLayer("layer");
  PaintSettings maskFill;
  maskFill.target = slot;
  const std::vector<uint32_t> colors = canvas.colorImage();
  canvas.takeChangedMasks();
  canvas.fill(maskFill, 1.0f, false);
  check(canvas.material(slot).mask[c] == 255 && canvas.colorImage() == colors &&
            canvas.takeChangedMasks() == 1u << slot,
        "fill: with a layer as target the mask fills, not the colors");
  canvas.fill(maskFill, 1.0f, true);
  check(canvas.material(slot).mask[c] == 0, "fill: inverted empties the mask");
  canvas.undo();
  check(canvas.material(slot).mask[c] == 255, "fill: undo restores the mask");
}

void testTangents(const Mesh& m, const TextureMapping& map) {
  std::vector<Tangent> tan = computeTangents(m, map);
  int bad = 0, wrongWay = 0, total = 0;
  for (size_t i = 0; i < tan.size(); ++i) {
    const Vec3& n = m.vertex(map.texverts[i].vertex).normal;
    if (std::fabs(length(tan[i].t) - 1) > 1e-3f || std::fabs(dot(tan[i].t, n)) > 1e-3f) ++bad;
  }
  // Per triangle: t points along +u and w * cross(n, t) along +v.
  for (int t = 0; t < m.triangleSlots(); ++t) {
    const auto& c = map.corners[t];
    if (!m.triangle(t).active || c[0] < 0) continue;
    const TexVert &a = map.texverts[c[0]], &b = map.texverts[c[1]], &d = map.texverts[c[2]];
    const Vec3 e1 = m.vertex(b.vertex).pos - m.vertex(a.vertex).pos, e2 = m.vertex(d.vertex).pos - m.vertex(a.vertex).pos;
    const float u1 = b.u - a.u, v1 = b.v - a.v, u2 = d.u - a.u, v2 = d.v - a.v, det = u1 * v2 - u2 * v1;
    if (std::fabs(det) < 1e-12f) continue;
    const Vec3 pu = (e1 * v2 - e2 * v1) / det, pv = (e2 * u1 - e1 * u2) / det;
    const Tangent& tg = tan[c[0]];
    const Vec3 n = m.vertex(a.vertex).normal;
    ++total;
    if (dot(tg.t, pu) <= 0 || dot(cross(n, tg.t) * tg.w, pv) <= 0) ++wrongWay;
  }
  check(bad == 0, "tangents: unit length, orthogonal to the normal");
  check(wrongWay <= total / 100, "tangents: along +u and +v (" + std::to_string(wrongWay) + " of " +
                                     std::to_string(total) + " triangles off)");
}

// Paints, adds a layer, saves with paint data, loads again: everything back.
void testSave(const Mesh& m, const TextureMapping& map, const Sc1File& original) {
  Canvas canvas(m, map, kSize, kSize);
  canvas.material(0).matcap = "bildhauer:clay";
  const Texels& tx = canvas.texels();
  Vec3 center;
  const float diag = length(bboxDiagonal(m, &center));
  PaintSettings s;
  s.radius = diag * 0.05f;
  const int c = centerTexel(m, tx, s.radius * 1.3f);
  if (c < 0) return;
  s.toViewer = tx.normal[c];
  canvas.dab(tx.pos[c], s);
  s.tool = PaintTool::Bump;
  canvas.dab(tx.pos[c], s);
  const int slot = canvas.addLayer("bildhauer:gold");
  canvas.material(slot).additive = true;
  s.tool = PaintTool::Color;
  s.target = slot;
  canvas.dab(tx.pos[c], s);

  Sc1File file = original;
  meshToSc1(m, file, map, canvas);
  std::vector<uint8_t> data;
  std::string err;
  Sc1File back;
  Mesh mb;
  Sc1Texturing tex;
  TextureMapping mapBack;
  bool ok = writeSc1(file, data, &err) && readSc1(data, back, &err) && sc1ToMesh(back, mb, &err) &&
            decodeTexturing(back, tex, &err) && texturingToMapping(tex, mb, mapBack, &err);
  check(ok, "save: paint data written and read back" + (err.empty() ? "" : " (" + err + ")"));
  if (!ok) return;
  Canvas loaded(mb, mapBack, tex.color.w, tex.color.h);
  ok = texturingToCanvas(tex, loaded, &err);
  check(ok, "save: canvas loads" + (err.empty() ? "" : " (" + err + ")"));
  if (!ok) return;
  check(loaded.colorImage() == canvas.colorImage() && loaded.heights() == canvas.heights(), "save: images identical");
  bool masks = true;
  for (int k = 0; k < kMaxMaterials; ++k) {
    const Material &a = canvas.material(k), &b = loaded.material(k);
    if (a.used != b.used || (a.used && (a.matcap != b.matcap || a.mask != b.mask || a.colored != b.colored ||
                                        a.additive != b.additive || a.hidden != b.hidden)))
      masks = false;
  }
  check(masks, "save: material slots identical");
  bool uvs = mapBack.texverts.size() == map.texverts.size() && mapBack.numCharts == map.numCharts;
  for (size_t i = 0; uvs && i < map.texverts.size(); ++i)
    uvs = mapBack.texverts[i].u == map.texverts[i].u && mapBack.texverts[i].v == map.texverts[i].v;
  for (int t = 0; uvs && t < m.triangleSlots(); ++t)
    uvs = mapBack.corners[t] == map.corners[t] && mapBack.chart[t] == map.chart[t];
  check(uvs, "save: UVs, corners and charts identical");
  // Saving the loaded file again gives the same bytes.
  Sc1File again = back;
  meshToSc1(mb, again, mapBack, loaded);
  std::vector<uint8_t> data2;
  check(writeSc1(again, data2, &err) && data2 == data, "save: a second save is byte-identical");
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
  testFill(m, map);
  testStampDabs(m, map);
  testMasks(m, map);
  testTangents(m, map);
  testSave(m, map, file);

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
    std::vector<uint8_t> encoded;
    encodeTexturing(tex, encoded);
    check(encoded == file.texturing, "stored texturing block re-encodes byte-identically");
    TextureMapping storedMap = stored;
    testTangents(m, storedMap);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: paint_test <file.sc1>...\n");
    return 2;
  }
  testStampBasics();
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

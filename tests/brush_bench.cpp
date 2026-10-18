// Times brush strokes on a real mesh, phase by phase. Not a test; run it by
// hand to find performance bottlenecks.
//
// Usage: brush_bench <file.sc1>

#include <chrono>
#include <cstdio>
#include <string>

#include "core/brush.h"
#include "core/pick.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::vector<uint8_t> data;
  Sc1File file;
  Mesh base;
  std::string err;
  if (!readBinaryFile(argv[1], data) || !readSc1(data, file, &err) || !sc1ToMesh(file, base, &err)) return 1;
  Vec3 center;
  const Vec3 extent = bboxDiagonal(base, &center);
  const float size = length(extent);
  if (base.symmetric) center.x = 0;
  const Vec3 eye = center + Vec3{0, 0, 2 * size}, fwd{0, 0, -1};

  auto input = [&](const Mesh& m, Vec3 target) {
    StrokeInput in;
    in.rayOrigin = eye;
    in.rayDir = normalize(target - eye);
    in.viewDir = fwd;
    in.camRight = {1, 0, 0};
    in.camUp = {0, 1, 0};
    PickHit h = pick(m, in.rayOrigin, in.rayDir);
    in.point = h.visiblePoint();
    in.hit = h.hit;
    return in;
  };

  for (float radiusFactor : {0.08f, 0.03f, 0.012f}) {
    for (Tool tool : {Tool::Draw, Tool::Smooth, Tool::Crease}) {
      Mesh m = base;
      BrushSettings s;
      s.tool = tool;
      s.radius = radiusFactor * size;
      s.detailSize = s.radius * 0.5f;
      s.strength = 0.6f;
      int t0 = m.numTriangles();
      double pickMs = 0;
      int picks = 0, misses = 0;
      float pathLength = 0;
      Vec3 last;
      bool haveLast = false;
      auto t = std::chrono::steady_clock::now();
      Stroke stroke(m, s, input(m, center + Vec3{-0.15f * size, 0.05f * size, 0}));
      for (int i = 1; i <= 60; ++i) {
        auto p0 = std::chrono::steady_clock::now();
        StrokeInput in = input(m, center + Vec3{-0.15f * size + 0.005f * size * i, 0.05f * size, 0});
        pickMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count();
        ++picks;
        misses += !in.hit;
        if (in.hit) {
          if (haveLast) pathLength += length(in.point - last);
          last = in.point;
          haveLast = true;
        }
        stroke.moveTo(in);
      }
      double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
      const DabTimes& d = stroke.times();
      int n = std::max(1, stroke.dabCount());
      std::printf("%-7s r=%.3f  %3d dabs  tris %6d -> %6d  total %7.1f ms  per dab: tess %6.2f tool %6.2f geom %6.2f "
                  "beaut %6.2f  worst %6.1f  pick %5.2f  path %.1f r, %d misses\n",
                  toolName(tool), radiusFactor, stroke.dabCount(), t0, m.numTriangles(), total, d.tessellate / n,
                  d.tool / n, d.geometry / n, d.beautify / n, d.worstDab, pickMs / picks, pathLength / s.radius, misses);
    }
  }
  {  // the viewer copies the mesh for undo at every stroke start
    Mesh big = base;
    BrushSettings s;
    s.radius = 0.012f * size;
    s.detailSize = s.radius * 0.5f;
    Stroke stroke(big, s, input(big, center + Vec3{-0.15f * size, 0.05f * size, 0}));
    for (int i = 1; i <= 60; ++i) stroke.moveTo(input(big, center + Vec3{-0.15f * size + 0.005f * size * i, 0.05f * size, 0}));
    auto t = std::chrono::steady_clock::now();
    Mesh copy = big;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    std::printf("undo copy of %d triangles: %.2f ms\n", copy.numTriangles(), ms);
  }
  return 0;
}

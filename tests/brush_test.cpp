// Picking and brushes on real meshes. A camera looks at the model from the
// front; strokes run near the symmetry plane so the mirror pass is active.
// After every tool: validity, unchanged topology, exact plane, plus a
// tool-specific effect (Draw/Inflate raise the surface along the normals,
// inverted Draw lowers it, Mask lowers weights, Reduce removes triangles,
// Grab moves the anchor area, Scale enlarges it).
//
// Usage: brush_test <file.sc1>...

#include <cmath>
#include <cstdio>
#include <string>

#include "core/brush.h"
#include "core/pick.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

namespace {

struct Camera {
  Vec3 eye, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, -1};
};

StrokeInput inputAt(const Mesh& m, const Camera& cam, const Vec3& target, float mx, float my, bool* hit) {
  StrokeInput in;
  in.rayOrigin = cam.eye;
  in.rayDir = normalize(target - cam.eye);
  in.viewDir = cam.forward;
  in.camRight = cam.right;
  in.camUp = cam.up;
  in.mouseX = mx;
  in.mouseY = my;
  PickHit h = pick(m, in.rayOrigin, in.rayDir);
  if (hit) *hit = h.hit;
  in.point = h.hit ? h.point : target;
  in.hit = h.hit;
  return in;
}

// Brush displacement near c along the normals, measured against the
// reference state; tessellation alone does not change it.
float displacement(const Mesh& m, const Vec3& c, float radius) {
  float sum = 0;
  for (int v = 0; v < m.vertexSlots(); ++v) {
    const Vertex& vx = m.vertex(v);
    if (vx.active && length(vx.pos - c) < radius) sum += dot(vx.pos - vx.refPos, vx.normal);
  }
  return sum;
}

float surfaceDistance(const Mesh& m, const Camera& cam, const Vec3& target) {
  PickHit h = pick(m, cam.eye, normalize(target - cam.eye));
  return h.hit ? h.t : -1;
}

void testFile(const std::string& path) {
  std::printf("%s\n", path.c_str());
  std::vector<uint8_t> data;
  Sc1File file;
  Mesh base;
  std::string err;
  if (!readBinaryFile(path, data) || !readSc1(data, file, &err) || !sc1ToMesh(file, base, &err)) {
    check(false, "load: " + err);
    return;
  }
  const bool sym = file.symmetry;
  const Topology before = fullTopology(base, sym);
  Vec3 center;
  const Vec3 extent = bboxDiagonal(base, &center);
  const float size = length(extent);
  if (sym) center.x = 0;  // the full model is centered on the plane
  // Look along the thinnest of y/z, from the front side.
  Camera cam;
  if (extent.z < extent.y) {
    cam.forward = {0, 0, -1};
    cam.up = {0, 1, 0};
  } else {
    cam.forward = {0, -1, 0};
    cam.up = {0, 0, -1};
  }
  cam.eye = center - cam.forward * (2 * size);

  // Target: the front-most vertex close to the symmetry plane.
  Vec3 mid;
  float best = -1e30f;
  for (int v = 0; v < base.vertexSlots(); ++v) {
    const Vertex& vx = base.vertex(v);
    if (!vx.active || vx.pos.x < 0.01f * size || vx.pos.x > 0.06f * size) continue;
    float front = dot(vx.pos, -cam.forward);
    if (front > best) { best = front; mid = vx.pos; }
  }

  {  // picking, including the mirrored half
    PickHit right = pick(base, cam.eye, normalize(mid - cam.eye));
    check(right.hit && !right.mirrored, "pick: hits the stored half");
    if (sym) {
      PickHit left = pick(base, cam.eye, normalize(Vec3{-mid.x, mid.y, mid.z} - cam.eye));
      check(left.hit && left.mirrored && left.point.x >= 0, "pick: mirrored half maps onto the stored half");
    }
  }

  const float midDistance = surfaceDistance(base, cam, mid);
  if (midDistance < 0) {
    check(false, "front of the model is pickable");
    return;
  }

  for (int ti = 0; ti <= int(Tool::Reduce); ++ti) {
    for (bool inverted : {false, true}) {
      const Tool tool = Tool(ti);
      if (inverted && tool != Tool::Draw) continue;
      Mesh m = base;
      BrushSettings s;
      s.tool = tool;
      s.radius = 0.08f * size;
      s.detailSize = 0.04f * size;
      s.strength = tool == Tool::Mask ? -0.8f : (inverted ? -0.6f : 0.6f);
      std::string name = std::string(toolName(tool)) + (inverted ? " inverted" : "");
      int t0 = m.numTriangles();

      bool hit = false;
      const Vec3 along = cam.up * (0.006f * size);
      Stroke stroke(m, s, inputAt(m, cam, mid, 400, 300, &hit));
      for (int i = 1; i <= 8; ++i) {
        Vec3 target = mid + along * float(i);
        stroke.moveTo(inputAt(m, cam, target, 400 + 12.0f * i, 300, nullptr));
      }
      std::printf("  %s: %d dabs, %d -> %d triangles\n", name.c_str(), stroke.dabCount(), t0, m.numTriangles());
      check(hit && stroke.dabCount() > 0, name + ": stroke ran");
      checkMesh(m, sym, before, name.c_str());

      float moved = displacement(m, mid, s.radius);
      switch (tool) {
        case Tool::Draw:
          check(inverted ? moved < 0 : moved > 0, name + (inverted ? ": surface lowered" : ": surface raised"));
          break;
        case Tool::Inflate: check(moved > 0, name + ": surface raised"); break;
        case Tool::Mask: {
          int masked = 0;
          for (int v = 0; v < m.vertexSlots(); ++v) masked += m.vertex(v).active && m.vertex(v).weight < 0.9f;
          check(masked > 0, name + ": weights lowered (" + std::to_string(masked) + " vertices)");
          break;
        }
        case Tool::Reduce: check(m.numTriangles() < t0, name + ": fewer triangles"); break;
        case Tool::Grab: {
          float dragged = 0;
          for (int v = 0; v < m.vertexSlots(); ++v)
            if (m.vertex(v).active) dragged = std::fmax(dragged, dot(m.vertex(v).pos - m.vertex(v).refPos, cam.up));
          check(dragged > 0.01f * size, name + ": area dragged upward");
          break;
        }
        default: break;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: brush_test <file.sc1>...\n");
    return 2;
  }
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

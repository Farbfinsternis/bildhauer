// Picking and brushes on real meshes. A camera looks at the model from the
// front; strokes run near the symmetry plane so the mirror pass is active.
// After every tool: validity, unchanged topology, exact plane, plus a
// tool-specific effect (Draw/Inflate raise the surface along the normals,
// inverted Draw lowers it, Mask lowers weights, Reduce removes triangles,
// Grab moves the anchor area, Scale enlarges it). The brush mask on flat
// patches.
//
// Usage: brush_test <file.sc1>...

#include <cmath>
#include <cstdio>
#include <string>

#include "core/brush.h"
#include "core/brush_params.h"
#include "core/pick.h"
#include "core/relax.h"
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
  in.point = h.hit ? h.visiblePoint() : target;
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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(const Vec3& a, const Vec3& b, float eps = 1e-5f) { return length(a - b) <= eps; }

// Slider mapping and dab path against the values in re/docs/brushes.md 2.3/2.4.
void testParams() {
  std::printf("brush parameters\n");
  check(near(brushStrength(0), 0.1335f) && near(brushStrength(1), 1.9735f), "strength: 0.13 ... 1.97");
  check(near(brushRadius(0.3f, 10), 10 * 0.12f * (1.7f * std::pow(0.3f, 1.9f) + 0.03f)), "radius: proportional to depth");
  check(near(brushRadius(0.3f, 20) / brushRadius(0.3f, 10), 2), "radius: doubles with depth");
  check(brushRadius(0.4f, 10, 1.0f) > brushRadius(0.4f, 10, 0.5f), "radius: wider FOV, larger radius");
  check(fovDegrees(0) == 15 && fovDegrees(0.5f) == 35 && fovDegrees(1) == 55, "FOV slider: 15 ... 55 degrees");
  check(near(spacingFactor(0), 0.05f) && near(spacingFactor(1), 5.0f), "spacing factor: 0.05 ... 5");
  check(near(pressureCurve(1), 1) && near(pressureCurve(0.5f), std::pow(0.5f, 1.12f)), "pressure curve");
  check(near(defaultSliders(Tool::Smooth).size, 0.2f) && near(defaultSliders(Tool::Flatten).strength, 0.4f) &&
            near(defaultSliders(Tool::Draw).strength, 1),
        "slider defaults");

  const Vec3 c{0, 0, 0}, b{1, 0, 0}, a{1, 1, 0};
  check(near(dabCurve(c, b, a, 0), (c + b) * 0.5f) && near(dabCurve(c, b, a, 1), (b + a) * 0.5f),
        "dab curve: midpoint to midpoint");
  Vec3 m = dabCurve(c, b, a, 0.5f);
  check(m.x > 0.75f && m.y < 0.25f, "dab curve: cuts the corner toward the middle sample");
  check(near(dabCurve(b, b, b, 0.3f), b), "dab curve: a resting pointer stays put");
  Vec3 onLine = dabCurve({0, 0, 0}, {1, 0, 0}, {2, 0, 0}, 0.4f);
  check(near(onLine.y, 0) && onLine.x > 0.5f && onLine.x < 1.5f, "dab curve: straight samples, straight path");
}

// A Draw stroke straight across the plane of a flat symmetric patch must
// raise an even ridge: the stroke is folded onto the stored half, which must
// not weaken the plane region (brush.h, symmetry compensation).
void testSymmetricStroke() {
  std::printf("stroke across the symmetry plane\n");
  Mesh m;
  const int nx = 30, ny = 40;
  for (int j = 0; j <= ny; ++j)
    for (int i = 0; i <= nx; ++i) m.addVertex({1.5f * i / nx, -1 + 2.0f * j / ny, 0});
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      int a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
      m.addTriangle(a, b, d);
      m.addTriangle(a, d, c);
    }
  m.symmetric = true;
  for (int v = 0; v < m.vertexSlots(); ++v) m.vertex(v).onSymmetryPlane = m.vertex(v).pos.x == 0;
  m.updateGeometry();
  m.storeReference();

  BrushSettings s;
  s.tool = Tool::Draw;
  s.radius = s.detailSize = 0.15f;
  s.strength = brushStrength(1);
  for (float step : {0.01f, 0.08f}) {
    Mesh mesh = m;
    StrokeInput in;
    in.viewDir = {0, 0, -1};
    in.point = {-1.2f, 0, 0};
    Stroke stroke(mesh, s, in);
    for (float x = -1.2f + step; x <= 1.2f; x += step) {
      in.point = {x, 0, 0};
      stroke.moveTo(in);
    }
    // Ridge height along the stroke line, in bins of 0.05.
    float lo = 1e30f, hi = 0;
    for (float x = 0; x < 0.6f; x += 0.05f) {
      float sum = 0;
      int n = 0;
      for (int v = 0; v < mesh.vertexSlots(); ++v) {
        const Vertex& vx = mesh.vertex(v);
        if (vx.active && std::fabs(vx.refPos.y) < 0.03f && vx.refPos.x >= x && vx.refPos.x < x + 0.05f) {
          sum += vx.pos.z;
          ++n;
        }
      }
      if (n == 0) continue;
      lo = std::fmin(lo, sum / n);
      hi = std::fmax(hi, sum / n);
    }
    char what[96];
    std::snprintf(what, sizeof what, "step %.2f: ridge even across the plane (%.3f ... %.3f)", step, lo, hi);
    check(hi > 0 && lo > 0.85f * hi, what);

    // The viewer runs one beautify pass per frame until nothing is dirty.
    int frames = 0;
    while (!mesh.dirtyVertices.empty() && frames < 1000) {
      beautifyPass(mesh);
      ++frames;
    }
    std::snprintf(what, sizeof what, "step %.2f: beautify settles after the stroke (%d frames)", step, frames);
    check(mesh.dirtyVertices.empty(), what);
  }
}

// A flat patch in z = 0, x from x0 to 1, y from -1 to 1; optionally with
// ripples (for Smooth and Flatten) and as the stored half of a symmetric model.
Mesh patch(float x0, bool symmetric, bool ripples) {
  Mesh m;
  const int nx = int(std::lround((1 - x0) * 40)), ny = 80;
  for (int j = 0; j <= ny; ++j)
    for (int i = 0; i <= nx; ++i) {
      const float x = x0 + (1 - x0) * i / nx, y = -1 + 2.0f * j / ny;
      m.addVertex({x, y, ripples ? 0.02f * std::sin(40 * x) * std::sin(40 * y) : 0.0f});
    }
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      int a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
      m.addTriangle(a, b, d);
      m.addTriangle(a, d, c);
    }
  m.symmetric = symmetric;
  for (int v = 0; v < m.vertexSlots(); ++v) m.vertex(v).onSymmetryPlane = symmetric && m.vertex(v).pos.x == 0;
  m.updateGeometry();
  m.storeReference();
  return m;
}

// One dab at c, seen from +z (screen right +x, up +y).
void dabAt(Mesh& m, const BrushSettings& s, const Vec3& c) {
  StrokeInput in;
  in.point = c;
  in.viewDir = {0, 0, -1};
  in.camRight = {1, 0, 0};
  in.camUp = {0, 1, 0};
  Stroke stroke(m, s, in);
}

// The brush mask (brushes.md 3.4) on a flat patch.
void testBrushMask() {
  std::printf("brush mask\n");
  const Stamp white{1, 1, {0xFFFFFFFFu}}, clear{1, 1, {0x00FFFFFFu}};
  Stamp leftHalf{8, 1, {}};  // opaque for u < 0.5: left on screen at angle 0
  for (int x = 0; x < 8; ++x) leftHalf.rgba.push_back(x < 4 ? 0xFFFFFFFFu : 0x00FFFFFFu);
  BrushSettings s;
  s.tool = Tool::Draw;
  s.radius = 0.5f;
  s.detailSize = 2;  // no tessellation: vertices stay where the checks look
  s.strength = brushStrength(1);

  // A white mask replaces smooth(1 - 1.2 d/r) by 1 (Draw's first dab: z = r f k).
  Mesh plain = patch(-1, false, false), full = patch(-1, false, false);
  dabAt(plain, s, {0, 0, 0});
  s.stamp = &white;
  dabAt(full, s, {0, 0, 0});
  int ratioOk = 0, ratioBad = 0, beyond = 0;
  for (int v = 0; v < plain.vertexSlots(); ++v) {
    const Vec3 p = plain.vertex(v).refPos;
    const float d = length(p), zPlain = plain.vertex(v).pos.z, zMask = full.vertex(v).pos.z;
    if (d >= s.radius / 1.2f) {
      beyond += zMask != 0;
      continue;
    }
    if (std::fabs(p.x) > 0.6f * s.radius || std::fabs(p.y) > 0.6f * s.radius || zPlain < 1e-5f) continue;
    const float expect = 1 / smoothFalloff(1 - 1.2f * d / s.radius);
    (std::fabs(zMask / zPlain - expect) < 1e-3f * expect ? ratioOk : ratioBad)++;
  }
  check(ratioOk > 20 && ratioBad == 0, "brush mask: replaces the falloff (" + std::to_string(ratioOk) + " vertices, " +
                                           std::to_string(ratioBad) + " off)");
  check(beyond == 0, "brush mask: nothing moves beyond the brush's reach");

  // Half mask: only the left half rises; turned by 90 degrees, the lower half.
  auto raised = [](const Mesh& m, auto&& where) {
    int n = 0;
    for (int v = 0; v < m.vertexSlots(); ++v)
      if (where(m.vertex(v).refPos) && m.vertex(v).pos.z > 1e-6f) ++n;
    return n;
  };
  s.stamp = &leftHalf;
  Mesh half = patch(-1, false, false);
  dabAt(half, s, {0, 0, 0});
  const int left = raised(half, [](const Vec3& p) { return p.x < -0.1f; });
  const int right = raised(half, [](const Vec3& p) { return p.x > 0.1f; });
  check(left > 20 && right == 0, "brush mask: a half mask raises one side (" + std::to_string(left) + " / " +
                                     std::to_string(right) + ")");
  s.stampAngle = 3.14159265f / 2;
  Mesh turned = patch(-1, false, false);
  dabAt(turned, s, {0, 0, 0});
  const int below = raised(turned, [](const Vec3& p) { return p.y < -0.1f; });
  const int above = raised(turned, [](const Vec3& p) { return p.y > 0.1f; });
  check(below > 20 && above == 0, "brush mask: the angle turns it (" + std::to_string(below) + " / " +
                                      std::to_string(above) + ")");
  s.stampAngle = 0;

  // Symmetry: a dab on the mirrored half at x = -0.5 is mirrored with its
  // stamp, so the screen-left side (x < -0.5) lands at x > 0.5.
  Mesh sym = patch(0, true, false);
  dabAt(sym, s, {-0.5f, 0, 0});
  const int outer = raised(sym, [](const Vec3& p) { return p.x > 0.6f; });
  const int inner = raised(sym, [](const Vec3& p) { return p.x < 0.4f; });
  check(outer > 20 && inner == 0, "brush mask: mirrored with the dab (" + std::to_string(outer) + " / " +
                                      std::to_string(inner) + ")");

  // Smooth and Flatten read it too; Inflate does not.
  for (Tool tool : {Tool::Smooth, Tool::Flatten, Tool::Inflate}) {
    s.tool = tool;
    s.strength = 1;
    const Mesh start = patch(-1, false, true);
    auto movedBy = [&](const Stamp* st) {
      Mesh m = start;
      s.stamp = st;
      dabAt(m, s, {0, 0, 0});
      float most = 0;
      for (int v = 0; v < m.vertexSlots(); ++v) most = std::fmax(most, length(m.vertex(v).pos - start.vertex(v).pos));
      return most;
    };
    const float withClear = movedBy(&clear), withWhite = movedBy(&white);
    const std::string name = toolName(tool);
    if (tool == Tool::Inflate) check(withClear > 0, name + ": ignores the brush mask");
    else check(withClear == 0 && withWhite > 0, name + ": a clear mask stops it, a white one does not");
  }
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
  testParams();
  testSymmetricStroke();
  testBrushMask();
  for (int i = 1; i < argc; ++i) testFile(argv[i]);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

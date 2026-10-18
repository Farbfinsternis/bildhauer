#include "core/brush.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "core/brush_params.h"
#include "core/reduce.h"
#include "core/relax.h"
#include "core/tessellate.h"

namespace bh {

namespace {

constexpr float kPi = 3.14159265f;

double msSince(std::chrono::steady_clock::time_point& t) {
  auto now = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(now - t).count();
  t = now;
  return ms;
}

float smoothFalloff(float x) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  return (1 - std::cos(kPi * x)) / 2;
}

Vec3 mirrorX(Vec3 v) { return {-v.x, v.y, v.z}; }

Vec3 projectOnPlane(const Vec3& p, const Vec3& origin, const Vec3& n) { return p - n * dot(n, p - origin); }

void touched(Mesh& mesh, int v) {
  mesh.vertex(v).relaxCountdown = 10;
  markDirty(mesh, v);
}

bool usable(const Vertex& v) { return v.active && !v.hidden; }

}  // namespace

const char* toolName(Tool tool) {
  static const char* names[] = {"Draw", "Smooth", "Grab", "Inflate", "Flatten", "Pinch",
                                "Mask", "Crease", "Rotate", "Scale", "Reduce"};
  return names[int(tool)];
}

Stroke::Stroke(Mesh& mesh, const BrushSettings& settings, const StrokeInput& start)
    : mesh_(mesh), s_(settings), start_(start), sampleB_(start.point), sampleC_(start.point),
      anchor_(start.point) {
  if (mesh_.symmetric && anchor_.x < 0) anchor_.x = -anchor_.x;
  mesh_.storeReference();
  if (s_.detailFactor >= 0.1f || s_.tool == Tool::Reduce) mesh_.clearQuads();
  if (s_.tool == Tool::Grab || s_.tool == Tool::Rotate || s_.tool == Tool::Scale) dab(anchor_, start);
  else moveTo(start);
}

float Stroke::weight(int v) const { return s_.respectMask ? mesh_.vertex(v).weight : 1.0f; }

void Stroke::updateRegion(const Vec3& c) {
  const bool transformTool = s_.tool == Tool::Grab || s_.tool == Tool::Rotate || s_.tool == Tool::Scale;
  const float reach = 1.6f * s_.radius;
  const bool all = transformTool && s_.global;
  if (!all && regionValid_ && length(c - regionCenter_) + reach <= regionRadius_) return;
  regionCenter_ = c;
  regionRadius_ = 2 * reach;
  regionValid_ = !all;
  region_.clear();
  inRegion_.assign(mesh_.vertexSlots(), 0);
  const Vec3 cm = mirrorX(c);
  for (int v = 0; v < mesh_.vertexSlots(); ++v) {
    const Vertex& vx = mesh_.vertex(v);
    if (!vx.active) continue;
    if (all || length(vx.pos - c) < regionRadius_ || (mesh_.symmetric && length(vx.pos - cm) < regionRadius_)) {
      region_.push_back(v);
      inRegion_[v] = 1;
    }
  }
}

Vec3 Stroke::dragPoint(const StrokeInput& in) const {
  // Pointer ray against the view plane through the anchor.
  float denom = dot(in.rayDir, in.viewDir);
  if (std::fabs(denom) < 1e-6f) return anchor_;
  float t = dot(anchor_ - in.rayOrigin, in.viewDir) / denom;
  return in.rayOrigin + in.rayDir * t;
}

void Stroke::moveTo(const StrokeInput& in) {
  if (s_.tool == Tool::Grab || s_.tool == Tool::Rotate || s_.tool == Tool::Scale) {
    dab(dragPoint(in), in);
    return;
  }
  if (!in.hit) return;  // off the model: wait until the pointer is back
  // Sample the curve through the last samples finely and dab whenever the
  // path has moved on by `spacing`. The older sample only advances once a dab
  // was placed, so slow movement accumulates.
  const Vec3 a = in.point;
  const float spacing = s_.spacingFactor * s_.radius;
  const int n = std::min(100000, int(length(a - sampleC_) * 10 / spacing));
  Vec3 last = sampleC_;
  bool placed = false;
  for (int i = 0; i <= n; ++i) {
    Vec3 q = dabCurve(sampleC_, sampleB_, a, n > 0 ? float(i) / n : 0.0f);
    if (firstDab_ || length(q - last) > spacing) {
      firstDab_ = false;
      placed = true;
      last = q;
      dab(q, in);
    }
  }
  if (placed) sampleC_ = sampleB_;
  sampleB_ = a;
}

void Stroke::setSize(float radius, float detailSize) {
  s_.radius = radius;
  s_.detailSize = detailSize;
}

void Stroke::dab(Vec3 c, const StrokeInput& in) {
  if (mesh_.symmetric && c.x < 0) c.x = -c.x;
  ++dabs_;
  auto clock = std::chrono::steady_clock::now();
  const auto dabStart = clock;
  const Tool tool = s_.tool;
  const bool transformTool = tool == Tool::Grab || tool == Tool::Rotate || tool == Tool::Scale;
  updateRegion(c);
  if (tool != Tool::Smooth && tool != Tool::Reduce && !(transformTool && s_.global)) {
    BrushDetail detail;
    detail.center = c;
    detail.radius = s_.radius;
    detail.detailSize = s_.detailSize;
    detail.detailFactor = s_.detailFactor;
    detail.beautify = false;  // runs once after the tool
    std::vector<int> created;
    tessellateForBrush(mesh_, detail, &region_, &created);
    inRegion_.resize(mesh_.vertexSlots(), 0);
    for (int v : created)
      if (!inRegion_[v]) { inRegion_[v] = 1; region_.push_back(v); }
  }
  times_.tessellate += msSince(clock);
  auto mirrorFor = [&](float r) { return mesh_.symmetric && c.x < r; };
  switch (tool) {
    case Tool::Draw: draw(c, mirrorFor(s_.radius)); break;
    case Tool::Smooth: smooth(c); break;
    case Tool::Grab: grab(c); break;
    case Tool::Inflate: inflate(c, mirrorFor(1.5f * s_.radius)); break;
    case Tool::Flatten: flatten(c, mirrorFor(1.5f * s_.radius), in.viewDir); break;
    case Tool::Pinch: pinch(c, mirrorFor(1.5f * s_.radius)); break;
    case Tool::Mask: mask(c); break;
    case Tool::Crease: crease(c, mirrorFor(s_.radius)); break;
    case Tool::Rotate:
    case Tool::Scale: transform(in); break;
    case Tool::Reduce: reduce(c); break;
  }
  times_.tool += msSince(clock);
  mesh_.updateGeometryAround(mesh_.dirtyVertices);
  times_.geometry += msSince(clock);
  if (s_.beautify) beautifyPass(mesh_);
  times_.beautify += msSince(clock);
  auto start = dabStart;
  times_.worstDab = std::max(times_.worstDab, msSince(start));
}

void Stroke::draw(const Vec3& c, bool mirror) {
  const float r = s_.radius, s = s_.strength;
  const Vec3 cm = mirrorX(c);
  Vec3 sumN, sumR;
  float sumA = 0;
  for (int v : region_) {
    const Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float d = length(vx.pos - c);
    if (d < r) {
      float a = (1 - d / r) * weight(v);
      sumN += vx.normal * a;
      sumR += vx.refPos * a;
      sumA += a;
    }
    if (mirror) {
      float dm = length(vx.pos - cm);
      if (dm < r) {
        float a = (1 - dm / r) * weight(v);
        sumN += mirrorX(vx.normal) * a;
        sumR += mirrorX(vx.refPos) * a;
        sumA += a;
      }
    }
  }
  if (sumA <= 0.01f) return;
  const Vec3 N = normalize(sumN), R = sumR / sumA;

  float k, h;
  if (!s_.clay) {
    k = 0.1f * std::pow(std::fabs(s), 1.3f) * 0.6f;
    h = r;
  } else {
    float a = std::pow(std::fabs(s), 0.8f) * 0.7f;
    if (s_.soft) { h = r * (1 + a) * 0.1f; k = 0.8f * a; }
    else { h = r * 0.2f * a; k = 0.6f * std::sqrt(a); }
    if (a > 0) h *= 1.5f;
  }
  const float sign = s >= 0 ? 1.0f : -1.0f;
  const float comp = mirror && s_.compensateSymmetry ? 1 - 0.5f * (r - c.x) / r : 1.0f;

  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    const Vec3 p = vx.pos;
    Vec3 move;
    bool moved = false;
    auto apply = [&](const Vec3& center, const Vec3& n, const Vec3& origin, float kfall) {
      float t = 1 - kfall * length(p - center) / r;
      if (t <= 0) return;
      float f = smoothFalloff(t) * weight(v) * comp;
      Vec3 delta = projectOnPlane(p, origin, n) + n * (sign * h) - p;
      if (dot(vx.normal, delta) * s <= 0) return;
      move += delta * (f * k);
      moved = true;
    };
    if (mirror) apply(cm, mirrorX(N), mirrorX(R), 1.3f);
    apply(c, N, R, 1.2f);
    if (!moved) continue;
    vx.pos += move;
    touched(mesh_, v);
  }
}

void Stroke::smooth(const Vec3& c) {
  const float r = s_.radius, s = std::fabs(s_.strength);
  for (int iteration = 0; iteration < 2; ++iteration) {
    for (int v : region_) {
      Vertex& vx = mesh_.vertex(v);
      if (!usable(vx)) continue;
      float t = 1 - 1.3f * length(vx.pos - c) / r;
      if (t <= 0) continue;
      bool boundary = !vx.onSymmetryPlane && isBoundaryVertex(mesh_, v);
      Vec3 sum;
      float sumW = 0;
      for (int i = 0; i < vx.numEdges; ++i) {
        const Edge& e = mesh_.edge(vx.edges[i]);
        if (e.quadDiagonal) continue;
        int n = e.other(v);
        if (boundary && !isBoundaryVertex(mesh_, n)) continue;
        float w = weight(n);
        sum += mesh_.vertex(n).pos * w;
        sumW += w;
      }
      if (sumW == 0) continue;
      Vec3 avg = sum / sumW;
      if (vx.onSymmetryPlane) avg.x = 0;
      vx.pos += (avg - vx.pos) * (std::sin(t * kPi / 2) * weight(v) * 0.7f * s);
      if (iteration == 0) touched(mesh_, v);
    }
  }
}

void Stroke::inflate(const Vec3& c, bool mirror) {
  const float r = 1.5f * s_.radius, s = s_.strength;
  const Vec3 cm = mirrorX(c);
  const float comp = mirror && s_.compensateSymmetry ? 1 - 0.5f * (r - c.x) / r : 1.0f;
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    bool moved = false;
    auto apply = [&](const Vec3& center) {
      float t = (1 - 1.3f * length(vx.pos - center) / r) * comp;
      if (t <= 0) return;
      Vec3 goal = vx.refPos + vx.normal * (r * std::sin(t * kPi / 2));
      vx.pos += (goal - vx.pos) * (t * weight(v) * 0.02f * s);
      moved = true;
    };
    if (mirror) apply(cm);
    apply(c);
    if (moved) touched(mesh_, v);
  }
}

void Stroke::pinch(const Vec3& c, bool mirror) {
  const float r = 1.5f * s_.radius, s = s_.strength;
  const Vec3 cm = mirrorX(c);
  float sum = 0;
  for (int v : region_) {
    const Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float d = length(vx.pos - c);
    if (d < r) sum += (1 - d / r) * weight(v);
    if (mirror && !vx.onSymmetryPlane) {
      float dm = length(vx.pos - cm);
      if (dm < r) sum += (1 - dm / r) * weight(v);
    }
  }
  if (sum <= 0.01f) return;
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float t = 1 - 1.3f * length(vx.pos - c) / r;
    if (t <= 0) continue;
    vx.pos += (c - vx.pos) * (t * t * weight(v) * 0.1f * s);
    touched(mesh_, v);
  }
}

void Stroke::crease(const Vec3& c, bool mirror) {
  const float r = s_.radius, s = s_.strength;
  const Vec3 cm = mirrorX(c);
  Vec3 sumN, sumR;
  float sumA = 0;
  for (int v : region_) {
    const Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float d = length(vx.pos - c);
    if (d < r) {
      float a = (1 - d / r) * (1 - d / r) * weight(v);
      sumN += vx.normal * a;
      sumR += vx.refPos * a;
      sumA += a;
    }
    if (mirror && !vx.onSymmetryPlane) {
      float dm = length(vx.pos - cm);
      if (dm < r) {
        float a = (1 - dm / r) * (1 - dm / r) * weight(v);
        sumN += mirrorX(vx.normal) * a;
        sumR += mirrorX(vx.refPos) * a;
        sumA += a;
      }
    }
  }
  if (sumA <= 0.01f) return;
  const Vec3 N = normalize(sumN), R = sumR / sumA;
  const float sp = s >= 0 ? s : -std::sqrt(-s) * 1.4f;
  const float r2 = 1.6f * r;
  const Vec3 goal = R - N * (r2 / 6 * sp);
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float t = 1 - 1.3f * length(vx.pos - c) / r2;
    if (t <= 0) continue;
    float f = t * t * weight(v);
    vx.pos += (goal - vx.pos) * (f * f * f * std::fabs(s) * 0.35f);
    touched(mesh_, v);
  }
}

void Stroke::flatten(const Vec3& c, bool mirror, const Vec3& viewDir) {
  const float r = 1.5f * s_.radius, s = s_.strength, rh = r / 2;
  const Vec3 cm = mirrorX(c);
  if (!(s_.lockPlane && planeLocked_)) {
    // View-facing weighting; viewDir points into the screen, so the surface
    // facing the camera has dot(-viewDir, n) > 0.
    const Vec3 toEye = -viewDir;
    Vec3 sumN, sumP;
    float sumA = 0;
    for (int v : region_) {
      const Vertex& vx = mesh_.vertex(v);
      if (!usable(vx)) continue;
      float d = length(vx.pos - c);
      if (d < rh) {
        float a = (1 - d / rh) * (dot(toEye, vx.normal) + 0.5f) * weight(v);
        sumN += vx.normal * a;
        sumP += vx.pos * a;
        sumA += a;
      }
      if (mirror && !vx.onSymmetryPlane) {
        float dm = length(vx.pos - cm);
        if (dm < rh) {
          Vec3 nm = mirrorX(vx.normal);
          float a = (1 - dm / rh) * (dot(toEye, nm) + 0.5f) * weight(v);
          sumN += nm * a;
          sumP += mirrorX(vx.pos) * a;
          sumA += a;
        }
      }
    }
    if (sumA <= 0.01f) return;
    planeN_ = normalize(sumN);
    planeP_ = sumP / sumA;
    planeLocked_ = true;
  }
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float t = 1 - 1.3f * length(vx.pos - c) / r;
    if (t <= 0) continue;
    float f = smoothFalloff(t);
    float height = dot(vx.pos - planeP_, planeN_);
    if (s_.lockPlane) {
      float g = (1 - std::fabs(height) / r) * 1.2f;
      if (g <= 0) continue;
      f *= std::min(g, 1.0f);
    } else if (s > 0 && height < 0) {
      continue;
    }
    if (s_.angleFalloff) {
      float a = (dot(vx.normal, planeN_) - 0.3f) * 1.5f;
      if (a <= 0) continue;
      f *= std::min(a, 1.0f);
    }
    Vec3 goal = vx.pos - planeN_ * height;
    vx.pos += (goal - vx.pos) * (f * weight(v) * 0.25f * s);
    touched(mesh_, v);
  }
}

void Stroke::mask(const Vec3& c) {
  const float r = s_.radius, s = s_.strength;
  const float k = std::pow(std::fabs(s), 1.5f);
  const float target = s > 0 ? 1.1f : -0.1f;
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    float t = 1 - 1.3f * length(vx.pos - c) / r;
    if (t <= 0) continue;
    float g = std::sin(t * kPi / 2);
    g = 0.3f * g * g;
    vx.weight = std::clamp(vx.weight + (target - vx.weight) * k * g, 0.0f, 1.0f);
    touched(mesh_, v);
  }
}

void Stroke::grab(const Vec3& c) {
  const float r = 1.5f * s_.radius;
  Vec3 delta = c - anchor_;
  if (s_.global) {
    for (int v : region_) {
      Vertex& vx = mesh_.vertex(v);
      if (!usable(vx) || vx.weight == 0) continue;
      Vec3 d = delta;
      if (vx.onSymmetryPlane) d.x = 0;
      vx.pos = vx.refPos + d;
      mesh_.setDirty(v);
    }
    return;
  }
  if (s_.limit) {
    float l = length(delta);
    if (l > 0) delta = delta / l * (r * std::atan(l / r));
  }
  const float k = std::min(1.0f, (1.2f * std::fabs(s_.strength)) * (1.2f * std::fabs(s_.strength)));
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx)) continue;
    Vec3 d = delta;
    if (vx.onSymmetryPlane) d.x = 0;
    if (s_.limit) {
      float x = 1.1f * length(vx.refPos - anchor_) / r;
      if (x >= 1) continue;
      vx.pos = vx.refPos + d * (smoothFalloff(1 - x) * weight(v));
    } else {
      float x = 1.1f * length(vx.pos - c) / r;
      if (x >= 1) continue;
      vx.pos += d * (smoothFalloff(1 - x) * k * weight(v));
    }
    touched(mesh_, v);
  }
  if (!s_.limit) anchor_ = c;
}

void Stroke::transform(const StrokeInput& in) {
  const float r = 1.5f * s_.radius;
  const float dx = in.mouseX - start_.mouseX, dy = in.mouseY - start_.mouseY;
  const Vec3 fwd = normalize(in.viewDir);
  auto apply = [&](const Vec3& ref) -> Vec3 {
    Vec3 q = ref - anchor_;
    if (s_.tool == Tool::Scale) {
      float sx = std::pow(2.0f, dx / 200), sy = std::pow(2.0f, -dy / 200);
      if (!s_.xyz) return anchor_ + q * ((sx + sy) / 2);
      return anchor_ + in.camRight * (dot(q, in.camRight) * sx) + in.camUp * (dot(q, in.camUp) * sy) +
             fwd * dot(q, fwd);
    }
    // Rotate around the view axis through the pivot.
    float angle = dx * kPi / 200, ca = std::cos(angle), sa = std::sin(angle);
    Vec3 axis = fwd;
    return anchor_ + q * ca + cross(axis, q) * sa + axis * (dot(axis, q) * (1 - ca));
  };
  for (int v : region_) {
    Vertex& vx = mesh_.vertex(v);
    if (!usable(vx) || weight(v) == 0) continue;
    float f = 1;
    if (!s_.global) {
      float t = 1 - 1.3f * length(vx.refPos - anchor_) / r;
      if (t <= 0) continue;
      f = std::sin(std::min(1.0f, 1.5f * t) * kPi / 2) * weight(v);
    }
    Vec3 target = apply(vx.refPos);
    vx.pos = vx.refPos + (target - vx.refPos) * f;
    if (vx.onSymmetryPlane) vx.pos.x = 0;
    mesh_.setDirty(v);
    if (!s_.global) touched(mesh_, v);
  }
}

void Stroke::reduce(const Vec3& c) {
  const float r = s_.radius, s = s_.strength;
  if (s < 0) {
    BrushDetail detail;
    detail.center = c;
    detail.radius = r;
    detail.detailSize = s_.detailSize;
    detail.detailFactor = s_.detailFactor;
    detail.scale = std::fabs(s);
    detail.beautify = false;
    std::vector<int> created;
    tessellateForBrush(mesh_, detail, &region_, &created);
    inRegion_.resize(mesh_.vertexSlots(), 0);
    for (int v : created)
      if (!inRegion_[v]) { inRegion_[v] = 1; region_.push_back(v); }
    return;
  }
  // Phase 1 finds the cheapest edge cost under the brush on the first dab,
  // phase 2 collapses edges cheaper than the adaptive threshold.
  for (int phase = dabs_ == 1 ? 1 : 2; phase <= 2; ++phase) {
    const float threshold = (0.8f * s + 1) * reduceMin_;
    for (int v : region_) {
      const Vertex& vx = mesh_.vertex(v);
      if (!usable(vx) || vx.weight == 0 || length(vx.pos - c) >= r) continue;
      for (int i = 0; i < vx.numEdges; ++i) {
        const int e = vx.edges[i];
        const Edge& edge = mesh_.edge(e);
        const Vertex& a = mesh_.vertex(edge.v[0]);
        const Vertex& b = mesh_.vertex(edge.v[1]);
        float cost = ((1 - dot(a.normal, b.normal)) * 40 + 1) * edge.length;
        reduceMin_ = std::min(reduceMin_, cost);
        if (phase < 2) continue;
        int k = a.numEdges + b.numEdges - 12;
        if (k > 0) cost *= 0.25f * k + 1;
        if (cost < threshold) {
          if (collapseEdge(mesh_, e) >= 0) break;
        }
      }
    }
  }
}

}  // namespace bh

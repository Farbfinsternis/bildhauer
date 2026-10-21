#include "core/paint.h"

#include <algorithm>
#include <cmath>

namespace bh {

namespace {

Vec3 mirrorX(Vec3 v) { return {-v.x, v.y, v.z}; }

uint32_t gray(uint32_t g) { return g * 0x010101u; }

}  // namespace

// --- texels -------------------------------------------------------------------

Texels bakeTexels(const Mesh& m, const TextureMapping& map, int width, int height) {
  Texels tx;
  tx.width = width;
  tx.height = height;
  const size_t n = size_t(width) * height;
  tx.triangle.assign(n, -1);
  tx.pos.assign(n, {});
  tx.normal.assign(n, {});
  // Per texel: distance of its center inside the triangle that owns it, in
  // texels (negative: outside, at most one texel). The deepest triangle wins.
  std::vector<float> depth(n, -1e30f);

  const int nt = std::min(m.triangleSlots(), int(map.corners.size()));
  for (int t = 0; t < nt; ++t) {
    const Triangle& tri = m.triangle(t);
    if (!tri.active || t >= int(map.chart.size()) || map.chart[t] < 0) continue;
    float px[3], py[3];
    Vec3 p[3], nrm[3];
    bool ok = true;
    for (int k = 0; k < 3; ++k) {
      int c = map.corners[t][k];
      if (c < 0 || c >= int(map.texverts.size())) { ok = false; break; }
      px[k] = map.texverts[c].u * width;
      py[k] = map.texverts[c].v * height;
      p[k] = m.vertex(tri.v[k]).pos;
      nrm[k] = m.vertex(tri.v[k]).normal;
    }
    if (!ok) continue;
    const float area2 = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
    if (std::fabs(area2) < 1e-9f) continue;
    // Height of the triangle over each edge (opposite corner k), in texels.
    float h[3];
    for (int k = 0; k < 3; ++k) {
      int a = (k + 1) % 3, b = (k + 2) % 3;
      float len = std::hypot(px[b] - px[a], py[b] - py[a]);
      h[k] = len > 0 ? std::fabs(area2) / len : 0;
    }
    const int x0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]}) - 1.5f)));
    const int x1 = std::min(width - 1, int(std::ceil(std::max({px[0], px[1], px[2]}) + 1.5f)));
    const int y0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]}) - 1.5f)));
    const int y1 = std::min(height - 1, int(std::ceil(std::max({py[0], py[1], py[2]}) + 1.5f)));
    for (int y = y0; y <= y1; ++y) {
      const float qy = y + 0.5f;
      for (int x = x0; x <= x1; ++x) {
        const float qx = x + 0.5f;
        float l[3];
        l[0] = ((px[1] - qx) * (py[2] - qy) - (px[2] - qx) * (py[1] - qy)) / area2;
        l[1] = ((px[2] - qx) * (py[0] - qy) - (px[0] - qx) * (py[2] - qy)) / area2;
        l[2] = 1 - l[0] - l[1];
        const float inside = std::min({l[0] * h[0], l[1] * h[1], l[2] * h[2]});
        const size_t i = size_t(y) * width + x;
        if (inside < -1 || inside <= depth[i]) continue;
        depth[i] = inside;
        // Outside texels take the nearest point on the triangle (roughly).
        float sum = 0;
        for (float& w : l) sum += (w = std::max(w, 0.0f));
        for (float& w : l) w /= sum;
        tx.triangle[i] = t;
        tx.pos[i] = p[0] * l[0] + p[1] * l[1] + p[2] * l[2];
        tx.normal[i] = normalize(nrm[0] * l[0] + nrm[1] * l[1] + nrm[2] * l[2]);
      }
    }
  }
  for (int t : tx.triangle) tx.covered += t >= 0;
  return tx;
}

void TexelGrid::build(const Texels& tx) {
  texels_ = &tx;
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (size_t i = 0; i < tx.triangle.size(); ++i) {
    if (tx.triangle[i] < 0) continue;
    const Vec3& p = tx.pos[i];
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  if (lo.x > hi.x) lo = hi = {};
  origin_ = lo;
  const Vec3 ext = hi - lo;
  cell_ = std::max(length(ext) / 128, 1e-6f);
  for (;;) {
    nx_ = int(ext.x / cell_) + 1;
    ny_ = int(ext.y / cell_) + 1;
    nz_ = int(ext.z / cell_) + 1;
    if (double(nx_) * ny_ * nz_ <= 4e6) break;
    cell_ *= 1.25f;
  }
  auto cellOf = [&](const Vec3& p) {
    int x = std::clamp(int((p.x - origin_.x) / cell_), 0, nx_ - 1);
    int y = std::clamp(int((p.y - origin_.y) / cell_), 0, ny_ - 1);
    int z = std::clamp(int((p.z - origin_.z) / cell_), 0, nz_ - 1);
    return (z * ny_ + y) * nx_ + x;
  };
  start_.assign(size_t(nx_) * ny_ * nz_ + 1, 0);
  for (size_t i = 0; i < tx.triangle.size(); ++i)
    if (tx.triangle[i] >= 0) ++start_[cellOf(tx.pos[i]) + 1];
  for (size_t c = 1; c < start_.size(); ++c) start_[c] += start_[c - 1];
  items_.assign(start_.back(), 0);
  std::vector<int> fill(start_.begin(), start_.end() - 1);
  for (size_t i = 0; i < tx.triangle.size(); ++i)
    if (tx.triangle[i] >= 0) items_[fill[cellOf(tx.pos[i])]++] = int(i);
}

void TexelGrid::query(const Vec3& c, float radius, std::vector<int>& out) const {
  if (!texels_ || items_.empty()) return;
  auto range = [&](float v, float o, int n, int& a, int& b) {
    a = std::clamp(int(std::floor((v - radius - o) / cell_)), 0, n - 1);
    b = std::clamp(int(std::floor((v + radius - o) / cell_)), 0, n - 1);
  };
  int x0, x1, y0, y1, z0, z1;
  range(c.x, origin_.x, nx_, x0, x1);
  range(c.y, origin_.y, ny_, y0, y1);
  range(c.z, origin_.z, nz_, z0, z1);
  const float r2 = radius * radius;
  for (int z = z0; z <= z1; ++z)
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        const int cell = (z * ny_ + y) * nx_ + x;
        for (int k = start_[cell]; k < start_[cell + 1]; ++k) {
          const int i = items_[k];
          const Vec3 d = texels_->pos[i] - c;
          if (dot(d, d) <= r2) out.push_back(i);
        }
      }
}

// --- canvas -------------------------------------------------------------------

const char* paintToolName(PaintTool tool) {
  static const char* names[] = {"Paint color", "Paint bump", "Flatten bump"};
  return names[int(tool)];
}

Canvas::Canvas(const Mesh& mesh, const TextureMapping& map, int width, int height)
    : texels_(bakeTexels(mesh, map, width, height)), symmetric_(mesh.symmetric) {
  grid_.build(texels_);
  const size_t n = size_t(width) * height;
  color_.resize(n);
  r16_.resize(n);
  g16_.resize(n);
  b16_.resize(n);
  heights_.assign(n, kNeutralHeight);
  normals_.resize(n * 3);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const size_t i = size_t(y) * width + x;
      const uint32_t g = texels_.triangle[i] < 0 ? 0x66 : (((x >> 3) + (y >> 3)) & 1) ? 0x57 : 0x50;
      color_[i] = gray(g);
      setColor16(int(i), color_[i]);
    }
  tilesX_ = (width + kTileSize - 1) / kTileSize;
  tilesY_ = (height + kTileSize - 1) / kTileSize;
  changed_.assign(size_t(tilesX_) * tilesY_, 0);
  savedIn_.assign(changed_.size(), 0);
  for (int t = 0; t < tilesX_ * tilesY_; ++t) updateNormals(t);
  seen_.assign(n, 0);
}

bool Canvas::loadImages(const std::vector<int32_t>& color, const std::vector<int32_t>& bump, std::string* error) {
  const size_t n = color_.size();
  if (color.size() != n || bump.size() != n) {
    if (error) *error = "image size does not match the canvas";
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    color_[i] = uint32_t(color[i]);
    setColor16(int(i), color_[i]);
    heights_[i] = bump[i];
  }
  for (int t = 0; t < tilesX_ * tilesY_; ++t) updateNormals(t);
  undo_.clear();
  return true;
}

void Canvas::setColor16(int i, uint32_t c) {
  r16_[i] = uint16_t((c & 0xFF) * 257);
  g16_[i] = uint16_t((c >> 8 & 0xFF) * 257);
  b16_[i] = uint16_t((c >> 16 & 0xFF) * 257);
}

void Canvas::dab(const Vec3& center, const PaintSettings& s) {
  const float r = s.radius;
  if (r <= 0) return;
  const float reach = 1.3f * r;
  const float k = std::copysign(std::min(1.0f, 0.6f * std::fabs(s.strength)), s.strength);
  const float hard = s.hardness * s.hardness * 0.95f;
  const int w = texels_.width;

  found_.clear();
  grid_.query(center, reach, found_);
  if (symmetric_) grid_.query(mirrorX(center), reach, found_);
  if (++dabStamp_ == 0) {
    std::fill(seen_.begin(), seen_.end(), 0);
    dabStamp_ = 1;
  }
  hits_.clear();
  hitX_.clear();
  hitN_.clear();
  Vec3 mean;
  for (int i : found_) {
    if (seen_[i] == dabStamp_) continue;
    seen_[i] = dabStamp_;
    const Vec3& p = texels_.pos[i];
    float d = length(p - center);
    Vec3 n = texels_.normal[i];
    if (symmetric_) {
      float dm = length(mirrorX(p) - center);
      if (dm < d) {
        d = dm;
        n = mirrorX(n);
      }
    }
    if (d > reach) continue;
    const float x = 2 * d / (3 * r);
    hits_.push_back(i);
    hitX_.push_back(x);
    hitN_.push_back(n);
    mean += n * (1 - x);
  }
  // Stamp normal for the surface angle falloff: the mean texel normal.
  const Vec3 stampN = normalize(mean);
  const bool angle = s.angleFalloff && length(stampN) > 0;

  const uint32_t target = k > 0 ? s.primary : s.secondary;
  const int64_t t16[3] = {(target & 0xFF) * 257, (target >> 8 & 0xFF) * 257, (target >> 16 & 0xFF) * 257};
  const int64_t alpha16 = std::lround(std::fabs(k) * 255) * 257;

  for (size_t h = 0; h < hits_.size(); ++h) {
    float f = 1;
    if (hard < 1) f = smoothFalloff((1 - hitX_[h]) / (1 - hard));
    if (angle) {
      const float a = (dot(hitN_[h], stampN) - 0.15f) * 3;
      if (a < 0) continue;
      if (a < 1) f *= a;
    }
    if (f <= 0) continue;
    const int i = hits_[h];
    touchTile((i / w / kTileSize) * tilesX_ + (i % w) / kTileSize);
    switch (s.tool) {
      case PaintTool::Color: {
        const int64_t amount = (alpha16 * std::lround(f * 32768)) >> 15;
        uint16_t* ch[3] = {&r16_[i], &g16_[i], &b16_[i]};
        for (int c = 0; c < 3; ++c) *ch[c] = uint16_t(*ch[c] + (((t16[c] - *ch[c]) * amount) >> 16));
        color_[i] = (color_[i] & 0xFF000000u) | uint32_t(r16_[i] >> 8) | uint32_t(g16_[i] >> 8) << 8 |
                    uint32_t(b16_[i] >> 8) << 16;
        break;
      }
      case PaintTool::Bump: {
        const int64_t v = heights_[i] + int64_t(std::lround(f * k * 24576)) * 255 / 256;
        heights_[i] = int32_t(std::clamp<int64_t>(v, 0, INT32_MAX));
        break;
      }
      case PaintTool::FlattenBump: {
        const int64_t v = heights_[i] + ((std::lround(f * k * 255) * (int64_t(kNeutralHeight) - heights_[i])) >> 8);
        heights_[i] = int32_t(std::clamp<int64_t>(v, 0, INT32_MAX));
        break;
      }
    }
  }
}

void Canvas::touchTile(int tile) {
  if (!undo_.empty() && savedIn_[tile] != step_) {
    savedIn_[tile] = step_;
    SavedTile saved{tile, {}, {}};
    const int x0 = (tile % tilesX_) * kTileSize, y0 = (tile / tilesX_) * kTileSize;
    const int x1 = std::min(x0 + kTileSize, width()), y1 = std::min(y0 + kTileSize, height());
    for (int y = y0; y < y1; ++y) {
      const size_t row = size_t(y) * width();
      saved.color.insert(saved.color.end(), color_.begin() + row + x0, color_.begin() + row + x1);
      saved.heights.insert(saved.heights.end(), heights_.begin() + row + x0, heights_.begin() + row + x1);
    }
    undo_.back().push_back(std::move(saved));
  }
  markChanged(tile);
}

void Canvas::markChanged(int tile) {
  if (changed_[tile]) return;
  changed_[tile] = 1;
  changedList_.push_back(tile);
}

void Canvas::updateNormals(int tile) {
  const int w = width(), h = height();
  const int x0 = (tile % tilesX_) * kTileSize, y0 = (tile / tilesX_) * kTileSize;
  const int x1 = std::min(x0 + kTileSize, w), y1 = std::min(y0 + kTileSize, h);
  const double dz = 10.0 * (1 << 20) / w;
  // A neighbor outside every chart keeps the neutral height; using it would
  // put a cliff along each chart border.
  auto at = [&](size_t i, bool inside, double c) { return inside && texels_.triangle[i] >= 0 ? heights_[i] : c; };
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const size_t i = size_t(y) * w + x;
      const double c = heights_[i];
      const double dx = c - at(i + 1, x + 1 < w, c);
      const double dy = c - at(i + w, y + 1 < h, c);
      const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
      normals_[i * 3] = uint8_t(128 + 127 * dx / len);
      normals_[i * 3 + 1] = uint8_t(128 + 127 * dy / len);
      normals_[i * 3 + 2] = uint8_t(128 + 127 * dz / len);
    }
}

std::vector<int> Canvas::takeChangedTiles() {
  // The normals of a tile's last column and row read the next tile, so the
  // tiles left of and above a changed tile change too.
  const size_t direct = changedList_.size();
  for (size_t k = 0; k < direct; ++k) {
    const int t = changedList_[k];
    if (t % tilesX_ > 0) markChanged(t - 1);
    if (t / tilesX_ > 0) markChanged(t - tilesX_);
  }
  std::vector<int> out;
  out.swap(changedList_);
  for (int t : out) {
    changed_[t] = 0;
    updateNormals(t);
  }
  return out;
}

void Canvas::beginUndoStep() {
  ++step_;
  undo_.emplace_back();
  if (undo_.size() > 30) undo_.pop_front();
}

bool Canvas::undo() {
  while (!undo_.empty() && undo_.back().empty()) undo_.pop_back();
  if (undo_.empty()) return false;
  for (const SavedTile& saved : undo_.back()) {
    const int x0 = (saved.tile % tilesX_) * kTileSize, y0 = (saved.tile / tilesX_) * kTileSize;
    const int x1 = std::min(x0 + kTileSize, width()), y1 = std::min(y0 + kTileSize, height());
    size_t k = 0;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x, ++k) {
        const size_t i = size_t(y) * width() + x;
        color_[i] = saved.color[k];
        setColor16(int(i), color_[i]);
        heights_[i] = saved.heights[k];
      }
    savedIn_[saved.tile] = 0;
    markChanged(saved.tile);
  }
  undo_.pop_back();
  return true;
}

// --- stroke -------------------------------------------------------------------

PaintStroke::PaintStroke(Canvas& canvas, const PaintSettings& settings, const Vec3& start)
    : canvas_(canvas), s_(settings), path_(start) {
  canvas_.beginUndoStep();
  moveTo(start, true);
}

void PaintStroke::moveTo(const Vec3& point, bool hit) {
  if (!hit) return;  // off the model: wait until the pointer is back
  path_.moveTo(point, s_.spacingFactor * s_.radius, [&](const Vec3& q) { canvas_.dab(q, s_); });
}

}  // namespace bh

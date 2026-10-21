#pragma once

// Paint mode (re/docs/paint.md 2-3): texels baked from the texture mapping,
// the color and bump layers, and paint strokes.
//
// Texels: every texel whose center lies on a triangle in UV space, or within
// one texel of it (so filtering at chart borders reads painted texels), gets
// the 3D position and the interpolated vertex normal of that point. A grid
// over the texel positions answers the dab's range query.
//
// Layers as in Sculptris: the color layer keeps 16-bit channels beside its
// 8-bit RGBA image, so small steps accumulate; the bump layer holds 32-bit
// heights around 2^28 and a normal map derived from them. The texture is
// split into 64 x 64 tiles for uploading and undo.
//
// Deviations where the docs leave things open or Sculptris overflows: heights
// are clamped to [0, 2^31) instead of wrapping; with symmetry (one texture for
// both halves) a texel is painted from the nearer of its two images; bump
// normals ignore neighbors outside the charts. Not yet
// implemented: brush mask/texture, heightmask, paint mask, Combine color and
// bump, Airbrush, Fill/Clear.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/dab_path.h"
#include "core/mesh.h"
#include "core/texmap.h"

namespace bh {

constexpr int kTileSize = 64;
constexpr int32_t kNeutralHeight = 1 << 28;

struct Texels {
  int width = 0, height = 0;
  std::vector<int> triangle;      // per texel: triangle slot, -1 = not covered
  std::vector<Vec3> pos, normal;  // per texel, valid where covered
  int covered = 0;
};

// Rasterizes every mapped triangle into a width x height texture.
Texels bakeTexels(const Mesh& mesh, const TextureMapping& map, int width, int height);

// Uniform grid over the covered texels.
class TexelGrid {
 public:
  void build(const Texels& texels);
  // Appends the texels within `radius` of c.
  void query(const Vec3& c, float radius, std::vector<int>& out) const;

 private:
  const Texels* texels_ = nullptr;
  Vec3 origin_;
  float cell_ = 1;
  int nx_ = 0, ny_ = 0, nz_ = 0;
  std::vector<int> start_, items_;  // items_ sorted by cell, start_ per cell + 1
};

enum class PaintTool : int { Color, Bump, FlattenBump };

const char* paintToolName(PaintTool tool);

struct PaintSettings {
  PaintTool tool = PaintTool::Color;
  float radius = 0.1f;    // world units
  float strength = 1.0f;  // signed s (brush_params.h); negative = inverted
  float hardness = 0.5f;  // slider, [0, 1]
  uint32_t primary = 0x2266DD;    // 0x00BBGGRR; Sculptris starts with RGB 221, 102, 34
  uint32_t secondary = 0xDD6622;  // RGB 34, 102, 221
  float spacingFactor = 0.25f;
  bool angleFalloff = true;  // "Surface angle falloff"
};

class Canvas {
 public:
  // A fresh canvas: covered texels get Sculptris' start pattern (gray 0x50 /
  // 0x57 in 8 x 8 checks), the rest gray 0x66; heights neutral.
  Canvas(const Mesh& mesh, const TextureMapping& map, int width, int height);

  // Replaces both layers with stored images (.sc1 texturing block: color
  // 0xAABBGGRR, bump heights), each width x height.
  bool loadImages(const std::vector<int32_t>& color, const std::vector<int32_t>& bump, std::string* error);

  int width() const { return texels_.width; }
  int height() const { return texels_.height; }
  const Texels& texels() const { return texels_; }
  const std::vector<uint32_t>& colorImage() const { return color_; }  // 0xAABBGGRR
  const std::vector<int32_t>& heights() const { return heights_; }
  const std::vector<uint8_t>& normalImage() const { return normals_; }  // RGB, see paint.md 3.4

  // One dab at a visible surface point (mirror side included).
  void dab(const Vec3& center, const PaintSettings& s);

  // Tiles changed since the last call, with their normal map brought up to
  // date. Tile t covers texels from (t % tilesX, t / tilesX) * kTileSize.
  std::vector<int> takeChangedTiles();
  int tilesX() const { return tilesX_; }
  int tilesY() const { return tilesY_; }

  // Undo works in steps; a stroke is one step. Tiles are saved before their
  // first change in a step.
  void beginUndoStep();
  bool undo();

 private:
  void touchTile(int tile);
  void markChanged(int tile);
  void updateNormals(int tile);
  void setColor16(int texel, uint32_t rgba);

  Texels texels_;
  TexelGrid grid_;
  bool symmetric_ = false;

  std::vector<uint32_t> color_;
  std::vector<uint16_t> r16_, g16_, b16_;
  std::vector<int32_t> heights_;
  std::vector<uint8_t> normals_;

  int tilesX_ = 0, tilesY_ = 0;
  std::vector<char> changed_;
  std::vector<int> changedList_;

  struct SavedTile {
    int tile;
    std::vector<uint32_t> color;
    std::vector<int32_t> heights;
  };
  std::deque<std::vector<SavedTile>> undo_;
  std::vector<uint32_t> savedIn_;  // per tile: undo step it was saved in
  uint32_t step_ = 0;

  // Dab scratch, per texel / per hit.
  std::vector<uint32_t> seen_;
  uint32_t dabStamp_ = 0;
  std::vector<int> found_, hits_;
  std::vector<float> hitX_;
  std::vector<Vec3> hitN_;
};

class PaintStroke {
 public:
  // `start` is the picked visible point. Opens an undo step.
  PaintStroke(Canvas& canvas, const PaintSettings& settings, const Vec3& start);
  void moveTo(const Vec3& point, bool hit);
  void setRadius(float radius) { s_.radius = radius; }

 private:
  Canvas& canvas_;
  PaintSettings s_;
  DabPath path_;
};

}  // namespace bh

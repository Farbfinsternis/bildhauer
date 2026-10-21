#pragma once

// Texture mapping for paint mode (re/docs/paint.md): each triangle belongs to
// a chart (Sculptris: patch) and has one texture vertex per corner; a texvert
// is shared within its chart, so a vertex on a chart border has one texvert per
// adjacent chart. UVs are in [0, 1] of a square texture.
//
// bildhauer builds its own atlas (Sculptris' patch growing and packing are not
// reconstructed, see paint.md 2): charts grow over neighbors whose normals stay
// within a cone of the chart normal, each chart is projected onto its own plane
// (like a Sculptris patch), turned along its principal axis, and the charts are
// shelf-packed at one common texel density.

#include <array>
#include <vector>

#include "core/mesh.h"

namespace bh {

struct TexVert {
  float u = 0, v = 0;
  int vertex = -1;
};

// Projection frame of a chart, saved in .sc1 as a Sculptris patch: where
// the chart starts in UV, its 3D center, and its axes (in-plane x and y as
// laid out in the texture, then the normal).
struct ChartFrame {
  Vec3 uvOrigin, center;
  std::array<Vec3, 3> axes;
};

struct TextureMapping {
  int size = 0;  // texture width and height in texels
  std::vector<TexVert> texverts;
  std::vector<std::array<int, 3>> corners;  // per triangle slot: texvert per corner, -1 = none
  std::vector<int> chart;                   // per triangle slot: chart id, -1 = none
  int numCharts = 0;
  std::vector<ChartFrame> frames;           // per chart

  bool empty() const { return texverts.empty(); }
};

struct AtlasSettings {
  int size = 1024;          // texture size in texels
  float maxAngle = 50.0f;   // degrees between a triangle normal and its chart normal
  int padding = 4;          // texels of space around every chart
  int minChartTris = 6;     // smaller charts are merged into a neighbor if possible
};

TextureMapping buildAtlas(const Mesh& mesh, const AtlasSettings& settings = {});

struct AtlasStats {
  int charts = 0;
  int unmapped = 0;      // active triangles without chart or texverts
  int outOfRange = 0;    // texverts outside [0, 1]
  int flipped = 0;       // triangles whose UV winding disagrees with their chart
  int overlaps = 0;      // pairs of charts whose UV bounding boxes overlap
  float coverage = 0;    // UV area of all triangles / texture area
  float maxStretch = 0;  // texel density sqrt(UV area / mesh area): largest deviation from the median, as a factor
};

// Tangent frame per texvert for bump shading: t is the unit direction of
// +u, made orthogonal to the vertex normal; the direction of +v is
// w * cross(normal, t), w = +-1 (UV winding).
struct Tangent {
  Vec3 t;
  float w = 1;
};

std::vector<Tangent> computeTangents(const Mesh& mesh, const TextureMapping& map);

// Consistency check and quality figures of a mapping.
AtlasStats atlasStats(const Mesh& mesh, const TextureMapping& map);

}  // namespace bh

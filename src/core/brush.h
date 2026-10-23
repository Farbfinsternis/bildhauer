#pragma once

// Sculpting brushes (re/docs/brushes.md). A Stroke stores the reference state
// when it starts and turns pointer movement into dabs spaced along a smooth
// path (dab_path.h); each dab tessellates under the brush, applies the
// tool, and updates normals. Beautify is not part of a dab: like Sculptris,
// the caller runs one beautifyPass per frame while vertices are dirty.
//
// Deviations from Sculptris where the docs leave things open: the work region
// is a sphere around the dab (twice the brush reach) instead of a box, and
// Rotate turns by the horizontal mouse movement.
//
// Deliberate deviation: symmetry compensation. Sculptris maps every dab onto
// the stored half and halves both contributions where the brush overlaps its
// mirror image. A stroke across the plane is thereby folded: everything beside
// the plane is passed twice, the plane once, and the line dips there. Here a
// vertex counts per stroke what it received from dabs on its own side and
// from mirrored dabs, and only the larger of the two takes effect (Draw,
// Inflate, Pinch, Crease, Flatten).
//
// The surface normal under the brush (stroke +0x68: the brush mask's frame,
// Flatten's plane weighting) is taken per dab around the dab center, from
// the vertex normals within r weighted by 1 - d/r (Sculptris: per frame at
// the cursor, from the depth buffer). Draw's mirror pass samples a mirrored
// brush mask frame (Sculptris keeps the axes).

#include <random>

#include "core/dab_path.h"
#include "core/mesh.h"
#include "core/paint.h"

namespace bh {

enum class Tool : int { Draw, Smooth, Grab, Inflate, Flatten, Pinch, Mask, Crease, Rotate, Scale, Reduce };

const char* toolName(Tool tool);

struct BrushSettings {
  Tool tool = Tool::Draw;
  float radius = 0.1f;      // world units
  float strength = 0.5f;    // signed; negative = inverted
  float detailSize = 0.1f;  // tessellation threshold base (stroke +0xB0); Sculptris: = radius
  float detailFactor = 1.1f;
  float spacingFactor = 0.25f;  // distance between dabs / radius ("Brush spacing")

  bool clay = false, soft = false;              // Draw
  bool lockPlane = false, angleFalloff = false; // Flatten
  bool global = false, limit = false, xyz = false;  // Grab, Rotate, Scale
  bool respectMask = true;
  bool compensateSymmetry = true;

  // Brush mask: Draw, Smooth and Flatten take the stamp's alpha as their
  // falloff. Null: no mask.
  const Stamp* stamp = nullptr;
  StampMode stampMode = StampMode::Fixed;
  float stampAngle = 0;  // radians, for StampMode::Fixed
};

struct StrokeInput {
  Vec3 point;            // picked point on the visible surface, mirrored half included
                         // (ignored by Grab/Rotate/Scale)
  bool hit = true;       // false: the pointer is off the model; surface tools skip it
  Vec3 rayOrigin, rayDir;  // pointer ray
  Vec3 viewDir;          // camera forward
  Vec3 camRight, camUp;  // camera axes
  float mouseX = 0, mouseY = 0;  // pixels, y down
};

// Time spent per dab phase, in milliseconds (summed over the stroke).
struct DabTimes {
  double tessellate = 0, tool = 0, geometry = 0;
  double worstDab = 0;
};

class Stroke {
 public:
  Stroke(Mesh& mesh, const BrushSettings& settings, const StrokeInput& start);
  void moveTo(const StrokeInput& input);
  // The radius follows the view depth under the pointer during the stroke;
  // dab spacing scales with it.
  void setSize(float radius, float detailSize);
  int dabCount() const { return dabs_; }
  const DabTimes& times() const { return times_; }

 private:
  void dab(Vec3 c, const StrokeInput& in);
  Vec3 dragPoint(const StrokeInput& in) const;

  void draw(const Vec3& c, bool mirror);
  void smooth(const Vec3& c);
  void grab(const Vec3& c);
  void inflate(const Vec3& c, bool mirror);
  void flatten(const Vec3& c, bool mirror, const Vec3& toEye);
  void pinch(const Vec3& c, bool mirror);
  void mask(const Vec3& c);
  void crease(const Vec3& c, bool mirror);
  void transform(const StrokeInput& in);
  void reduce(const Vec3& c);

  float weight(int v) const;
  void updateRegion(const Vec3& c);
  // The surface normal under the dab at c (stored half): the vertex normals
  // within r weighted by 1 - d/r; the viewer if there are none.
  Vec3 surfaceNormal(const Vec3& c, const StrokeInput& in) const;
  // The brush mask's frame for the dab at c (stored half); `pointed` is
  // where the dab lies on screen (c, or its mirror image).
  void updateStampFrame(const Vec3& c, const Vec3& pointed, const StrokeInput& in);
  // The mask value of a vertex at p for a frame centered at `center`, or -1
  // without a mask.
  float stampValue(const Vec3& p, const Vec3& center, const StampFrame& frame) const;
  // The part of falloff `f` that vertex v still gets in this stroke: the
  // growth of max(direct, mirrored) coverage. `mirrorSide`: the contribution
  // comes from the other half of the model.
  float covered(int v, float f, bool mirrorSide);
  void inheritCoverage(const std::vector<int>& created);

  Mesh& mesh_;
  BrushSettings s_;
  StrokeInput start_;
  DabPath path_;
  int dabs_ = 0;
  DabTimes times_;

  Vec3 anchor_;       // Grab/Rotate/Scale pivot
  Vec3 planeN_, planeP_;  // locked Flatten plane
  bool planeLocked_ = false;
  float reduceMin_ = 1e5f;

  bool dabMirrored_ = false;  // current dab was mapped from the mirrored half

  bool stampOn_ = false;    // the current dab uses the brush mask
  StampFrame stampFrame_;   // for the stored half
  Vec3 stampDirection_, lastPointed_;
  bool hasLastPointed_ = false;
  std::mt19937 random_{12345};
  std::vector<float> coverDirect_, coverMirror_;  // per vertex, this stroke

  // Work region: the vertices within regionRadius_ of regionCenter_ (and of
  // its mirror image with symmetry). Rebuilt only when the brush reach leaves
  // it; all tool loops run over it.
  std::vector<int> region_;
  std::vector<char> inRegion_;
  Vec3 regionCenter_;
  float regionRadius_ = 0;
  bool regionValid_ = false;
};

}  // namespace bh

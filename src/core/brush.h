#pragma once

// Sculpting brushes (re/docs/brushes.md). A Stroke stores the reference state
// when it starts and turns pointer movement into evenly spaced dabs; each
// dab tessellates under the brush, applies the tool, and updates normals and
// beautify.
//
// Deviations from Sculptris where the docs leave things open: no work-region
// box (all vertices are tested), linear dab interpolation, dab spacing and
// detail size are plain settings, and Rotate turns by the horizontal mouse
// movement.

#include "core/mesh.h"

namespace bh {

enum class Tool : int { Draw, Smooth, Grab, Inflate, Flatten, Pinch, Mask, Crease, Rotate, Scale, Reduce };

const char* toolName(Tool tool);

struct BrushSettings {
  Tool tool = Tool::Draw;
  float radius = 0.1f;      // world units
  float strength = 0.5f;    // signed; negative = inverted
  float detailSize = 0.05f; // tessellation threshold base (stroke +0xB0)
  float detailFactor = 1.1f;
  float spacing = 0;        // distance between dabs; 0 = radius / 4

  bool clay = false, soft = false;              // Draw
  bool lockPlane = false, angleFalloff = false; // Flatten
  bool global = false, limit = false, xyz = false;  // Grab, Rotate, Scale
  bool respectMask = true;
  bool compensateSymmetry = true;
  bool beautify = true;
};

struct StrokeInput {
  Vec3 point;            // picked surface point (ignored by Grab/Rotate/Scale)
  Vec3 rayOrigin, rayDir;  // pointer ray
  Vec3 viewDir;          // camera forward
  Vec3 camRight, camUp;  // camera axes
  float mouseX = 0, mouseY = 0;  // pixels, y down
};

class Stroke {
 public:
  Stroke(Mesh& mesh, const BrushSettings& settings, const StrokeInput& start);
  void moveTo(const StrokeInput& input);
  int dabCount() const { return dabs_; }

 private:
  void dab(Vec3 c, const StrokeInput& in);
  Vec3 dragPoint(const StrokeInput& in) const;

  void draw(const Vec3& c, bool mirror);
  void smooth(const Vec3& c);
  void grab(const Vec3& c);
  void inflate(const Vec3& c, bool mirror);
  void flatten(const Vec3& c, bool mirror, const Vec3& viewDir);
  void pinch(const Vec3& c, bool mirror);
  void mask(const Vec3& c);
  void crease(const Vec3& c, bool mirror);
  void transform(const StrokeInput& in);
  void reduce(const Vec3& c);

  float weight(int v) const;

  Mesh& mesh_;
  BrushSettings s_;
  StrokeInput start_;
  Vec3 lastPoint_, lastDab_;
  bool hasDab_ = false;
  int dabs_ = 0;

  Vec3 anchor_;       // Grab/Rotate/Scale pivot
  Vec3 planeN_, planeP_;  // locked Flatten plane
  bool planeLocked_ = false;
  float reduceMin_ = 1e5f;
};

}  // namespace bh

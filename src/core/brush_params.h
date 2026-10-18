#pragma once

// Brush parameters from the UI sliders and the dab path (re/docs/brushes.md
// 2.3 and 2.4). Sliders are in [0, 1]; the results are world units for the
// radius and the signed strength `s` the brushes work with.

#include <cmath>

#include "core/brush.h"

namespace bh {

struct ToolSliders {
  float size = 0.3f;
  float strength = 1.0f;
};

// Slider values of a fresh session: size 0.3 (Smooth 0.2), strength 1.0
// (Smooth and Flatten 0.4).
inline ToolSliders defaultSliders(Tool tool) {
  if (tool == Tool::Smooth) return {0.2f, 0.4f};
  if (tool == Tool::Flatten) return {0.3f, 0.4f};
  return {};
}

// Camera angle of the FOV slider, in degrees (35 at the default 0.5).
inline float fovDegrees(float fovSlider) { return float(int(fovSlider * 40 + 15)); }

// Brush radius at view depth `depth` (distance along the view axis): constant
// on screen, slightly corrected for the field of view.
inline float brushRadius(float size, float depth, float fovSlider = 0.5f) {
  const float a = 1.7f * std::pow(size, 1.9f) + 0.03f;
  return depth * 0.12f * a * std::pow(fovSlider + 0.5f, 1.45f);
}

// Signed brush strength of the Strength slider: 0.13 ... 1.97.
inline float brushStrength(float strength) { return 1.15f * (strength + 0.3f) * (strength + 0.3f) + 0.03f; }

// Pen pressure shaped by the "Pressure sensitivity" slider (default 0.35).
inline float pressureCurve(float pressure, float sensitivity = 0.35f) {
  return std::pow(pressure, 1 - (sensitivity - 0.5f) * 0.8f);
}

// Dab distance as a fraction of the radius, from the "Brush spacing" slider.
inline float spacingFactor(float slider) { return 4.95f * slider * slider + 0.05f; }

// Dab path through the last two stroke samples C (older) and B and the new
// sample A: a cubic Bezier from the midpoint of C-B to the midpoint of B-A
// with its handles pulled toward B, close to the quadratic B-spline of the
// samples.
inline Vec3 dabCurve(const Vec3& c, const Vec3& b, const Vec3& a, float t) {
  const Vec3 k0 = c + (b - c) * 0.5f, k1 = c + (b - c) * 0.85f;
  const Vec3 k2 = a + (b - a) * 0.85f, k3 = a + (b - a) * 0.5f;
  const float u = 1 - t;
  return k0 * (u * u * u) + k1 * (3 * u * u * t) + k2 * (3 * u * t * t) + k3 * (t * t * t);
}

}  // namespace bh

#pragma once

// Viewer state shared by the app modules: the model and its paint data, the
// camera, the tool settings, and what the window shows.

#include <windows.h>

#include <GL/gl.h>

#include <array>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/brush.h"
#include "core/brush_params.h"
#include "core/mesh.h"
#include "core/paint.h"
#include "core/pick.h"
#include "core/texmap.h"
#include "io/sc1.h"

namespace bh::viewer {

constexpr float kPi = 3.14159265f;
constexpr float kFovSlider = 0.5f;
inline const float kFov = fovDegrees(kFovSlider) * kPi / 180.0f;
constexpr int kPaintTextureSize = 1024;

struct Camera {
  Vec3 target;
  float yaw = 0, pitch = 0.2f, distance = 3;

  Vec3 eye() const {
    return target + Vec3{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)} * distance;
  }
  Vec3 forward() const { return normalize(target - eye()); }
  Vec3 right() const { return normalize(cross(forward(), {0, 1, 0})); }
  Vec3 up() const { return cross(right(), forward()); }
};

struct App {
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  int width = 1280, height = 800;

  Mesh mesh;
  Sc1File file;          // keeps extension values for saving
  std::string path;      // file the model came from, empty for a new one
  std::string savePath;  // where Save writes without asking; empty: ask first
  bool modified = false;
  Camera camera;
  float sceneSize = 1;

  BrushSettings brush;
  bool beautify = true;  // "Beautify/relax mesh (always used when adding detail)"
  std::array<ToolSliders, int(Tool::Reduce) + 1> sliders;
  std::unique_ptr<Stroke> stroke;
  std::deque<Mesh> undo;

  // Paint mode (one way, as in Sculptris): texture mapping, layers, and their
  // textures. Sliders per paint tool (size, strength, hardness): size and
  // strength as Sculptris resets the tools with the same IDs (Draw, Smooth,
  // Flatten), hardness 0.5 as its reset value.
  bool paintMode = false;
  TextureMapping texmap;
  std::unique_ptr<Canvas> canvas;
  std::unique_ptr<PaintStroke> paintStroke;
  PaintSettings paint;
  std::array<std::array<float, 3>, 3> paintSliders{};  // per tool: size, strength, hardness
  std::vector<Tangent> tangents;                       // per texvert
  int editSlot = 0;  // what Paint color paints: 0 the color layer, n the mask of material slot n
  bool showBumpNormals = false;
  // Brush mask and brush texture (paint.md 3.6) as loaded (the mask already
  // gray), and the stamps built from them; stampsVersion counts rebuilds.
  Stamp maskImage, textureImage;
  std::string maskPath, texturePath;
  bool maskOn = false, maskInvert = false, textureOn = false;
  StampMode stampMode = StampMode::Fixed;
  float stampAngle = 0;  // degrees
  Stamps stamps;
  int stampsVersion = 0;
  bool uploadAll = false;  // the canvas is new: upload whole textures
  GLuint colorTex = 0, normalTex = 0;
  struct SlotTextures {
    GLuint matcap = 0, mask = 0;
    std::string key;  // matcap name and gray/colored it was made for
  };
  std::array<SlotTextures, kMaxMaterials> slotTex;
  GLuint paintShader = 0;  // 0: no OpenGL 2.0, a stand-in shading is used

  bool wireframe = false;
  int mouseX = 0, mouseY = 0;
  bool orbiting = false, panning = false;
  PickHit hover;
  GLuint matcap = 0;
  std::string status;
};

extern App app;

ToolSliders& sliders(Tool tool);
std::array<float, 3>& paintSliders();  // of the current paint tool

// Brush radius at a visible surface point: proportional to its view depth.
float worldRadius(const Vec3& at);

}  // namespace bh::viewer

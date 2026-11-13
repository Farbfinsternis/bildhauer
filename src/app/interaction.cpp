#include "app/input.h"
#include "app/app.h"
#include "app/document.h"
#include <algorithm>

namespace bh::viewer {

namespace {
StrokeInput pointerRay(int mx, int my) {
  const Camera& c = app.camera;
  float aspect = float(app.viewW) / float(std::max(app.viewH, 1));
  float x = (2.0f * (mx - app.viewX) / app.viewW - 1) * std::tan(kFov / 2) * aspect;
  float y = (1 - 2.0f * (my - app.viewY) / app.viewH) * std::tan(kFov / 2);
  StrokeInput in;
  in.rayOrigin = c.eye();
  in.rayDir = normalize(c.forward() + c.right() * x + c.up() * y);
  in.viewDir = c.forward();
  in.camRight = c.right();
  in.camUp = c.up();
  in.mouseX = float(mx);
  in.mouseY = float(my);
  return in;
}

// Grab, Rotate and Scale follow the pointer; pen pressure would only make the
// held geometry lag behind the pen, so it scales surface brushes alone.
bool transformTool(Tool t) { return t == Tool::Grab || t == Tool::Rotate || t == Tool::Scale; }
}  // namespace

StrokeInput inputAt(int mx, int my, PickHit* hitOut) {
  StrokeInput in = pointerRay(mx, my);
  PickHit h = pick(app.mesh, in.rayOrigin, in.rayDir);
  in.point = h.visiblePoint();
  in.hit = h.hit;
  if (hitOut) *hitOut = h;
  return in;
}

void beginStroke(int mx, int my, bool invert, bool smoothing, float pressure) {
  PickHit h;
  StrokeInput in = inputAt(mx, my, &h);
  if (!h.hit) return;
  // Sculpting matters more than undo: a model beyond the budget still sculpts.
  if (!pushUndo()) app.status = "Model exceeds the undo memory budget; this stroke cannot be undone";
  app.hover = h;
  BrushSettings s = app.brush;
  if (smoothing) s.tool = Tool::Smooth;
  s.radius = worldRadius(in.point);
  s.detailSize = s.radius;
  s.strength = brushStrength(sliders(s.tool).strength);
  // Mask paints toward weight 0 by default (negative strength).
  if (s.tool == Tool::Mask) s.strength = -std::fabs(s.strength);
  if (invert) s.strength = -s.strength;
  app.strokeStrength = s.strength;
  if (!transformTool(s.tool)) s.strength *= std::clamp(pressure, 0.0f, 1.0f);
  s.stamp = sculptMaskActive() ? &app.sculptStamp : nullptr;
  s.stampMode = app.stampMode;
  s.stampAngle = app.stampAngle * kPi / 180;
  app.stroke = std::make_unique<Stroke>(app.mesh, s, in);
  app.modified = true;
}

void beginPaintStroke(int mx, int my, bool invert, float pressure) {
  StrokeInput in = inputAt(mx, my);
  if (!in.hit) return;
  const std::array<float, 3>& ps = paintSliders();
  PaintSettings s = app.paint;
  s.radius = worldRadius(in.point);
  s.strength = brushStrength(ps[1]) * (invert ? -1.0f : 1.0f);
  app.strokeStrength = s.strength;
  s.strength *= std::clamp(pressure, 0.0f, 1.0f);
  s.hardness = ps[2];
  s.target = app.editSlot;
  // The view the stroke is painted from: the screen axes turn a fixed stamp;
  // stamp and angle falloff face the surface (PaintStroke), the viewer only
  // where no texel is near.
  s.toViewer = -app.camera.forward();
  s.screenRight = app.camera.right();
  s.screenUp = app.camera.up();
  s.stamps = stampsActive() ? &app.stamps : nullptr;
  s.stampTexture = app.textureOn && !app.textureImage.empty();
  s.stampMode = app.stampMode;
  s.stampAngle = app.stampAngle * kPi / 180;
  app.paintStroke = std::make_unique<PaintStroke>(*app.canvas, s, in.point);
  app.modified = true;
}

void setTool(Tool t) {
  if (app.brush.tool == t) {
    if (t == Tool::Draw) app.brush.clay = !app.brush.clay;
    if (t == Tool::Flatten) app.brush.lockPlane = !app.brush.lockPlane;
    if (t == Tool::Grab || t == Tool::Rotate || t == Tool::Scale) app.brush.global = !app.brush.global;
  }
  app.brush.tool = t;
}

void moveStroke(int x, int y, float pressure) {
  const Tool tool = app.stroke ? app.stroke->tool() : app.brush.tool;
  const bool transform = app.stroke && transformTool(tool);
  StrokeInput in = transform ? pointerRay(x,y) : inputAt(x, y, &app.hover);
  if (transform) {
    const Vec3 p = app.stroke->dragTarget(in);
    app.hover.mirrored = app.mesh.symmetric && p.x < 0;
    app.hover.point = app.hover.mirrored ? Vec3{-p.x,p.y,p.z} : p;
  }
  if (app.stroke) {
    if (!transform && in.hit) {
      float r = worldRadius(in.point);
      app.stroke->setSize(r, r);
    }
    if (!transform) app.stroke->setStrength(app.strokeStrength * std::clamp(pressure, 0.0f, 1.0f));
    app.stroke->moveTo(in);
  }
  if (app.paintStroke) {
    if (in.hit) app.paintStroke->setRadius(worldRadius(in.point));
    app.paintStroke->setStrength(app.strokeStrength * std::clamp(pressure, 0.0f, 1.0f));
    app.paintStroke->moveTo(in.point, in.hit);
  }
}

bool deferGrabMove(int x, int y, float pressure) {
  if (!app.stroke || app.stroke->tool() != Tool::Grab) return false;
  app.pendingGrab = {true,x,y,pressure};
  return true;
}

bool flushPendingStroke() {
  if (!app.pendingGrab.valid) return false;
  const auto input = app.pendingGrab;
  app.pendingGrab.valid = false;
  if (!app.stroke || app.stroke->tool() != Tool::Grab) return false;
  moveStroke(input.x,input.y,input.pressure);
  return true;
}

void endStroke() {
  // Release, pause and file operations must retain the final target even
  // when no render frame occurred between the last MOVE and UP.
  flushPendingStroke();
  app.stroke.reset();
  app.paintStroke.reset();
}

}  // namespace bh::viewer

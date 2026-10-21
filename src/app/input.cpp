#include "app/input.h"

#include <algorithm>

#include "app/app.h"
#include "app/dialogs.h"
#include "app/document.h"
#include "app/ui.h"

namespace bh::viewer {

namespace {

void nudge(float& slider, float delta) { slider = std::clamp(slider + delta, 0.0f, 1.0f); }

void onMaterialKey(WPARAM key) {
  Canvas& cv = *app.canvas;
  Material& mt = cv.material(app.editSlot);
  if (key >= '0' && key <= '9') {
    const int slot = int(key - '0');
    if (cv.material(slot).used) app.editSlot = slot;
    else app.status = "no layer " + std::to_string(slot) + " (L adds one)";
    return;
  }
  switch (key) {
    case 'L': addMaterialLayer(); return;
    case 'M': mt.matcap = nextBuiltinMatcap(mt.matcap); break;
    case 'O': {
      std::string file = openFileDialog(app.hwnd, kImageFilter);
      if (file.empty()) return;
      mt.matcap = file;
      break;
    }
    case 'C': mt.colored = !mt.colored; break;
    case 'A': if (app.editSlot > 0) mt.additive = !mt.additive; break;
    case 'H': if (app.editSlot > 0) mt.hidden = !mt.hidden; break;
    default: return;
  }
  app.modified = true;
}

void onPaintKey(WPARAM key, bool ctrl, bool shift) {
  std::array<float, 3>& ps = paintSliders();
  float& slider = shift ? ps[2] : ctrl ? ps[1] : ps[0];
  switch (key) {
    case VK_OEM_4:
    case VK_OEM_COMMA: nudge(slider, -0.1f); return;
    case VK_OEM_6:
    case VK_OEM_PERIOD: nudge(slider, 0.1f); return;
    case VK_OEM_MINUS: nudge(ps[1], -0.1f); return;
    case VK_OEM_PLUS: nudge(ps[1], 0.1f); return;
  }
  if (ctrl) {
    if (key == 'F') fillLayer(shift);
    return;
  }
  switch (key) {
    case 'D': app.paint.tool = PaintTool::Color; break;
    case 'B': app.paint.tool = PaintTool::Bump; break;
    case 'F': app.paint.tool = PaintTool::FlattenBump; break;
    case 'N': app.showBumpNormals = !app.showBumpNormals; break;
    case 'W': app.wireframe = !app.wireframe; break;
    default: onMaterialKey(key);
  }
}

void onSculptKey(WPARAM key, bool ctrl) {
  ToolSliders& tool = sliders(app.brush.tool);
  switch (key) {
    case VK_OEM_4:
    case VK_OEM_COMMA: nudge(ctrl ? tool.strength : tool.size, -0.1f); return;  // [ ,
    case VK_OEM_6:
    case VK_OEM_PERIOD: nudge(ctrl ? tool.strength : tool.size, 0.1f); return;  // ] .
    case VK_OEM_MINUS: nudge(tool.strength, -0.1f); return;
    case VK_OEM_PLUS: nudge(tool.strength, 0.1f); return;
  }
  if (ctrl) {
    switch (key) {
      case 'D': subdivideAll(); break;
      case 'Y': reduceAll(); break;
      case 'P': askEnterPaintMode(); break;
    }
    return;
  }
  switch (key) {
    case 'D': setTool(Tool::Draw); break;
    case 'B': setTool(Tool::Smooth); break;
    case 'G': setTool(Tool::Grab); break;
    case 'C': setTool(Tool::Inflate); break;
    case 'F': setTool(Tool::Flatten); break;
    case 'V': setTool(Tool::Pinch); break;
    case 'M': setTool(Tool::Mask); break;
    case 'E': setTool(Tool::Crease); break;
    case 'R': setTool(Tool::Rotate); break;
    case 'T': setTool(Tool::Scale); break;
    case 'Y': setTool(Tool::Reduce); break;
    case 'W': app.wireframe = !app.wireframe; break;
  }
}

}  // namespace

StrokeInput inputAt(int mx, int my, PickHit* hitOut) {
  const Camera& c = app.camera;
  float aspect = float(app.width) / float(std::max(app.height, 1));
  float x = (2.0f * mx / app.width - 1) * std::tan(kFov / 2) * aspect;
  float y = (1 - 2.0f * my / app.height) * std::tan(kFov / 2);
  StrokeInput in;
  in.rayOrigin = c.eye();
  in.rayDir = normalize(c.forward() + c.right() * x + c.up() * y);
  in.viewDir = c.forward();
  in.camRight = c.right();
  in.camUp = c.up();
  in.mouseX = float(mx);
  in.mouseY = float(my);
  PickHit h = pick(app.mesh, in.rayOrigin, in.rayDir);
  in.point = h.visiblePoint();
  in.hit = h.hit;
  if (hitOut) *hitOut = h;
  return in;
}

void beginStroke(int mx, int my, bool invert, bool smoothing) {
  PickHit h;
  StrokeInput in = inputAt(mx, my, &h);
  if (!h.hit) return;
  pushUndo();
  BrushSettings s = app.brush;
  if (smoothing) s.tool = Tool::Smooth;
  s.radius = worldRadius(in.point);
  s.detailSize = s.radius;
  s.strength = brushStrength(sliders(s.tool).strength);
  // Mask paints toward weight 0 by default (negative strength).
  if (s.tool == Tool::Mask) s.strength = -std::fabs(s.strength);
  if (invert) s.strength = -s.strength;
  app.stroke = std::make_unique<Stroke>(app.mesh, s, in);
  app.modified = true;
}

void beginPaintStroke(int mx, int my, bool invert) {
  StrokeInput in = inputAt(mx, my);
  if (!in.hit) return;
  const std::array<float, 3>& ps = paintSliders();
  PaintSettings s = app.paint;
  s.radius = worldRadius(in.point);
  s.strength = brushStrength(ps[1]) * (invert ? -1.0f : 1.0f);
  s.hardness = ps[2];
  s.target = app.editSlot;
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

void onMouseDown(UINT msg, int x, int y, WPARAM flags) {
  SetCapture(app.hwnd);
  if (msg == WM_LBUTTONDOWN) {
    if (GetKeyState(VK_MENU) < 0) app.orbiting = true;
    else if (app.paintMode) beginPaintStroke(x, y, (flags & MK_CONTROL) != 0);
    else beginStroke(x, y, (flags & MK_CONTROL) != 0, (flags & MK_SHIFT) != 0);
  } else {
    (msg == WM_RBUTTONDOWN ? app.orbiting : app.panning) = true;
  }
  app.mouseX = x;
  app.mouseY = y;
}

void onMouseUp() {
  ReleaseCapture();
  app.stroke.reset();
  app.paintStroke.reset();
  app.orbiting = app.panning = false;
}

void onMouseMove(int x, int y) {
  int dx = x - app.mouseX, dy = y - app.mouseY;
  if (app.orbiting) {
    app.camera.yaw -= dx * 0.008f;
    app.camera.pitch = std::clamp(app.camera.pitch + dy * 0.008f, -1.5f, 1.5f);
  } else if (app.panning) {
    float scale = app.camera.distance * 2 * std::tan(kFov / 2) / float(app.height);
    app.camera.target += (app.camera.right() * float(-dx) + app.camera.up() * float(dy)) * scale;
  }
  StrokeInput in = inputAt(x, y, &app.hover);
  if (app.stroke) {
    if (in.hit) {
      float r = worldRadius(in.point);
      app.stroke->setSize(r, r);
    }
    app.stroke->moveTo(in);
  }
  if (app.paintStroke) {
    if (in.hit) app.paintStroke->setRadius(worldRadius(in.point));
    app.paintStroke->moveTo(in.point, in.hit);
  }
  app.mouseX = x;
  app.mouseY = y;
}

void onMouseWheel(int delta) { app.camera.distance *= delta > 0 ? 0.9f : 1.1f; }

void onKey(WPARAM key) {
  const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
  app.status.clear();
  if (ctrl) {  // File and Edit, in both modes
    switch (key) {
      case 'N': commandNew(); return;
      case 'O': commandOpen(); return;
      case 'S': shift ? commandSaveAs() : commandSave(); return;
      case 'Z': undoLast(); return;
    }
  }
  if (app.paintMode) onPaintKey(key, ctrl, shift);
  else onSculptKey(key, ctrl);
}

}  // namespace bh::viewer

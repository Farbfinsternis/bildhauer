#include "platform/windows/host.h"
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
      std::string file = openFileDialog(windowsHost.hwnd, kImageFilter);
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

void onMouseDown(UINT msg, int x, int y, WPARAM flags) {
  SetCapture(windowsHost.hwnd);
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
    float scale = app.camera.distance * 2 * std::tan(kFov / 2) / float(app.viewH);
    app.camera.target += (app.camera.right() * float(-dx) + app.camera.up() * float(dy)) * scale;
  }
  moveStroke(x, y);
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

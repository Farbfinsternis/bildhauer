// bildhauer viewer: Win32 + OpenGL, UI with Dear ImGui (third_party/imgui).
//
//   bildhauer [file.sc1]      (without a file: an icosphere)
//   bildhauer [file.sc1] --snapshot out.png [--demo] [--paint] [--save]
//                             [--symmetry-off] [--bump-normals] [--no-ui] [--frames N]
//                             render one frame and exit (snapshot.h);
//                             --frames also writes the mean frame time to out.png.txt
//
// Mouse and keys: Help > Mouse and keys in the viewer (ui.cpp), or the
// README. Files with paint data open in paint mode.

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>

#include <GL/gl.h>

#include <string>

#include "app/app.h"
#include "app/document.h"
#include "app/input.h"
#include "app/render.h"
#include "app/snapshot.h"
#include "app/ui.h"
#include "core/relax.h"
#include "imgui_impl_win32.h"

using namespace bh;
using namespace bh::viewer;

namespace {

// A stroke or a camera drag owns the mouse until the button is released,
// even over the UI.
bool viewHasMouse() { return app.stroke || app.paintStroke || app.orbiting || app.panning; }

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // The 3D view shows a crosshair; over the UI, ImGui sets its own cursor.
  if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && (viewHasMouse() || !uiWantsMouse())) {
    SetCursor(LoadCursor(nullptr, IDC_CROSS));
    return TRUE;
  }
  // The UI sees every message (it tracks buttons and keys itself); the view
  // then takes what the UI does not want.
  if (uiHandleMessage(hwnd, msg, wp, lp)) return TRUE;

  const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
  switch (msg) {
    case WM_SIZE:
      app.width = std::max(1, int(LOWORD(lp)));
      app.height = std::max(1, int(HIWORD(lp)));
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      if (!uiWantsMouse()) onMouseDown(msg, x, y, wp);
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      if (viewHasMouse()) onMouseUp();
      return 0;
    case WM_MOUSEMOVE:
      if (viewHasMouse() || !uiWantsMouse()) onMouseMove(x, y);
      else app.hover = PickHit{};  // no brush ring over the UI
      return 0;
    case WM_MOUSEWHEEL:
      if (!uiWantsMouse()) onMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
      return 0;
    case WM_KEYDOWN:
      if (!uiWantsKeyboard()) onKey(wp);
      return 0;
    case WM_CLOSE:
      if (confirmDiscard()) DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

bool createWindow(HINSTANCE instance) {
  WNDCLASSA wc{};
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = "bildhauer";
  RegisterClassA(&wc);
  app.hwnd = CreateWindowA("bildhauer", "bildhauer", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                           app.width, app.height, nullptr, nullptr, instance, nullptr);
  if (!app.hwnd) return false;
  app.hdc = GetDC(app.hwnd);
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof pfd;
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  int format = ChoosePixelFormat(app.hdc, &pfd);
  if (!format || !SetPixelFormat(app.hdc, format, &pfd)) return false;
  HGLRC rc = wglCreateContext(app.hdc);
  if (!rc || !wglMakeCurrent(app.hdc, rc)) return false;
  RECT client;
  GetClientRect(app.hwnd, &client);
  app.width = std::max(1, int(client.right));
  app.height = std::max(1, int(client.bottom));
  return true;
}

void updateTitle() {
  static std::string shown;
  std::string name = app.path.empty() ? "new model" : app.path.substr(app.path.find_last_of("/\\") + 1);
  std::string title = "bildhauer - " + name + (app.modified ? " *" : "");
  if (title != shown) SetWindowTextA(app.hwnd, title.c_str());
  shown = title;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // Windows Imaging Component (matcap images)
  ImGui_ImplWin32_EnableDpiAwareness();
  std::string modelPath;
  SnapshotOptions snapshot;
  for (int i = 1; i < __argc; ++i) {
    std::string a = __argv[i];
    if (a == "--snapshot" && i + 1 < __argc) snapshot.path = __argv[++i];
    else if (a == "--frames" && i + 1 < __argc) snapshot.frames = std::atoi(__argv[++i]);
    else if (a == "--demo") snapshot.demo = true;
    else if (a == "--paint") snapshot.paint = true;
    else if (a == "--symmetry-off") snapshot.symmetryOff = true;
    else if (a == "--save") snapshot.save = true;
    else if (a == "--no-ui") snapshot.ui = false;
    else if (a == "--bump-normals") app.showBumpNormals = true;
    else modelPath = a;
  }
  for (int t = 0; t < int(app.sliders.size()); ++t) app.sliders[size_t(t)] = defaultSliders(Tool(t));
  app.paintSliders = {{{0.3f, 1.0f, 0.5f}, {0.3f, 0.4f, 0.5f}, {0.3f, 0.4f, 0.5f}}};
  if (!modelPath.empty()) {
    std::string err;
    if (!loadModel(modelPath, &err)) {
      MessageBoxA(nullptr, (modelPath + "\n" + err).c_str(), "bildhauer: cannot load", MB_ICONERROR);
      return 1;
    }
  } else {
    newModel();
  }
  if (!createWindow(instance)) {
    MessageBoxA(nullptr, "Could not create an OpenGL window.", "bildhauer", MB_ICONERROR);
    return 1;
  }
  initRendering();
  initUi(app.hwnd);
  if (!snapshot.path.empty()) return runSnapshot(snapshot);

  MSG msg;
  int settleFrames = 0;  // the UI needs a few frames to react to input (popups, hover)
  for (;;) {
    bool input = false;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        shutdownUi();
        return 0;
      }
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
      input = true;
    }
    if (input) settleFrames = 3;
    // One beautify pass per frame while vertices are dirty (mesh-core.md 5.2);
    // it keeps relaxing after the stroke until the mesh has settled.
    bool relaxing = (app.beautify || app.brush.detailFactor > 0.15f) && !app.mesh.dirtyVertices.empty();
    if (relaxing) beautifyPass(app.mesh);
    renderScene();
    drawUi();
    SwapBuffers(app.hdc);
    updateTitle();
    if (settleFrames > 0) --settleFrames;
    if (!viewHasMouse() && !relaxing && settleFrames == 0) WaitMessage();  // idle until the next input
  }
}

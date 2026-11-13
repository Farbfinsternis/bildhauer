#include "platform/windows/host.h"
// bildhauer viewer: Win32 + OpenGL, UI with Dear ImGui (third_party/imgui).
//
//   bildhauer [file.sc1]      (without a file: an icosphere)
//   bildhauer [file.sc1] --snapshot out.png [--demo] [--paint] [--save]
//                             [--symmetry-off] [--bump-normals] [--no-ui] [--frames N]
//                             [--brush-mask image] [--brush-texture image]
//                             [--heightmask] [--cavity] [--export-obj file.obj] [--obj-normals]
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
#include "app/update.h"
#include "app/widgets.h"
#include "core/relax.h"
#include "imgui_impl_win32.h"

using namespace bh;
using namespace bh::viewer;

namespace {

// A stroke or a camera drag owns the mouse until the button is released,
// even over the UI.
bool viewHasMouse() { return app.stroke || app.paintStroke || app.orbiting || app.panning; }

// The pen's pressure (0..1) from its last pointer message. Windows turns pen
// input into mouse messages as well; those carry no pressure, so the mouse
// handlers take it from here when a message comes from the pen.
float penPressure = 1;

void readPenPressure(WPARAM wp) {
  POINTER_PEN_INFO pen;
  if (!GetPointerPenInfo(GET_POINTERID_WPARAM(wp), &pen)) return;
  penPressure = (pen.penMask & PEN_MASK_PRESSURE) ? std::clamp(float(pen.pressure) / 1024.0f, 0.0f, 1.0f) : 1.0f;
}

// Mouse messages that Windows made from pen input are marked (see "System
// Events and Mouse Messages" in the Windows docs).
bool messageFromPen() { return (GetMessageExtraInfo() & 0xFFFFFF80) == 0xFF515700; }

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const float pressure = messageFromPen() ? penPressure : 1.0f;  // before other calls change it
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
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
      if (POINTER_INPUT_TYPE type; GetPointerType(GET_POINTERID_WPARAM(wp), &type) && type == PT_PEN)
        readPenPressure(wp);
      break;  // on to DefWindowProc, which sends the mouse messages
    case WM_DPICHANGED: {
      // Moved to a monitor with other scaling: the size Windows suggests
      // keeps the window as large to the eye, and the UI follows.
      setUiScale(HIWORD(wp) / 96.0f);
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SIZE:
      app.width = std::max(1, int(LOWORD(lp)));
      app.height = std::max(1, int(HIWORD(lp)));
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      if (!uiWantsMouse()) onMouseDown(msg, x, y, wp, pressure);
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      if (viewHasMouse()) onMouseUp();
      return 0;
    case WM_MOUSEMOVE:
      if (viewHasMouse() || !uiWantsMouse()) onMouseMove(x, y, pressure);
      else app.hover = PickHit{};  // no brush ring over the UI
      return 0;
    case WM_MOUSEWHEEL:
      if (!uiWantsMouse()) onMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
      return 0;
    case WM_KEYDOWN:
      if (!uiWantsKeyboard()) onKey(wp);
      return 0;
    case kUpdateCheckedMessage:
      onUpdateChecked();
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

// Holding the pen still would otherwise wait for a right click, and taps
// would draw ripples: a sculpting stroke wants the plain pen.
void disablePenGestures(HWND hwnd) {
  const char* name = "MicrosoftTabletPenServiceProperty";
  GlobalAddAtomA(name);
  // Press and hold, tap feedback, barrel feedback, flicks.
  const DWORD_PTR flags = 0x00000001 | 0x00000008 | 0x00000010 | 0x00010000;
  SetPropA(hwnd, name, reinterpret_cast<HANDLE>(flags));
  const BOOL off = FALSE;
  for (FEEDBACK_TYPE f : {FEEDBACK_PEN_BARRELVISUALIZATION, FEEDBACK_PEN_TAP, FEEDBACK_PEN_DOUBLETAP,
                          FEEDBACK_PEN_PRESSANDHOLD, FEEDBACK_PEN_RIGHTTAP})
    SetWindowFeedbackSetting(hwnd, f, 0, sizeof off, &off);
}

// The starting size is meant for 100 %; on a scaled monitor the window grows
// along, within the monitor's work area.
void scaleWindowForDpi(HWND hwnd) {
  const float scale = GetDpiForWindow(hwnd) / 96.0f;
  if (scale <= 1) return;
  MONITORINFO mi{sizeof mi};
  GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
  RECT w;
  GetWindowRect(hwnd, &w);
  const RECT& area = mi.rcWork;
  const int width = std::min(int((w.right - w.left) * scale), int(area.right - area.left));
  const int height = std::min(int((w.bottom - w.top) * scale), int(area.bottom - area.top));
  const int left = std::clamp(int(w.left), int(area.left), int(area.right) - width);
  const int top = std::clamp(int(w.top), int(area.top), int(area.bottom) - height);
  SetWindowPos(hwnd, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool createWindow(HINSTANCE instance) {
  WNDCLASSEXA wc{};
  wc.cbSize = sizeof wc;
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // The logo from bildhauer.rc, in the sizes the title bar and Alt+Tab want.
  wc.hIcon = static_cast<HICON>(LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                           GetSystemMetrics(SM_CYICON), 0));
  wc.hIconSm = static_cast<HICON>(LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), 0));
  wc.lpszClassName = "bildhauer";
  RegisterClassExA(&wc);
  windowsHost.hwnd = CreateWindowA("bildhauer", "bildhauer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           app.width, app.height, nullptr, nullptr, instance, nullptr);
  if (!windowsHost.hwnd) return false;
  disablePenGestures(windowsHost.hwnd);
  windowsHost.hdc = GetDC(windowsHost.hwnd);
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof pfd;
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  int format = ChoosePixelFormat(windowsHost.hdc, &pfd);
  if (!format || !SetPixelFormat(windowsHost.hdc, format, &pfd)) return false;
  HGLRC rc = wglCreateContext(windowsHost.hdc);
  if (!rc || !wglMakeCurrent(windowsHost.hdc, rc)) return false;
  RECT client;
  GetClientRect(windowsHost.hwnd, &client);
  app.width = std::max(1, int(client.right));
  app.height = std::max(1, int(client.bottom));
  return true;
}

void updateTitle() {
  static std::string shown;
  std::string name = app.path.empty() ? "new model" : app.path.substr(app.path.find_last_of("/\\") + 1);
  std::string title = "bildhauer - " + name + (app.modified ? " *" : "");
  if (title != shown) SetWindowTextA(windowsHost.hwnd, title.c_str());
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
    else if (a == "--export-obj" && i + 1 < __argc) snapshot.exportObj = __argv[++i];
    else if (a == "--obj-normals") app.exportNormals = true;
    else if (a == "--no-ui") snapshot.ui = false;
    else if (a == "--bump-normals") app.showBumpNormals = true;
    else if (a == "--heightmask") app.paint.heightmask = true;
    else if (a == "--cavity") app.paint.heightmask = app.paint.cavity = true;
    else if (a == "--brush-mask" && i + 1 < __argc) snapshot.brushMask = __argv[++i];
    else if (a == "--brush-texture" && i + 1 < __argc) snapshot.brushTexture = __argv[++i];
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
  // Snapshots keep exactly the size they were made for.
  if (snapshot.path.empty()) scaleWindowForDpi(windowsHost.hwnd);
  ShowWindow(windowsHost.hwnd, SW_SHOW);
  initRendering();
  initUi(windowsHost.hwnd);
  if (!snapshot.path.empty()) return runSnapshot(snapshot);
  if (checkUpdatesOnStart()) startUpdateCheck(windowsHost.hwnd, false);

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
    layoutView(true);
    renderScene();
    drawUi();
    SwapBuffers(windowsHost.hdc);
    updateTitle();
    if (settleFrames > 0) --settleFrames;
    // Idle until the next input, unless the view name under the axes is fading.
    if (!viewHasMouse() && !relaxing && settleFrames == 0 && !axisLabelFading()) WaitMessage();
  }
}

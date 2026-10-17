// bildhauer viewer: Win32 + OpenGL 1.x, no other dependencies.
//
//   bildhauer [file.sc1]      (without a file: an icosphere)
//   bildhauer [file.sc1] --snapshot out.png [--demo] [--frames N]
//                             render one frame (after demo strokes) and exit;
//                             --frames also writes the mean frame time to out.png.txt
//
// Mouse: LMB sculpt (Ctrl inverts; with Mask: Ctrl unmasks), RMB or Alt+LMB
// orbit, MMB pan, wheel zoom.
// Keys: D Draw, B Smooth, G Grab, C Inflate, F Flatten, V Pinch, M Mask,
// E Crease, R Rotate, T Scale, Y Reduce brush; [ ] size, - = strength,
// W wireframe, Ctrl+D subdivide, Ctrl+Y reduce all, Ctrl+Z undo,
// Ctrl+S save as <name>_bildhauer.sc1.

#include <windows.h>
#include <windowsx.h>

#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/brush.h"
#include "core/mesh.h"
#include "core/pick.h"
#include "core/reduce.h"
#include "core/subdivide.h"
#include "io/sc1.h"

using namespace bh;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFov = 40.0f * kPi / 180.0f;

// --- scene -------------------------------------------------------------------

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
  Sc1File file;  // keeps extension values for saving
  std::string path;
  Camera camera;
  float sceneSize = 1;

  BrushSettings brush;
  float brushPixels = 60;
  std::unique_ptr<Stroke> stroke;
  std::deque<Mesh> undo;

  bool wireframe = false;
  int mouseX = 0, mouseY = 0;
  bool orbiting = false, panning = false;
  PickHit hover;
  GLuint matcap = 0;
  std::string status;
};

App app;

// An icosphere as the default model (full mesh, no symmetry).
void makeIcosphere(Mesh& m, int levels) {
  const float t = (1 + std::sqrt(5.0f)) / 2;
  std::vector<Vec3> p = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                         {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  std::vector<std::array<int, 3>> f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                       {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                       {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (int l = 0; l < levels; ++l) {
    std::map<std::pair<int, int>, int> mid;
    auto midpoint = [&](int a, int b) {
      auto key = std::minmax(a, b);
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      p.push_back(normalize(p[a] + p[b]));
      return mid[key] = int(p.size()) - 1;
    };
    std::vector<std::array<int, 3>> next;
    for (auto& tri : f) {
      int a = midpoint(tri[0], tri[1]), b = midpoint(tri[1], tri[2]), c = midpoint(tri[2], tri[0]);
      next.push_back({tri[0], a, c});
      next.push_back({tri[1], b, a});
      next.push_back({tri[2], c, b});
      next.push_back({a, b, c});
    }
    f.swap(next);
  }
  m = Mesh{};
  for (Vec3& q : p) m.addVertex(normalize(q));
  for (auto& tri : f) m.addTriangle(tri[0], tri[1], tri[2]);
  m.updateGeometry();
  m.storeReference();
}

void frameModel() {
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (int v = 0; v < app.mesh.vertexSlots(); ++v) {
    const Vertex& vx = app.mesh.vertex(v);
    if (!vx.active) continue;
    lo = {std::fmin(lo.x, vx.pos.x), std::fmin(lo.y, vx.pos.y), std::fmin(lo.z, vx.pos.z)};
    hi = {std::fmax(hi.x, vx.pos.x), std::fmax(hi.y, vx.pos.y), std::fmax(hi.z, vx.pos.z)};
  }
  if (app.mesh.symmetric) lo.x = -hi.x;
  app.camera.target = (lo + hi) * 0.5f;
  app.sceneSize = std::fmax(length(hi - lo), 1e-3f);
  app.camera.distance = app.sceneSize * 1.6f;
}

bool loadModel(const std::string& path) {
  std::vector<uint8_t> data;
  std::string err;
  if (!readBinaryFile(path, data) || !readSc1(data, app.file, &err) || !sc1ToMesh(app.file, app.mesh, &err)) {
    MessageBoxA(nullptr, (path + "\n" + err).c_str(), "bildhauer: cannot load", MB_ICONERROR);
    return false;
  }
  app.path = path;
  return true;
}

void saveModel() {
  std::string out = app.path.empty() ? std::string("untitled") : app.path;
  if (out.size() > 4 && out.substr(out.size() - 4) == ".sc1") out.resize(out.size() - 4);
  out += "_bildhauer.sc1";
  meshToSc1(app.mesh, app.file);
  std::vector<uint8_t> data;
  std::string err;
  bool ok = writeSc1(app.file, data, &err) && writeBinaryFile(out, data);
  app.status = ok ? "saved " + out : "save failed: " + err;
}

void pushUndo() {
  app.undo.push_back(app.mesh);
  if (app.undo.size() > 20) app.undo.pop_front();
}

// --- rendering ---------------------------------------------------------------

// A neutral clay matcap, computed (no image assets).
GLuint makeMatcap() {
  const int n = 256;
  std::vector<unsigned char> px(n * n * 3);
  const Vec3 light = normalize({-0.45f, 0.65f, 0.6f});
  const Vec3 base{0.80f, 0.66f, 0.56f};
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1;
      float r2 = std::fmin(u * u + v * v, 1.0f);
      Vec3 nrm{u, v, std::sqrt(1 - r2)};
      float diff = std::fmax(0.0f, dot(nrm, light));
      Vec3 refl = nrm * (2 * dot(nrm, light)) - light;
      float spec = std::pow(std::fmax(0.0f, refl.z), 24.0f);
      float rim = std::pow(1 - nrm.z, 3.0f) * 0.25f;
      Vec3 c = base * (0.22f + 0.78f * diff) + Vec3{1, 1, 1} * (0.3f * spec + rim);
      unsigned char* o = &px[(y * n + x) * 3];
      o[0] = (unsigned char)(std::fmin(c.x, 1.0f) * 255);
      o[1] = (unsigned char)(std::fmin(c.y, 1.0f) * 255);
      o[2] = (unsigned char)(std::fmin(c.z, 1.0f) * 255);
    }
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, n, n, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  return tex;
}

void loadView() {
  const Camera& c = app.camera;
  Vec3 f = c.forward(), r = c.right(), u = c.up(), e = c.eye();
  const float m[16] = {r.x, u.x, -f.x, 0, r.y, u.y, -f.y, 0, r.z, u.z, -f.z, 0,
                       -dot(r, e), -dot(u, e), dot(f, e), 1};
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(m);
}

// Draws the stored mesh, or its mirror image (x -> -x). Per-vertex arrays
// indexed by vertex slot, plus an index list of the active triangles.
void drawMesh(bool mirrored) {
  const Mesh& m = app.mesh;
  const Camera& c = app.camera;
  const Vec3 r = c.right(), u = c.up();
  static std::vector<float> pos, uv, col;
  static std::vector<GLuint> idx;
  const size_t n = size_t(m.vertexSlots());
  pos.resize(n * 3);
  uv.resize(n * 2);
  col.resize(n * 3);
  for (size_t i = 0; i < n; ++i) {
    const Vertex& v = m.vertex(int(i));
    if (!v.active) continue;
    Vec3 p = v.pos, nr = v.normal;
    if (mirrored) { p.x = -p.x; nr.x = -nr.x; }
    pos[i * 3] = p.x; pos[i * 3 + 1] = p.y; pos[i * 3 + 2] = p.z;
    uv[i * 2] = 0.5f + 0.49f * dot(nr, r);
    uv[i * 2 + 1] = 0.5f + 0.49f * dot(nr, u);
    float shade = v.hidden ? 0.5f : 0.35f + 0.65f * v.weight;
    col[i * 3] = shade; col[i * 3 + 1] = shade; col[i * 3 + 2] = v.weight < 1 ? shade * 0.8f : shade;
  }
  idx.clear();
  for (int t = 0; t < m.triangleSlots(); ++t) {
    const Triangle& tri = m.triangle(t);
    if (tri.active) idx.insert(idx.end(), {GLuint(tri.v[0]), GLuint(tri.v[1]), GLuint(tri.v[2])});
  }
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glVertexPointer(3, GL_FLOAT, 0, pos.data());
  glTexCoordPointer(2, GL_FLOAT, 0, uv.data());
  glColorPointer(3, GL_FLOAT, 0, col.data());
  glFrontFace(mirrored ? GL_CW : GL_CCW);
  glEnable(GL_TEXTURE_2D);
  glDrawElements(GL_TRIANGLES, GLsizei(idx.size()), GL_UNSIGNED_INT, idx.data());
  glDisable(GL_TEXTURE_2D);
  if (app.wireframe) {
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(0.1f, 0.1f, 0.12f, 0.35f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1, -1);
    glDrawElements(GL_TRIANGLES, GLsizei(idx.size()), GL_UNSIGNED_INT, idx.data());
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);
  glFrontFace(GL_CCW);
}

float worldRadius(const Vec3& at) {
  float d = length(at - app.camera.eye());
  return app.brushPixels * 2 * d * std::tan(kFov / 2) / float(app.height);
}

void drawBrushCursor() {
  if (!app.hover.hit) return;
  Vec3 c = app.hover.point, n = app.hover.normal;
  if (app.hover.mirrored) { c.x = -c.x; n.x = -n.x; }
  Vec3 a = normalize(cross(n, std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
  Vec3 b = cross(n, a);
  float r = worldRadius(c);
  glDisable(GL_DEPTH_TEST);
  glColor4f(1, 0.85f, 0.3f, 0.9f);
  glBegin(GL_LINE_LOOP);
  for (int i = 0; i < 48; ++i) {
    float t = i * 2 * kPi / 48;
    Vec3 p = c + (a * std::cos(t) + b * std::sin(t)) * r + n * (r * 0.02f);
    glVertex3f(p.x, p.y, p.z);
  }
  glEnd();
  glEnable(GL_DEPTH_TEST);
}

void render(bool present = true) {
  glViewport(0, 0, app.width, app.height);
  glClearColor(0.16f, 0.17f, 0.19f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  float nearZ = app.sceneSize * 0.01f, farZ = app.sceneSize * 50;
  float top = nearZ * std::tan(kFov / 2), right = top * float(app.width) / float(std::max(app.height, 1));
  glFrustum(-right, right, -top, top, nearZ, farZ);
  loadView();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindTexture(GL_TEXTURE_2D, app.matcap);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  drawMesh(false);
  if (app.mesh.symmetric) drawMesh(true);
  glDisable(GL_CULL_FACE);
  drawBrushCursor();
  if (!present) return;
  SwapBuffers(app.hdc);

  char title[512];
  std::snprintf(title, sizeof title, "bildhauer - %s  size %.0fpx  strength %.2f  %s%s|  %d tris  %s",
                toolName(app.brush.tool), app.brushPixels, app.brush.strength,
                app.brush.clay ? "clay " : "", app.brush.global ? "global " : "", app.mesh.numTriangles(),
                app.status.c_str());
  SetWindowTextA(app.hwnd, title);
}

// --- input -------------------------------------------------------------------

StrokeInput inputAt(int mx, int my, PickHit* hitOut = nullptr) {
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
  in.point = h.point;
  in.hit = h.hit;
  if (hitOut) *hitOut = h;
  return in;
}

void beginStroke(int mx, int my, bool invert) {
  PickHit h;
  StrokeInput in = inputAt(mx, my, &h);
  if (!h.hit) return;
  pushUndo();
  BrushSettings s = app.brush;
  s.radius = worldRadius(h.point);
  s.detailSize = s.radius * 0.5f;
  // Mask paints toward weight 0 by default (negative strength).
  if (s.tool == Tool::Mask) s.strength = -std::fabs(s.strength);
  if (invert) s.strength = -s.strength;
  app.stroke = std::make_unique<Stroke>(app.mesh, s, in);
}

void setTool(Tool t) {
  if (app.brush.tool == t) {
    // Pressing the tool key again toggles its option, as in Sculptris.
    if (t == Tool::Draw) app.brush.clay = !app.brush.clay;
    if (t == Tool::Flatten) app.brush.lockPlane = !app.brush.lockPlane;
    if (t == Tool::Grab || t == Tool::Rotate || t == Tool::Scale) app.brush.global = !app.brush.global;
  }
  app.brush.tool = t;
}

void onKey(WPARAM key) {
  bool ctrl = GetKeyState(VK_CONTROL) < 0;
  app.status.clear();
  if (ctrl) {
    switch (key) {
      case 'Z':
        if (!app.undo.empty()) { app.mesh = std::move(app.undo.back()); app.undo.pop_back(); }
        break;
      case 'S': saveModel(); break;
      case 'D': pushUndo(); subdivide(app.mesh, {true, app.mesh.symmetric}); break;
      case 'Y': pushUndo(); reduceMesh(app.mesh); break;
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
    case VK_OEM_4: app.brushPixels = std::fmax(5.0f, app.brushPixels / 1.15f); break;   // [
    case VK_OEM_6: app.brushPixels = std::fmin(400.0f, app.brushPixels * 1.15f); break; // ]
    case VK_OEM_MINUS: app.brush.strength = std::fmax(0.05f, app.brush.strength - 0.05f); break;
    case VK_OEM_PLUS: app.brush.strength = std::fmin(1.0f, app.brush.strength + 0.05f); break;
  }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      app.width = std::max(1, int(LOWORD(lp)));
      app.height = std::max(1, int(HIWORD(lp)));
      return 0;
    case WM_LBUTTONDOWN:
      SetCapture(hwnd);
      if (GetKeyState(VK_MENU) < 0) app.orbiting = true;
      else beginStroke(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_CONTROL) != 0);
      app.mouseX = GET_X_LPARAM(lp);
      app.mouseY = GET_Y_LPARAM(lp);
      return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      SetCapture(hwnd);
      (msg == WM_RBUTTONDOWN ? app.orbiting : app.panning) = true;
      app.mouseX = GET_X_LPARAM(lp);
      app.mouseY = GET_Y_LPARAM(lp);
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      ReleaseCapture();
      app.stroke.reset();
      app.orbiting = app.panning = false;
      return 0;
    case WM_MOUSEMOVE: {
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      int dx = x - app.mouseX, dy = y - app.mouseY;
      if (app.orbiting) {
        app.camera.yaw -= dx * 0.008f;
        app.camera.pitch = std::clamp(app.camera.pitch + dy * 0.008f, -1.5f, 1.5f);
      } else if (app.panning) {
        float scale = app.camera.distance * 2 * std::tan(kFov / 2) / float(app.height);
        app.camera.target += (app.camera.right() * float(-dx) + app.camera.up() * float(dy)) * scale;
      }
      StrokeInput in = inputAt(x, y, &app.hover);
      if (app.stroke) app.stroke->moveTo(in);
      app.mouseX = x;
      app.mouseY = y;
      return 0;
    }
    case WM_MOUSEWHEEL:
      app.camera.distance *= GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.9f : 1.1f;
      return 0;
    case WM_KEYDOWN:
      onKey(wp);
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
  wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
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
  return rc && wglMakeCurrent(app.hdc, rc);
}

// --- snapshot mode (for checks without interaction) ----------------------------

uint32_t crc32(const unsigned char* d, size_t n, uint32_t c = 0xffffffffu) {
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return c;
}

// PNG with stored (uncompressed) deflate blocks; no zlib needed.
bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgbBottomUp) {
  std::vector<unsigned char> raw;
  for (int y = h - 1; y >= 0; --y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgbBottomUp.begin() + size_t(y) * w * 3, rgbBottomUp.begin() + size_t(y + 1) * w * 3);
  }
  std::vector<unsigned char> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  for (size_t pos = 0; pos < raw.size() || pos == 0; pos += 65535) {
    size_t len = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + len >= raw.size() ? 1 : 0);
    z.insert(z.end(), {(unsigned char)(len & 255), (unsigned char)(len >> 8), (unsigned char)(~len & 255),
                       (unsigned char)((~len >> 8) & 255)});
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
    if (pos + len >= raw.size()) break;
  }
  uint32_t adler = (b << 16) | a;
  z.insert(z.end(), {(unsigned char)(adler >> 24), (unsigned char)(adler >> 16), (unsigned char)(adler >> 8),
                     (unsigned char)adler});
  std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
    uint32_t n = uint32_t(data.size());
    out.insert(out.end(), {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n});
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    uint32_t c = ~crc32(td.data(), td.size());
    out.insert(out.end(), {(unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c});
  };
  std::vector<unsigned char> ihdr = {(unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8),
                                     (unsigned char)w, (unsigned char)(h >> 24), (unsigned char)(h >> 16),
                                     (unsigned char)(h >> 8), (unsigned char)h, 8, 2, 0, 0, 0};
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return writeBinaryFile(path, out);
}

// A few strokes across the front, in screen space: Draw, inverted Draw, Mask.
void runDemo() {
  auto strokeAcross = [&](Tool tool, bool invert, int y0) {
    app.brush.tool = tool;
    beginStroke(app.width / 2 - 130, y0, invert);
    for (int i = 0; i <= 40; ++i)
      if (app.stroke) app.stroke->moveTo(inputAt(app.width / 2 - 130 + i * 6, y0 + int(20 * std::sin(i * 0.3))));
    app.stroke.reset();
  };
  app.brushPixels = 45;
  app.brush.strength = 0.8f;
  strokeAcross(Tool::Draw, false, app.height / 2 - 90);
  strokeAcross(Tool::Draw, true, app.height / 2);
  strokeAcross(Tool::Mask, false, app.height / 2 + 90);
  app.brush.tool = Tool::Draw;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  std::string modelPath, snapshot;
  bool demo = false;
  int frames = 0;
  for (int i = 1; i < __argc; ++i) {
    std::string a = __argv[i];
    if (a == "--snapshot" && i + 1 < __argc) snapshot = __argv[++i];
    else if (a == "--frames" && i + 1 < __argc) frames = std::atoi(__argv[++i]);
    else if (a == "--demo") demo = true;
    else modelPath = a;
  }
  if (!modelPath.empty()) {
    if (!loadModel(modelPath)) return 1;
  } else {
    makeIcosphere(app.mesh, 3);
  }
  if (!createWindow(instance)) {
    MessageBoxA(nullptr, "Could not create an OpenGL window.", "bildhauer", MB_ICONERROR);
    return 1;
  }
  app.matcap = makeMatcap();
  frameModel();
  if (!snapshot.empty()) {
    RECT rc;
    GetClientRect(app.hwnd, &rc);
    app.width = rc.right;
    app.height = rc.bottom;
    if (demo) runDemo();
    if (frames > 0) {  // average frame time, written next to the snapshot
      LARGE_INTEGER f, t0, t1;
      QueryPerformanceFrequency(&f);
      QueryPerformanceCounter(&t0);
      for (int i = 0; i < frames; ++i) { render(false); glFinish(); }
      QueryPerformanceCounter(&t1);
      double ms = double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(f.QuadPart) / frames;
      FILE* out = std::fopen((snapshot + ".txt").c_str(), "w");
      if (out) {
        std::fprintf(out, "%d triangles, %.2f ms per frame\n", app.mesh.numTriangles(), ms);
        std::fclose(out);
      }
    }
    render(false);
    std::vector<unsigned char> px(size_t(app.width) * app.height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, app.width, app.height, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    return writePng(snapshot, app.width, app.height, px) ? 0 : 1;
  }

  MSG msg;
  for (;;) {
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) return 0;
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    render();
    if (!app.stroke) WaitMessage();  // idle until the next input
  }
}

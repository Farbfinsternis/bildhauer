// bildhauer viewer: Win32 + OpenGL 1.x, no other dependencies.
//
//   bildhauer [file.sc1]      (without a file: an icosphere)
//   bildhauer [file.sc1] --snapshot out.png [--demo] [--frames N]
//                             render one frame (after demo strokes) and exit;
//                             --frames also writes the mean frame time to out.png.txt
//
//                             (--paint: in paint mode, with paint strokes;
//                             --bump-normals: show the bump normal map;
//                             --save: save as with Ctrl+S before rendering)
//
// Mouse: LMB sculpt (Ctrl inverts; with Mask: Ctrl unmasks; Shift smooths),
// RMB or Alt+LMB orbit, MMB pan, wheel zoom.
// Keys: D Draw, B Smooth, G Grab, C Inflate, F Flatten, V Pinch, M Mask,
// E Crease, R Rotate, T Scale, Y Reduce brush; [ ] (or , .) size of the
// current tool, with Ctrl its strength (also - =),
// W wireframe, Ctrl+D subdivide, Ctrl+Y reduce all, Ctrl+Z undo,
// Ctrl+S save as <name>_bildhauer.sc1, Ctrl+P enter paint mode (one way).
// Paint mode: LMB paints (Ctrl: secondary color / lower / away from neutral);
// D Paint color, B Paint bump, F Flatten bump; [ ] size, with Ctrl strength,
// with Shift hardness; K primary color, Shift+K secondary; N shows the bump
// normal map; Ctrl+Z undo; Ctrl+S saves with paint data. Materials: L adds a
// layer, 0-9 choose what Paint color paints (0 the color, n the mask of
// layer n), M cycles the built-in matcaps of the current slot, O loads a
// matcap image for it, C toggles Colored, A additive, H hides the layer.
// Files with paint data open in paint mode.

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <objbase.h>

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

#include "app/gl2.h"
#include "app/matcaps.h"
#include "core/brush.h"
#include "core/brush_params.h"
#include "core/mesh.h"
#include "core/paint.h"
#include "core/pick.h"
#include "core/reduce.h"
#include "core/relax.h"
#include "core/subdivide.h"
#include "core/texmap.h"
#include "io/sc1.h"

using namespace bh;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFovSlider = 0.5f;
const float kFov = fovDegrees(kFovSlider) * kPi / 180.0f;

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
  std::vector<Tangent> tangents;  // per texvert
  int editSlot = 0;  // what Paint color paints: 0 the color layer, n the mask of material slot n
  bool showBumpNormals = false;
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

constexpr int kPaintTextureSize = 1024;

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
  m.clearDirty();
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
  if (app.file.hasTexturing) {  // painted in Sculptris: open in paint mode with the stored UVs and images
    Sc1Texturing tex;
    std::unique_ptr<Canvas> canvas;
    bool ok = decodeTexturing(app.file, tex, &err) && texturingToMapping(tex, app.mesh, app.texmap, &err);
    if (ok) {
      canvas = std::make_unique<Canvas>(app.mesh, app.texmap, tex.color.w, tex.color.h);
      ok = texturingToCanvas(tex, *canvas, &err);
    }
    if (ok) {
      app.canvas = std::move(canvas);
      app.tangents = computeTangents(app.mesh, app.texmap);
      app.paintMode = true;
      app.uploadAll = true;
    } else {
      app.status = "paint data not loaded: " + err;
    }
  }
  return true;
}

void saveModel() {
  std::string out = app.path.empty() ? std::string("untitled") : app.path;
  if (out.size() > 4 && out.substr(out.size() - 4) == ".sc1") out.resize(out.size() - 4);
  out += "_bildhauer.sc1";
  if (app.paintMode) meshToSc1(app.mesh, app.file, app.texmap, *app.canvas);
  else meshToSc1(app.mesh, app.file);
  std::vector<uint8_t> data;
  std::string err;
  bool ok = writeSc1(app.file, data, &err) && writeBinaryFile(out, data);
  app.status = ok ? "saved " + out : "save failed: " + err;
}

void pushUndo() {
  app.undo.push_back(app.mesh);
  if (app.undo.size() > 20) app.undo.pop_front();
}

// Freezes the mesh, builds the UV atlas and a fresh canvas. There is no way
// back: sculpting would invalidate the mapping.
void enterPaintMode() {
  if (app.paintMode) return;
  app.stroke.reset();
  for (int i = 0; i < 200 && !app.mesh.dirtyVertices.empty(); ++i) beautifyPass(app.mesh);
  app.mesh.updateGeometry();
  app.mesh.clearDirty();
  app.undo.clear();
  app.texmap = buildAtlas(app.mesh, {kPaintTextureSize});
  app.canvas = std::make_unique<Canvas>(app.mesh, app.texmap, kPaintTextureSize, kPaintTextureSize);
  app.canvas->material(0).matcap = builtinMatcaps()[0];
  app.tangents = computeTangents(app.mesh, app.texmap);
  app.editSlot = 0;
  app.paintMode = true;
  app.uploadAll = true;
  char msg[128];
  std::snprintf(msg, sizeof msg, "paint mode: %d charts, texel coverage %d%%", app.texmap.numCharts,
                int(100.0 * app.canvas->texels().covered / (kPaintTextureSize * kPaintTextureSize)));
  app.status = msg;
}

// --- rendering ---------------------------------------------------------------

GLuint makeTexture(int w, int h, GLint internal, GLenum format, const void* data) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl2::kClampToEdge);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl2::kClampToEdge);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, data);
  return tex;
}

// A matcap texture; an image that cannot be read falls back to clay.
GLuint matcapTexture(const std::string& name, bool gray, std::string* error) {
  MatcapImage im;
  if (!loadMatcap(name, gray, im, error)) loadMatcap(builtinMatcaps()[0], gray, im, nullptr);
  return makeTexture(im.width, im.height, GL_RGB8, GL_RGB, im.rgb.data());
}

// Paint shading (paint.md 4.1), bildhauer's own shaders. The bump normal map
// is sampled half a texel up and left of the color map, since it is a
// forward difference; it bends the normal in the tangent frame. The
// view-space normal picks the matcap value m, which lights the paint color
// like a hard-light layer. A layer pass takes its alpha from the slot's mask
// (uploaded as min(255, mask * 33 / 32)).
const char* kPaintVertexShader = R"(
#version 110
attribute vec4 tangent;
uniform float mirror;  // -1 on the mirrored half: flips the tangent frame's handedness
varying vec3 vNormal;
varying vec4 vTangent;
void main() {
  gl_Position = ftransform();
  vNormal = gl_NormalMatrix * gl_Normal;
  vTangent = vec4(gl_NormalMatrix * tangent.xyz, tangent.w * mirror);
  gl_TexCoord[0] = gl_MultiTexCoord0;
}
)";

const char* kPaintFragmentShader = R"(
#version 110
uniform sampler2D colorMap, normalMap, matcap, mask;
uniform vec2 texel;
uniform float useMask;
varying vec3 vNormal;
varying vec4 vTangent;
void main() {
  vec2 uv = gl_TexCoord[0].st;
  vec3 n = normalize(vNormal);
  vec3 t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
  vec3 b = cross(n, t) * vTangent.w;
  vec3 tn = texture2D(normalMap, uv - 0.5 * texel).xyz * 2.0 - 1.0;
  vec3 s = normalize(t * tn.x + b * tn.y + n * tn.z);
  vec3 m = texture2D(matcap, s.xy * 0.495 + 0.5).rgb;
  vec3 c = texture2D(colorMap, uv).rgb;
  vec3 mult = min(2.0 * m, 1.0);
  vec3 screen = max(2.0 * m - 1.0, 0.0);
  float a = useMask > 0.5 ? min(texture2D(mask, uv).r * 33.0 / 32.0, 1.0) : 1.0;
  gl_FragColor = vec4((1.0 - (1.0 - c) * (1.0 - screen)) * mult, a);
}
)";

void initPaintShader() {
  std::string err;
  if (!gl2::load()) {
    app.status = "no OpenGL 2.0: paint shading without bump";
    return;
  }
  app.paintShader = gl2::buildProgram(kPaintVertexShader, kPaintFragmentShader, "tangent", &err);
  if (!app.paintShader) app.status = "paint shader: " + err;
}

// Brings the textures up to date with the canvas: color and normal maps
// (all of them after a new canvas, otherwise the changed tiles), and per
// material slot its matcap and mask.
void syncPaintTextures() {
  Canvas& cv = *app.canvas;
  std::vector<int> tiles = cv.takeChangedTiles();  // also updates their normals
  uint32_t masks = cv.takeChangedMasks();
  if (app.uploadAll) {
    if (app.colorTex) glDeleteTextures(1, &app.colorTex);
    if (app.normalTex) glDeleteTextures(1, &app.normalTex);
    app.colorTex = makeTexture(cv.width(), cv.height(), GL_RGB8, GL_RGBA, cv.colorImage().data());
    app.normalTex = makeTexture(cv.width(), cv.height(), GL_RGB8, GL_RGB, cv.normalImage().data());
    for (auto& st : app.slotTex)
      if (st.mask) glDeleteTextures(1, &st.mask), st.mask = 0;
    app.uploadAll = false;
    tiles.clear();
  }
  if (!tiles.empty()) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, cv.width());
    for (int pass = 0; pass < 2; ++pass) {
      glBindTexture(GL_TEXTURE_2D, pass == 0 ? app.colorTex : app.normalTex);
      for (int t : tiles) {
        const int x0 = (t % cv.tilesX()) * kTileSize, y0 = (t / cv.tilesX()) * kTileSize;
        const int w = std::min(kTileSize, cv.width() - x0), h = std::min(kTileSize, cv.height() - y0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, x0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, y0);
        if (pass == 0) glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, cv.colorImage().data());
        else glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, w, h, GL_RGB, GL_UNSIGNED_BYTE, cv.normalImage().data());
      }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  }
  for (int slot = 0; slot < kMaxMaterials; ++slot) {
    const Material& mt = cv.material(slot);
    App::SlotTextures& st = app.slotTex[slot];
    if (!mt.used) {
      if (st.matcap) glDeleteTextures(1, &st.matcap);
      if (st.mask) glDeleteTextures(1, &st.mask);
      st = {};
      continue;
    }
    const std::string key = mt.matcap + (mt.colored ? "|colored" : "|gray");
    if (st.key != key) {
      if (st.matcap) glDeleteTextures(1, &st.matcap);
      std::string err;
      st.matcap = matcapTexture(mt.matcap, !mt.colored, &err);
      if (!err.empty()) app.status = err + " (shown as clay)";
      st.key = key;
    }
    if (st.mask && !(masks >> slot & 1)) continue;
    if (!st.mask) {
      st.mask = makeTexture(cv.width(), cv.height(), GL_LUMINANCE8, GL_LUMINANCE, mt.mask.data());
    } else {
      glBindTexture(GL_TEXTURE_2D, st.mask);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cv.width(), cv.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, mt.mask.data());
    }
  }
}

void loadView() {
  const Camera& c = app.camera;
  Vec3 f = c.forward(), r = c.right(), u = c.up(), e = c.eye();
  const float m[16] = {r.x, u.x, -f.x, 0, r.y, u.y, -f.y, 0, r.z, u.z, -f.z, 0,
                       -dot(r, e), -dot(u, e), dot(f, e), 1};
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(m);
}

// Triangle edges over the filled mesh, with the vertex array already set.
void drawWireframe(const std::vector<GLuint>& idx) {
  if (!app.wireframe) return;
  glColor4f(0.1f, 0.1f, 0.12f, 0.35f);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glEnable(GL_POLYGON_OFFSET_LINE);
  glPolygonOffset(-1, -1);
  glDrawElements(GL_TRIANGLES, GLsizei(idx.size()), GL_UNSIGNED_INT, idx.data());
  glDisable(GL_POLYGON_OFFSET_LINE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
  glDisableClientState(GL_COLOR_ARRAY);
  drawWireframe(idx);
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glFrontFace(GL_CCW);
}

void bindUnit(int unit, GLuint tex) {
  gl2::gl.ActiveTexture(gl2::kTexture0 + unit);
  glBindTexture(GL_TEXTURE_2D, tex);
}

// Paint mode: one vertex per texvert, since UVs differ across charts. Slot 0
// is drawn first, then every visible layer again with its own matcap and its
// mask as alpha (normal or additive blending). Without OpenGL 2.0 a
// stand-in: the color map times the base matcap with 2x modulation, no bump
// and no layers.
void drawPaintMesh(bool mirrored) {
  const Mesh& m = app.mesh;
  const TextureMapping& map = app.texmap;
  const Vec3 r = app.camera.right(), u = app.camera.up();
  static std::vector<float> pos, nrm, uv, tan, mc;
  static std::vector<GLuint> idx;
  const size_t n = map.texverts.size();
  pos.assign(n * 3, 0);
  nrm.assign(n * 3, 0);
  uv.resize(n * 2);
  tan.assign(n * 4, 0);
  mc.assign(n * 2, 0.5f);
  for (size_t i = 0; i < n; ++i) {
    const TexVert& tv = map.texverts[i];
    if (tv.vertex < 0 || tv.vertex >= m.vertexSlots() || !m.vertex(tv.vertex).active) continue;
    const Vertex& v = m.vertex(tv.vertex);
    Vec3 p = v.pos, nr = v.normal, t = i < app.tangents.size() ? app.tangents[i].t : Vec3{};
    if (mirrored) { p.x = -p.x; nr.x = -nr.x; t.x = -t.x; }
    pos[i * 3] = p.x; pos[i * 3 + 1] = p.y; pos[i * 3 + 2] = p.z;
    nrm[i * 3] = nr.x; nrm[i * 3 + 1] = nr.y; nrm[i * 3 + 2] = nr.z;
    tan[i * 4] = t.x; tan[i * 4 + 1] = t.y; tan[i * 4 + 2] = t.z;
    tan[i * 4 + 3] = i < app.tangents.size() ? app.tangents[i].w : 1;
    uv[i * 2] = tv.u;
    uv[i * 2 + 1] = tv.v;
    mc[i * 2] = 0.5f + 0.495f * dot(nr, r);
    mc[i * 2 + 1] = 0.5f + 0.495f * dot(nr, u);
  }
  idx.clear();
  const int nt = std::min(m.triangleSlots(), int(map.corners.size()));
  for (int t = 0; t < nt; ++t) {
    const auto& c = map.corners[t];
    if (m.triangle(t).active && c[0] >= 0 && c[1] >= 0 && c[2] >= 0)
      idx.insert(idx.end(), {GLuint(c[0]), GLuint(c[1]), GLuint(c[2])});
  }
  const GLsizei count = GLsizei(idx.size());
  const GLuint colorMap = app.showBumpNormals ? app.normalTex : app.colorTex;
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glVertexPointer(3, GL_FLOAT, 0, pos.data());
  glFrontFace(mirrored ? GL_CW : GL_CCW);

  if (app.paintShader) {
    using gl2::gl;
    const GLuint prog = app.paintShader;
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, 0, nrm.data());
    glTexCoordPointer(2, GL_FLOAT, 0, uv.data());
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, tan.data());
    gl.UseProgram(prog);
    gl.Uniform1i(gl.GetUniformLocation(prog, "colorMap"), 0);
    gl.Uniform1i(gl.GetUniformLocation(prog, "normalMap"), 1);
    gl.Uniform1i(gl.GetUniformLocation(prog, "matcap"), 2);
    gl.Uniform1i(gl.GetUniformLocation(prog, "mask"), 3);
    gl.Uniform2f(gl.GetUniformLocation(prog, "texel"), 1.0f / app.canvas->width(), 1.0f / app.canvas->height());
    gl.Uniform1f(gl.GetUniformLocation(prog, "mirror"), mirrored ? -1.0f : 1.0f);
    const GLint useMask = gl.GetUniformLocation(prog, "useMask");
    bindUnit(0, colorMap);
    bindUnit(1, app.normalTex);
    bindUnit(2, app.slotTex[0].matcap);
    gl.Uniform1f(useMask, 0);
    glDisable(GL_BLEND);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, idx.data());

    glEnable(GL_BLEND);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    gl.Uniform1f(useMask, 1);
    for (int slot = 1; slot < kMaxMaterials; ++slot) {
      const Material& mt = app.canvas->material(slot);
      const App::SlotTextures& st = app.slotTex[slot];
      if (!mt.used || mt.hidden || !st.matcap || !st.mask) continue;
      glBlendFunc(GL_SRC_ALPHA, mt.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
      bindUnit(2, st.matcap);
      bindUnit(3, st.mask);
      glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, idx.data());
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int unit = 3; unit >= 0; --unit) bindUnit(unit, 0);
    gl.UseProgram(0);
    gl.DisableVertexAttribArray(1);
    glDisableClientState(GL_NORMAL_ARRAY);
  } else {
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, colorMap);
    glTexCoordPointer(2, GL_FLOAT, 0, uv.data());
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, idx.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, app.slotTex[0].matcap);
    glTexCoordPointer(2, GL_FLOAT, 0, mc.data());
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, idx.data());
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
  }

  drawWireframe(idx);
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glFrontFace(GL_CCW);
}

ToolSliders& sliders(Tool tool) { return app.sliders[size_t(tool)]; }
std::array<float, 3>& paintSliders() { return app.paintSliders[size_t(app.paint.tool)]; }

// Brush radius at a visible surface point: proportional to its view depth.
float worldRadius(const Vec3& at) {
  float depth = dot(at - app.camera.eye(), app.camera.forward());
  if (app.paintMode) return paintBrushRadius(paintSliders()[0], depth, kFovSlider);
  return brushRadius(sliders(app.brush.tool).size, depth, kFovSlider);
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
  if (app.paintMode) {
    syncPaintTextures();
    drawPaintMesh(false);
    if (app.mesh.symmetric) drawPaintMesh(true);
  } else {
    glBindTexture(GL_TEXTURE_2D, app.matcap);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    drawMesh(false);
    if (app.mesh.symmetric) drawMesh(true);
  }
  glDisable(GL_CULL_FACE);
  drawBrushCursor();
  if (!present) return;
  SwapBuffers(app.hdc);

  char title[512];
  if (app.paintMode) {
    auto rgb = [](uint32_t c) { return (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF); };
    const std::array<float, 3>& ps = paintSliders();
    const Material& mt = app.canvas->material(app.editSlot);
    std::string matcap = mt.matcap.substr(mt.matcap.find_last_of(":/\\") + 1);
    char slot[160];
    if (app.editSlot == 0) std::snprintf(slot, sizeof slot, "paints color, base %s", matcap.c_str());
    else
      std::snprintf(slot, sizeof slot, "paints mask %d (%s%s%s%s)", app.editSlot, matcap.c_str(),
                    mt.colored ? ", colored" : "", mt.additive ? ", additive" : "", mt.hidden ? ", hidden" : "");
    std::snprintf(title, sizeof title,
                  "bildhauer - %s  size %.1f  strength %.1f  hardness %.1f  colors #%06X #%06X  %s %s|  %d tris  %s",
                  paintToolName(app.paint.tool), ps[0], ps[1], ps[2], rgb(app.paint.primary),
                  rgb(app.paint.secondary), slot, app.showBumpNormals ? "[bump normals] " : "", app.mesh.numTriangles(),
                  app.status.c_str());
  } else {
    std::snprintf(title, sizeof title, "bildhauer - %s  size %.1f  strength %.1f  %s%s|  %d tris  %s",
                  toolName(app.brush.tool), sliders(app.brush.tool).size, sliders(app.brush.tool).strength,
                  app.brush.clay ? "clay " : "", app.brush.global ? "global " : "", app.mesh.numTriangles(),
                  app.status.c_str());
  }
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
  in.point = h.visiblePoint();
  in.hit = h.hit;
  if (hitOut) *hitOut = h;
  return in;
}

// Shift sculpts with Smooth at the Smooth strength and the current tool's size.
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
}

void nudge(float& slider, float delta) { slider = std::clamp(slider + delta, 0.0f, 1.0f); }

// Ctrl inverts: the secondary color, lowering the bump, flattening away
// from the neutral height.
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
}

// Win32 color dialog; COLORREF has the canvas' byte order (0x00BBGGRR).
void pickColor(uint32_t& color) {
  static COLORREF custom[16] = {};
  CHOOSECOLORA cc{};
  cc.lStructSize = sizeof cc;
  cc.hwndOwner = app.hwnd;
  cc.rgbResult = color;
  cc.lpCustColors = custom;
  cc.Flags = CC_RGBINIT | CC_FULLOPEN;
  if (ChooseColorA(&cc)) color = cc.rgbResult & 0xFFFFFF;
}

// Matcap image for a material slot (file dialog).
void pickMatcapFile(Material& mt) {
  char file[MAX_PATH] = {};
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = app.hwnd;
  ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif\0All files\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = sizeof file;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn)) mt.matcap = file;
}

// The next built-in matcap after the slot's current one.
std::string nextBuiltinMatcap(const std::string& current) {
  const std::vector<std::string>& names = builtinMatcaps();
  auto it = std::find(names.begin(), names.end(), current);
  return it == names.end() || it + 1 == names.end() ? names[0] : *(it + 1);
}

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
    case 'L': {
      int last = 0;
      for (int s = 0; s < kMaxMaterials; ++s)
        if (cv.material(s).used) last = s;
      const int slot = cv.addLayer(nextBuiltinMatcap(cv.material(last).matcap));
      if (slot < 0) {
        app.status = "all 16 material slots are in use";
        return;
      }
      cv.material(slot).colored = true;
      app.editSlot = slot;
      break;
    }
    case 'M': mt.matcap = nextBuiltinMatcap(mt.matcap); break;
    case 'O': pickMatcapFile(mt); break;
    case 'C': mt.colored = !mt.colored; break;
    case 'A': if (app.editSlot > 0) mt.additive = !mt.additive; break;
    case 'H': if (app.editSlot > 0) mt.hidden = !mt.hidden; break;
  }
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
    if (key == 'Z' && !app.canvas->undo()) app.status = "nothing to undo";
    if (key == 'S') saveModel();
    return;
  }
  switch (key) {
    case 'D': app.paint.tool = PaintTool::Color; break;
    case 'B': app.paint.tool = PaintTool::Bump; break;
    case 'F': app.paint.tool = PaintTool::FlattenBump; break;
    case 'K': pickColor(shift ? app.paint.secondary : app.paint.primary); break;
    case 'N': app.showBumpNormals = !app.showBumpNormals; break;
    case 'W': app.wireframe = !app.wireframe; break;
    default: onMaterialKey(key);
  }
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
  if (app.paintMode) {
    onPaintKey(key, ctrl, GetKeyState(VK_SHIFT) < 0);
    return;
  }
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
      case 'Z':
        if (!app.undo.empty()) { app.mesh = std::move(app.undo.back()); app.undo.pop_back(); }
        break;
      case 'S': saveModel(); break;
      case 'D': pushUndo(); subdivide(app.mesh, {true, app.mesh.symmetric}); break;
      case 'Y': pushUndo(); reduceMesh(app.mesh); break;
      case 'P': enterPaintMode(); break;
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

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      app.width = std::max(1, int(LOWORD(lp)));
      app.height = std::max(1, int(HIWORD(lp)));
      return 0;
    case WM_LBUTTONDOWN:
      SetCapture(hwnd);
      if (GetKeyState(VK_MENU) < 0) app.orbiting = true;
      else if (app.paintMode) beginPaintStroke(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_CONTROL) != 0);
      else beginStroke(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_CONTROL) != 0, (wp & MK_SHIFT) != 0);
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
      app.paintStroke.reset();
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
    beginStroke(app.width / 2 - 130, y0, invert, false);
    for (int i = 0; i <= 40; ++i)
      if (app.stroke) app.stroke->moveTo(inputAt(app.width / 2 - 130 + i * 6, y0 + int(20 * std::sin(i * 0.3))));
    app.stroke.reset();
  };
  sliders(Tool::Draw) = sliders(Tool::Mask) = {0.4f, 0.5f};
  strokeAcross(Tool::Draw, false, app.height / 2 - 90);
  strokeAcross(Tool::Draw, true, app.height / 2);
  strokeAcross(Tool::Mask, false, app.height / 2 + 90);
  app.brush.tool = Tool::Draw;
}

// Paint strokes across the front: color, secondary color, bump.
void runPaintDemo() {
  auto strokeAcross = [&](PaintTool tool, bool invert, int y0) {
    app.paint.tool = tool;
    beginPaintStroke(app.width / 2 - 70, y0, invert);
    for (int i = 0; i <= 40; ++i) {
      if (!app.paintStroke) break;
      StrokeInput in = inputAt(app.width / 2 - 70 + i * 4, y0 + int(20 * std::sin(i * 0.3)));
      app.paintStroke->moveTo(in.point, in.hit);
    }
    app.paintStroke.reset();
  };
  strokeAcross(PaintTool::Color, false, app.height / 2 - 110);
  strokeAcross(PaintTool::Color, true, app.height / 2 - 30);
  strokeAcross(PaintTool::Bump, false, app.height / 2 + 50);
  // A gold layer, painted in across the forehead.
  app.editSlot = app.canvas->addLayer("bildhauer:gold");
  app.canvas->material(app.editSlot).colored = true;
  strokeAcross(PaintTool::Color, false, app.height / 2 - 170);
  app.paint.tool = PaintTool::Color;
  app.editSlot = 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // Windows Imaging Component (matcap images)
  std::string modelPath, snapshot;
  bool demo = false, paint = false, save = false;
  int frames = 0;
  for (int i = 1; i < __argc; ++i) {
    std::string a = __argv[i];
    if (a == "--snapshot" && i + 1 < __argc) snapshot = __argv[++i];
    else if (a == "--frames" && i + 1 < __argc) frames = std::atoi(__argv[++i]);
    else if (a == "--demo") demo = true;
    else if (a == "--paint") paint = true;
    else if (a == "--bump-normals") app.showBumpNormals = true;
    else if (a == "--save") save = true;
    else modelPath = a;
  }
  for (int t = 0; t < int(app.sliders.size()); ++t) app.sliders[size_t(t)] = defaultSliders(Tool(t));
  app.paintSliders = {{{0.3f, 1.0f, 0.5f}, {0.3f, 0.4f, 0.5f}, {0.3f, 0.4f, 0.5f}}};
  if (!modelPath.empty()) {
    if (!loadModel(modelPath)) return 1;
  } else {
    makeIcosphere(app.mesh, 3);
  }
  if (!createWindow(instance)) {
    MessageBoxA(nullptr, "Could not create an OpenGL window.", "bildhauer", MB_ICONERROR);
    return 1;
  }
  app.matcap = matcapTexture(builtinMatcaps()[0], false, nullptr);
  initPaintShader();
  frameModel();
  if (!snapshot.empty()) {
    RECT rc;
    GetClientRect(app.hwnd, &rc);
    app.width = rc.right;
    app.height = rc.bottom;
    if (paint) enterPaintMode();
    if (demo) app.paintMode ? runPaintDemo() : runDemo();
    if (save) saveModel();
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
    // One beautify pass per frame while vertices are dirty (mesh-core.md 5.2);
    // it keeps relaxing after the stroke until the mesh has settled.
    bool relaxing = (app.beautify || app.brush.detailFactor > 0.15f) && !app.mesh.dirtyVertices.empty();
    if (relaxing) beautifyPass(app.mesh);
    render();
    if (!app.stroke && !app.paintStroke && !relaxing) WaitMessage();  // idle until the next input
  }
}

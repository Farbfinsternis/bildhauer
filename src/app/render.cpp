#include "app/render.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "app/gl2.h"
#include "app/gpu_mesh.h"
#include "app/matcaps.h"

namespace bh::viewer {

namespace {

// The sculpt mesh stays in buffers on the graphics card, and the shader does
// the matcap lookup and mask tint that drawMesh computes per vertex each frame.
const char* kSculptVertexShader = R"(#version 120
uniform float mirror;
uniform vec3 cameraRight, cameraUp;
varying vec2 matcapUv;
varying vec3 tint;
void main() {
  vec3 flip = vec3(mirror, 1.0, 1.0);
  vec3 n = gl_Normal * flip;
  matcapUv = vec2(0.5 + 0.49 * dot(n, cameraRight), 0.5 + 0.49 * dot(n, cameraUp));
  float weight = gl_MultiTexCoord0.x;
  float shade = weight < 0.0 ? 0.5 : 0.35 + 0.65 * weight;
  tint = vec3(shade, shade, weight >= 0.0 && weight < 1.0 ? shade * 0.8 : shade);
  gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xyz * flip, 1.0);
})";

const char* kSculptFragmentShader = R"(#version 120
uniform sampler2D matcap;
varying vec2 matcapUv;
varying vec3 tint;
void main() {
  gl_FragColor = vec4(texture2D(matcap, matcapUv).rgb * tint, 1.0);
})";

GLuint sculptProgram = 0, sculptVertices = 0, sculptIndices = 0;  // 0: drawMesh instead
GpuMesh gpu;
size_t vertexCapacity = 0, indexCapacity = 0;

// Sends what changed. The buffer grows ahead of the mesh, so that a stroke
// adding triangles does not reallocate it each frame.
template <class T>
void send(GLenum target, GLuint buffer, const std::vector<T>& data, const std::vector<GpuRange>& ranges, size_t& capacity) {
  using gl2::gl;
  gl.BindBuffer(target, buffer);
  if (data.size() > capacity) {
    capacity = data.size() + data.size() / 4 + 1024;
    gl.BufferData(target, ptrdiff_t(capacity * sizeof(T)), nullptr, gl2::kDynamicDraw);
    gl.BufferSubData(target, 0, ptrdiff_t(data.size() * sizeof(T)), data.data());
    return;
  }
  for (const GpuRange& r : ranges)
    gl.BufferSubData(target, ptrdiff_t(r.first * sizeof(T)), ptrdiff_t(r.count * sizeof(T)), data.data() + r.first);
}

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
  sculptProgram = gl2::buildProgram(kSculptVertexShader, kSculptFragmentShader, "unused", &err);
  if (!sculptProgram) {
    app.status = "sculpt shader: " + err;
    return;
  }
  gl2::gl.GenBuffers(1, &sculptVertices);
  gl2::gl.GenBuffers(1, &sculptIndices);
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

// The stored mesh and, when symmetric, its mirror image, from the buffers.
void drawSculptBuffers() {
  using gl2::gl;
  gpu.update(app.mesh, false);
  send(gl2::kArrayBuffer, sculptVertices, gpu.vertices, gpu.vertexRanges, vertexCapacity);
  send(gl2::kElementArrayBuffer, sculptIndices, gpu.indices, gpu.indexRanges, indexCapacity);
  const GLsizei stride = sizeof(GpuVertex), count = GLsizei(gpu.indices.size());
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glVertexPointer(3, GL_FLOAT, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, position)));
  glNormalPointer(GL_FLOAT, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, normal)));
  glTexCoordPointer(1, GL_FLOAT, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, weight)));
  const Vec3 r = app.camera.right(), u = app.camera.up();
  for (bool mirrored : {false, true}) {
    if (mirrored && !app.mesh.symmetric) break;
    glFrontFace(mirrored ? GL_CW : GL_CCW);
    gl.UseProgram(sculptProgram);
    gl.Uniform1i(gl.GetUniformLocation(sculptProgram, "matcap"), 0);
    gl.Uniform1f(gl.GetUniformLocation(sculptProgram, "mirror"), mirrored ? -1.0f : 1.0f);
    gl.Uniform3f(gl.GetUniformLocation(sculptProgram, "cameraRight"), r.x, r.y, r.z);
    gl.Uniform3f(gl.GetUniformLocation(sculptProgram, "cameraUp"), u.x, u.y, u.z);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    gl.UseProgram(0);
    if (!app.wireframe) continue;
    glPushMatrix();
    if (mirrored) glScalef(-1, 1, 1);
    glColor4f(0.1f, 0.1f, 0.12f, 0.35f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1, -1);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glPopMatrix();
  }
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  gl.BindBuffer(gl2::kArrayBuffer, 0);  // the UI draws from its own arrays
  gl.BindBuffer(gl2::kElementArrayBuffer, 0);
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

// Sculptris marks the mirror plane on the mesh with a faint light line: the
// stored half's edges on x = 0, pulled slightly toward the eye against
// z-fighting.
void drawSymmetryLine() {
  const Mesh& m = app.mesh;
  const Vec3 eye = app.camera.eye();
  glLineWidth(2);
  glColor4f(1, 1, 1, 0.18f);
  glBegin(GL_LINES);
  for (int i = 0; i < m.edgeSlots(); ++i) {
    const Edge& e = m.edge(i);
    if (!e.active || !m.vertex(e.v[0]).onSymmetryPlane || !m.vertex(e.v[1]).onSymmetryPlane) continue;
    for (int v : e.v) {
      const Vec3 p = m.vertex(v).pos, q = p + (eye - p) * 0.002f;
      glVertex3f(q.x, q.y, q.z);
    }
  }
  glEnd();
  glLineWidth(1);
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

}  // namespace

void renderScene() {
  glViewport(0, 0, app.width, app.height);
  glClearColor(0.16f, 0.17f, 0.19f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(app.viewX, app.height - app.viewY - app.viewH, app.viewW, app.viewH);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  float nearZ = app.sceneSize * 0.01f, farZ = app.sceneSize * 50;
  float top = nearZ * std::tan(kFov / 2), right = top * float(app.viewW) / float(std::max(app.viewH, 1));
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
    glBindTexture(GL_TEXTURE_2D, builtinMatcapTexture(app.sculptMatcap));
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (sculptProgram) {
      drawSculptBuffers();
    } else {
      drawMesh(false);
      if (app.mesh.symmetric) drawMesh(true);
    }
    if (app.mesh.symmetric) drawSymmetryLine();
  }
  glDisable(GL_CULL_FACE);
  drawBrushCursor();
}

unsigned int builtinMatcapTexture(const std::string& name) {
  static std::vector<std::pair<std::string, GLuint>> made;
  for (const auto& [n, tex] : made)
    if (n == name) return tex;
  made.emplace_back(name, matcapTexture(name, false, nullptr));
  return made.back().second;
}

void initRendering() {
  initPaintShader();
}

}  // namespace bh::viewer

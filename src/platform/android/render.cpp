#include "platform/android/render.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/gpu_mesh.h"
#include "app/matcaps.h"

namespace bh::viewer {
namespace {

GLuint program = 0, vao = 0, vertices = 0, triangles = 0, lines = 0, cursor = 0, cursorVao = 0;
GLsizei triangleCount = 0, lineCount = 0;
GpuMesh gpu;
size_t vertexCapacity = 0, indexCapacity = 0;
// geometryDirty renews everything; otherwise the mesh reports what changed.
bool geometryDirty = true, linesDirty = true;
// One texture per built-in matcap; the choice survives a lost GL context.
std::vector<GLuint> matcaps;
int currentMatcap = -1;
float density = 1;  // pixels per dp

GLuint shader(GLenum type, const char* source) {
  GLuint id = glCreateShader(type);
  glShaderSource(id, 1, &source, nullptr);
  glCompileShader(id);
  GLint ok;
  glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048]{};
    glGetShaderInfoLog(id, sizeof log, nullptr, log);
    throw std::runtime_error(log);
  }
  return id;
}

// Sends what changed. The buffer grows ahead of the mesh, so that a stroke
// adding triangles does not reallocate it each frame.
template <class T>
void send(GLenum target, GLuint buffer, const std::vector<T>& data, const std::vector<GpuRange>& ranges, size_t& capacity) {
  glBindBuffer(target, buffer);
  if (data.size() > capacity) {
    capacity = data.size() + data.size() / 4 + 1024;
    glBufferData(target, GLsizeiptr(capacity * sizeof(T)), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(target, 0, GLsizeiptr(data.size() * sizeof(T)), data.data());
    return;
  }
  for (const GpuRange& r : ranges)
    glBufferSubData(target, GLintptr(r.first * sizeof(T)), GLsizeiptr(r.count * sizeof(T)), data.data() + r.first);
}

void syncGeometry() {
  gpu.update(app.mesh, geometryDirty);
  geometryDirty = false;
  linesDirty |= gpu.topologyChanged;
  glBindVertexArray(vao);
  send(GL_ARRAY_BUFFER, vertices, gpu.vertices, gpu.vertexRanges, vertexCapacity);
  send(GL_ELEMENT_ARRAY_BUFFER, triangles, gpu.indices, gpu.indexRanges, indexCapacity);
  triangleCount = GLsizei(gpu.indices.size());
  if (!app.wireframe || !linesDirty) return;
  const Mesh& m = app.mesh;
  std::vector<uint32_t> edges;
  for (int i = 0; i < m.edgeSlots(); ++i) {
    const Edge& e = m.edge(i);
    if (e.active && !e.quadDiagonal) { edges.push_back(uint32_t(e.v[0])); edges.push_back(uint32_t(e.v[1])); }
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lines);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(edges.size() * sizeof(uint32_t)), edges.data(), GL_DYNAMIC_DRAW);
  lineCount = GLsizei(edges.size());
  linesDirty = false;
}

void drawHalf(float mirror) {
  glUniform1f(glGetUniformLocation(program, "mirror"), mirror);
  glFrontFace(mirror < 0 ? GL_CW : GL_CCW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, triangles);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1, 1);
  glDrawElements(GL_TRIANGLES, triangleCount, GL_UNSIGNED_INT, nullptr);
  glDisable(GL_POLYGON_OFFSET_FILL);
  if (app.wireframe) {
    glUniform4f(glGetUniformLocation(program, "solid"), 0.08f, 0.09f, 0.1f, 1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lines);
    glDrawElements(GL_LINES, lineCount, GL_UNSIGNED_INT, nullptr);
    glUniform4f(glGetUniformLocation(program, "solid"), 0, 0, 0, 0);
  }
}

}  // namespace

void invalidateMobileGeometry() { geometryDirty = true; }
void syncMobileGeometry() { syncGeometry(); }
void setMobileDensity(float pixelsPerDp) { density = pixelsPerDp; }

int mobileMatcapCount() { return int(matcaps.size()); }
unsigned mobileMatcapTexture(int i) { return matcaps[size_t(i)]; }
int mobileMatcap() { return currentMatcap; }
void setMobileMatcap(int i) { currentMatcap = i; app.matcap = matcaps[size_t(i)]; }

void initMobileRendering() {
  // The old context may no longer exist. Every GPU name is replaced here.
  const char* vs = R"(#version 300 es
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in float weight;
uniform mat4 view, projection;
uniform float mirror;
out vec3 n;
out float maskWeight, worldX;
void main() {
  vec3 p = position * vec3(mirror, 1, 1);
  n = mat3(view) * (normal * vec3(mirror, 1, 1));
  maskWeight = max(weight, 0.0);
  worldX = p.x;
  gl_Position = projection * view * vec4(p, 1);
})";
  const char* fs = R"(#version 300 es
precision highp float;
in vec3 n;
in float maskWeight, worldX;
uniform sampler2D matcap;
uniform vec4 solid;
uniform float symmetryLine;
out vec4 color;
void main() {
  vec3 clay = texture(matcap, normalize(n).xy * 0.49 + 0.5).rgb;
  clay *= mix(vec3(0.35,0.35,0.28), vec3(1), maskWeight);
  // Sculptris marks the mirror plane with a faint light line.
  // symmetryLine: half its width in pixels, 0 without symmetry.
  float px = abs(worldX) / max(fwidth(worldX), 1e-6);
  float line = symmetryLine > 0.0 ? 1.0 - smoothstep(symmetryLine, symmetryLine + 1.0, px) : 0.0;
  clay = mix(clay, vec3(1), 0.22 * line);
  color = solid.a > 0.5 ? solid : vec4(clay, 1);
})";
  GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
  program = glCreateProgram();
  glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
  glDeleteShader(v); glDeleteShader(f);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) throw std::runtime_error("Cannot link the Android sculpt shader");
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vertices); glGenBuffers(1, &triangles); glGenBuffers(1, &lines);
  glBindBuffer(GL_ARRAY_BUFFER, vertices);
  for (GLuint a = 0; a < 3; ++a) glEnableVertexAttribArray(a);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, position)));
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, normal)));
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, weight)));
  glGenVertexArrays(1, &cursorVao); glGenBuffers(1, &cursor);
  glBindVertexArray(cursorVao); glBindBuffer(GL_ARRAY_BUFFER, cursor);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
  const std::vector<std::string>& names = builtinMatcaps();
  matcaps.assign(names.size(), 0);
  glGenTextures(GLsizei(matcaps.size()), matcaps.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (size_t i = 0; i < names.size(); ++i) {
    MatcapImage image;
    loadMatcap(names[i], false, image, nullptr);
    glBindTexture(GL_TEXTURE_2D, matcaps[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_BYTE, image.rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if (currentMatcap < 0)  // red wax, as in Sculptris
    currentMatcap = int(std::find(names.begin(), names.end(), "bildhauer:redwax") - names.begin()) % int(names.size());
  app.matcap = matcaps[size_t(currentMatcap)];
  glBindVertexArray(0);
  geometryDirty = linesDirty = true;
  vertexCapacity = indexCapacity = 0;  // the buffers are new
  __android_log_print(ANDROID_LOG_INFO, "bildhauer", "OpenGL ES: %s", glGetString(GL_VERSION));
}

void renderMobileScene() {
  glViewport(0, 0, app.width, app.height);
  glClearColor(0.16f, 0.17f, 0.19f, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(app.viewX, app.height - app.viewY - app.viewH, app.viewW, app.viewH);
  const Camera& camera = app.camera;
  Vec3 r = camera.right(), u = camera.up(), f = camera.forward(), e = camera.eye();
  const float view[16] = {r.x,u.x,-f.x,0, r.y,u.y,-f.y,0, r.z,u.z,-f.z,0, -dot(r,e),-dot(u,e),dot(f,e),1};
  float n = app.sceneSize * 0.01f, z = app.sceneSize * 50, cot = 1 / std::tan(kFov / 2);
  float aspect = float(app.viewW) / float(std::max(1, app.viewH));
  const float projection[16] = {cot/aspect,0,0,0, 0,cot,0,0, 0,0,-(z+n)/(z-n),-1, 0,0,-2*z*n/(z-n),0};
  syncGeometry();
  glUseProgram(program);
  glUniformMatrix4fv(glGetUniformLocation(program,"view"),1,GL_FALSE,view);
  glUniformMatrix4fv(glGetUniformLocation(program,"projection"),1,GL_FALSE,projection);
  glUniform4f(glGetUniformLocation(program,"solid"),0,0,0,0);
  glUniform1i(glGetUniformLocation(program,"matcap"),0);
  // About 2 dp wide, as in Sculptris; in pixels it grows with the screen density.
  glUniform1f(glGetUniformLocation(program,"symmetryLine"),app.mesh.symmetric ? density : 0);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,app.matcap);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glDisable(GL_BLEND);
  glBindVertexArray(vao);
  drawHalf(1);
  if (app.mesh.symmetric) drawHalf(-1);
  glFrontFace(GL_CCW);
  glDisable(GL_CULL_FACE);
  if (app.hover.hit) {
    Vec3 c = app.hover.visiblePoint(), normal = app.hover.normal;
    if (app.hover.mirrored) normal.x = -normal.x;
    Vec3 a = normalize(cross(normal, std::fabs(normal.y) < 0.9f ? Vec3{0,1,0} : Vec3{1,0,0})), b = cross(normal,a);
    std::array<Vec3,48> ring;
    const float radius = worldRadius(c);
    for (size_t i=0;i<ring.size();++i) {
      float t = float(i) * 2 * kPi / float(ring.size());
      ring[i] = c + (a*std::cos(t)+b*std::sin(t))*radius;
    }
    glBindVertexArray(cursorVao); glBindBuffer(GL_ARRAY_BUFFER,cursor);
    glBufferData(GL_ARRAY_BUFFER,sizeof(ring),ring.data(),GL_DYNAMIC_DRAW);
    glUniform1f(glGetUniformLocation(program,"mirror"),1);
    glUniform1f(glGetUniformLocation(program,"symmetryLine"),0);
    glUniform4f(glGetUniformLocation(program,"solid"),1,0.85f,0.3f,1);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINE_LOOP,0,GLsizei(ring.size()));
  }
  glBindVertexArray(0);
  glUseProgram(0);
}

}  // namespace bh::viewer

#pragma once

// The OpenGL 2.0 entry points the paint shading needs, loaded with
// wglGetProcAddress (the Windows SDK only declares OpenGL 1.1), and a helper
// to build a shader program.

#include <windows.h>

#include <GL/gl.h>

#include <string>

namespace bh::gl2 {

constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kClampToEdge = 0x812F;

struct Functions {
  GLuint(APIENTRY* CreateShader)(GLenum) = nullptr;
  void(APIENTRY* ShaderSource)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
  void(APIENTRY* CompileShader)(GLuint) = nullptr;
  void(APIENTRY* GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
  void(APIENTRY* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
  GLuint(APIENTRY* CreateProgram)() = nullptr;
  void(APIENTRY* AttachShader)(GLuint, GLuint) = nullptr;
  void(APIENTRY* BindAttribLocation)(GLuint, GLuint, const char*) = nullptr;
  void(APIENTRY* LinkProgram)(GLuint) = nullptr;
  void(APIENTRY* GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
  void(APIENTRY* GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
  void(APIENTRY* UseProgram)(GLuint) = nullptr;
  GLint(APIENTRY* GetUniformLocation)(GLuint, const char*) = nullptr;
  void(APIENTRY* Uniform1i)(GLint, GLint) = nullptr;
  void(APIENTRY* Uniform1f)(GLint, GLfloat) = nullptr;
  void(APIENTRY* Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
  void(APIENTRY* ActiveTexture)(GLenum) = nullptr;
  void(APIENTRY* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
  void(APIENTRY* EnableVertexAttribArray)(GLuint) = nullptr;
  void(APIENTRY* DisableVertexAttribArray)(GLuint) = nullptr;
};

extern Functions gl;

// Loads the entry points into `gl` (with a current context). False if the
// driver lacks any of them.
bool load();

// Compiles and links; attribute `attrib` is bound to location 1. Returns 0
// and the compiler log in `error` on failure.
GLuint buildProgram(const char* vertexSource, const char* fragmentSource, const char* attrib, std::string* error);

}  // namespace bh::gl2

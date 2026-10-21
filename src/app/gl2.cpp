#include "app/gl2.h"

namespace bh::gl2 {

Functions gl;

namespace {

template <class F>
bool loadOne(F& f, const char* name) {
  f = reinterpret_cast<F>(wglGetProcAddress(name));
  return f != nullptr;
}

GLuint compile(GLenum type, const char* source, std::string* error) {
  GLuint s = gl.CreateShader(type);
  gl.ShaderSource(s, 1, &source, nullptr);
  gl.CompileShader(s);
  GLint ok = 0;
  gl.GetShaderiv(s, kCompileStatus, &ok);
  if (ok) return s;
  char log[2048] = {};
  gl.GetShaderInfoLog(s, sizeof log, nullptr, log);
  if (error) *error = std::string(type == kVertexShader ? "vertex shader: " : "fragment shader: ") + log;
  return 0;
}

}  // namespace

bool load() {
  bool ok = true;
  ok &= loadOne(gl.CreateShader, "glCreateShader");
  ok &= loadOne(gl.ShaderSource, "glShaderSource");
  ok &= loadOne(gl.CompileShader, "glCompileShader");
  ok &= loadOne(gl.GetShaderiv, "glGetShaderiv");
  ok &= loadOne(gl.GetShaderInfoLog, "glGetShaderInfoLog");
  ok &= loadOne(gl.CreateProgram, "glCreateProgram");
  ok &= loadOne(gl.AttachShader, "glAttachShader");
  ok &= loadOne(gl.BindAttribLocation, "glBindAttribLocation");
  ok &= loadOne(gl.LinkProgram, "glLinkProgram");
  ok &= loadOne(gl.GetProgramiv, "glGetProgramiv");
  ok &= loadOne(gl.GetProgramInfoLog, "glGetProgramInfoLog");
  ok &= loadOne(gl.UseProgram, "glUseProgram");
  ok &= loadOne(gl.GetUniformLocation, "glGetUniformLocation");
  ok &= loadOne(gl.Uniform1i, "glUniform1i");
  ok &= loadOne(gl.Uniform1f, "glUniform1f");
  ok &= loadOne(gl.Uniform2f, "glUniform2f");
  ok &= loadOne(gl.ActiveTexture, "glActiveTexture");
  ok &= loadOne(gl.VertexAttribPointer, "glVertexAttribPointer");
  ok &= loadOne(gl.EnableVertexAttribArray, "glEnableVertexAttribArray");
  ok &= loadOne(gl.DisableVertexAttribArray, "glDisableVertexAttribArray");
  return ok;
}

GLuint buildProgram(const char* vertexSource, const char* fragmentSource, const char* attrib, std::string* error) {
  GLuint vs = compile(kVertexShader, vertexSource, error);
  GLuint fs = vs ? compile(kFragmentShader, fragmentSource, error) : 0;
  if (!fs) return 0;
  GLuint p = gl.CreateProgram();
  gl.AttachShader(p, vs);
  gl.AttachShader(p, fs);
  gl.BindAttribLocation(p, 1, attrib);
  gl.LinkProgram(p);
  GLint ok = 0;
  gl.GetProgramiv(p, kLinkStatus, &ok);
  if (ok) return p;
  char log[2048] = {};
  gl.GetProgramInfoLog(p, sizeof log, nullptr, log);
  if (error) *error = std::string("link: ") + log;
  return 0;
}

}  // namespace bh::gl2

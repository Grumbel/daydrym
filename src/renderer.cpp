// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "renderer.hpp"

#include <SDL.h>
#include <cstdio>
#include <string>

#if defined(USE_GLES) || defined(__ANDROID__)
#  include <GLES3/gl3.h>
#else
#  include <GL/gl.h>
// Minimal subset of GL 3.3 core if the system headers are incomplete
#  ifndef GL_VERTEX_SHADER
#    error "Desktop OpenGL headers incomplete; install mesa-libGL-devel or similar"
#  endif
#endif

static const char* kVertSrc = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
  v_color = a_color;
}
)";

static const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec3 v_color;
out vec4 o_color;
void main() {
  o_color = vec4(v_color, 1.0);
}
)";

// Desktop GL 3.3 core variants (no "es", higher precision default)
static const char* kVertSrcDesktop = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
  v_color = a_color;
}
)";

static const char* kFragSrcDesktop = R"(#version 330 core
in vec3 v_color;
out vec4 o_color;
void main() {
  o_color = vec4(v_color, 1.0);
}
)";

static unsigned int compile_shader(GLenum type, const char* src) {
  unsigned int s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Shader compile error: %s\n", log);
  }
  return s;
}

Renderer::Renderer() = default;

Renderer::~Renderer() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
}

bool Renderer::load_shaders() {
#if defined(USE_GLES) || defined(__ANDROID__)
  const char* vs = kVertSrc;
  const char* fs = kFragSrc;
#else
  const char* vs = kVertSrcDesktop;
  const char* fs = kFragSrcDesktop;
#endif
  unsigned int vert = compile_shader(GL_VERTEX_SHADER, vs);
  unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, fs);
  program_ = glCreateProgram();
  glAttachShader(program_, vert);
  glAttachShader(program_, frag);
  glLinkProgram(program_);
  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Program link error: %s\n", log);
    return false;
  }
  glDeleteShader(vert);
  glDeleteShader(frag);
  u_mvp_ = glGetUniformLocation(program_, "u_mvp");
  return true;
}

bool Renderer::init(int width, int height) {
  width_ = width;
  height_ = height;

  if (!load_shaders()) return false;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  // position
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, px)));
  // color
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, r)));
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST);
  glClearColor(0.45f, 0.65f, 0.95f, 1.f); // sky blue
  return true;
}

void Renderer::resize(int width, int height) {
  width_ = width;
  height_ = height;
  glViewport(0, 0, width, height);
}

void Renderer::begin_frame() {
  glViewport(0, 0, width_, height_);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::end_frame() {
  // nothing; SDL_GL_SwapWindow is done by caller
}

void Renderer::draw(const std::vector<Vertex>& vertices, const Mat4& mvp) {
  if (vertices.empty()) return;
  glUseProgram(program_);
  glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, mvp.m);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
}

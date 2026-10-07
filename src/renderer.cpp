// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "renderer.hpp"

#include <SDL.h>
#include <cstdio>
#include <vector>
#include <cmath>

#if defined(USE_GLES) || defined(__ANDROID__)
#  include <GLES3/gl3.h>
#else
#  define GL_GLEXT_PROTOTYPES 1
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

// ---------------------------------------------------------------------------
// Shaders (ES 3.00 / desktop 330 core)
// ---------------------------------------------------------------------------

static const char* kDepthVertES = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char* kDepthFragES = R"(#version 300 es
precision mediump float;
void main() {}
)";

static const char* kLitVertES = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat4 u_normal_mat; // upper-left 3x3 of inverse-transpose(model), as mat4
uniform mat4 u_light_matrix;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_color;
out vec4 v_shadow_coord;

void main() {
  vec4 world = u_model * vec4(a_pos, 1.0);
  v_world_pos = world.xyz;
  v_normal = normalize(mat3(u_normal_mat) * a_normal);
  v_uv = a_uv;
  v_color = a_color;
  v_shadow_coord = u_light_matrix * world;
  gl_Position = u_proj * u_view * world;
}
)";

static const char* kLitFragES = R"(#version 300 es
precision mediump float;
in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_color;
in vec4 v_shadow_coord;

uniform vec3 u_light_dir;   // normalized, points toward scene
uniform vec3 u_light_color;
uniform float u_ambient;
uniform float u_intensity;
uniform vec3 u_view_pos;
uniform sampler2D u_albedo;
uniform sampler2D u_shadow_map;

out vec4 o_color;

float shadow_factor(vec4 sc) {
  // Perspective divide + map to [0,1]
  vec3 proj = sc.xyz / sc.w;
  proj = proj * 0.5 + 0.5;
  if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    return 1.0;

  float bias = 0.003;
  float current = proj.z - bias;

  // 3x3 PCF
  float shadow = 0.0;
  vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0));
  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      float closest = texture(u_shadow_map, proj.xy + vec2(float(x), float(y)) * texel).r;
      shadow += current > closest ? 0.0 : 1.0;
    }
  }
  return shadow / 9.0;
}

void main() {
  vec3 N = normalize(v_normal);
  vec3 L = normalize(-u_light_dir);
  vec3 V = normalize(u_view_pos - v_world_pos);
  vec3 H = normalize(L + V);

  float diff = max(dot(N, L), 0.0);
  float spec = pow(max(dot(N, H), 0.0), 48.0);

  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  float sh = shadow_factor(v_shadow_coord);

  vec3 ambient = u_ambient * albedo;
  vec3 diffuse = diff * u_light_color * u_intensity * albedo * sh;
  vec3 specular = spec * u_light_color * u_intensity * 0.35 * sh;

  o_color = vec4(ambient + diffuse + specular, 1.0);
}
)";

// Desktop variants (330 core)
static const char* kDepthVertGL = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char* kDepthFragGL = R"(#version 330 core
void main() {}
)";

static const char* kLitVertGL = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat4 u_normal_mat;
uniform mat4 u_light_matrix;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_color;
out vec4 v_shadow_coord;

void main() {
  vec4 world = u_model * vec4(a_pos, 1.0);
  v_world_pos = world.xyz;
  v_normal = normalize(mat3(u_normal_mat) * a_normal);
  v_uv = a_uv;
  v_color = a_color;
  v_shadow_coord = u_light_matrix * world;
  gl_Position = u_proj * u_view * world;
}
)";

static const char* kLitFragGL = R"(#version 330 core
in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_color;
in vec4 v_shadow_coord;

uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform float u_ambient;
uniform float u_intensity;
uniform vec3 u_view_pos;
uniform sampler2D u_albedo;
uniform sampler2D u_shadow_map;

out vec4 o_color;

float shadow_factor(vec4 sc) {
  vec3 proj = sc.xyz / sc.w;
  proj = proj * 0.5 + 0.5;
  if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
    return 1.0;

  float bias = 0.003;
  float current = proj.z - bias;

  float shadow = 0.0;
  vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0));
  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      float closest = texture(u_shadow_map, proj.xy + vec2(float(x), float(y)) * texel).r;
      shadow += current > closest ? 0.0 : 1.0;
    }
  }
  return shadow / 9.0;
}

void main() {
  vec3 N = normalize(v_normal);
  vec3 L = normalize(-u_light_dir);
  vec3 V = normalize(u_view_pos - v_world_pos);
  vec3 H = normalize(L + V);

  float diff = max(dot(N, L), 0.0);
  float spec = pow(max(dot(N, H), 0.0), 48.0);

  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  float sh = shadow_factor(v_shadow_coord);

  vec3 ambient = u_ambient * albedo;
  vec3 diffuse = diff * u_light_color * u_intensity * albedo * sh;
  vec3 specular = spec * u_light_color * u_intensity * 0.35 * sh;

  o_color = vec4(ambient + diffuse + specular, 1.0);
}
)";

// ---------------------------------------------------------------------------

unsigned int Renderer::compile_shader(unsigned int type, const char* src) {
  unsigned int s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Shader compile error: %s\n", log);
  }
  return s;
}

unsigned int Renderer::link_program(unsigned int vs, unsigned int fs) {
  unsigned int p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Program link error: %s\n", log);
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}

Renderer::Renderer() = default;

Renderer::~Renderer() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (lit_program_) glDeleteProgram(lit_program_);
  if (depth_program_) glDeleteProgram(depth_program_);
  if (albedo_tex_) glDeleteTextures(1, &albedo_tex_);
  if (shadow_tex_) glDeleteTextures(1, &shadow_tex_);
  if (shadow_fbo_) glDeleteFramebuffers(1, &shadow_fbo_);
}

bool Renderer::load_programs() {
#if defined(USE_GLES) || defined(__ANDROID__)
  const char* dv = kDepthVertES; const char* df = kDepthFragES;
  const char* lv = kLitVertES;   const char* lf = kLitFragES;
#else
  const char* dv = kDepthVertGL; const char* df = kDepthFragGL;
  const char* lv = kLitVertGL;   const char* lf = kLitFragGL;
#endif

  depth_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, dv),
      compile_shader(GL_FRAGMENT_SHADER, df));
  lit_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, lv),
      compile_shader(GL_FRAGMENT_SHADER, lf));

  u_depth_mvp_ = glGetUniformLocation(depth_program_, "u_mvp");

  u_model_ = glGetUniformLocation(lit_program_, "u_model");
  u_view_ = glGetUniformLocation(lit_program_, "u_view");
  u_proj_ = glGetUniformLocation(lit_program_, "u_proj");
  u_normal_mat_ = glGetUniformLocation(lit_program_, "u_normal_mat");
  u_light_dir_ = glGetUniformLocation(lit_program_, "u_light_dir");
  u_light_color_ = glGetUniformLocation(lit_program_, "u_light_color");
  u_ambient_ = glGetUniformLocation(lit_program_, "u_ambient");
  u_intensity_ = glGetUniformLocation(lit_program_, "u_intensity");
  u_view_pos_ = glGetUniformLocation(lit_program_, "u_view_pos");
  u_albedo_ = glGetUniformLocation(lit_program_, "u_albedo");
  u_shadow_map_ = glGetUniformLocation(lit_program_, "u_shadow_map");
  u_light_matrix_ = glGetUniformLocation(lit_program_, "u_light_matrix");

  return lit_program_ != 0 && depth_program_ != 0;
}

bool Renderer::create_albedo_texture() {
  // Procedural checkerboard — no external assets required
  const int N = 256;
  std::vector<unsigned char> pixels(N * N * 4);
  for (int y = 0; y < N; ++y) {
    for (int x = 0; x < N; ++x) {
      bool c = ((x / 32) ^ (y / 32)) & 1;
      unsigned char v = c ? 220 : 160;
      int i = (y * N + x) * 4;
      pixels[i + 0] = v;
      pixels[i + 1] = v;
      pixels[i + 2] = v;
      pixels[i + 3] = 255;
    }
  }

  glGenTextures(1, &albedo_tex_);
  glBindTexture(GL_TEXTURE_2D, albedo_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

bool Renderer::create_shadow_map() {
  glGenTextures(1, &shadow_tex_);
  glBindTexture(GL_TEXTURE_2D, shadow_tex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
               kShadowSize, kShadowSize, 0,
               GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // Compare mode not required; we sample depth manually in the shader

  glGenFramebuffers(1, &shadow_fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_tex_, 0);
#if defined(USE_GLES) || defined(__ANDROID__)
  // No color buffer on ES — explicit draw/read buffers
  glDrawBuffers(0, nullptr);
#else
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
#endif
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "Shadow FBO incomplete: 0x%x\n", status);
    return false;
  }
  return true;
}

bool Renderer::init(int width, int height) {
  width_ = width;
  height_ = height;

  if (!load_programs()) return false;
  if (!create_albedo_texture()) return false;
  if (!create_shadow_map()) return false;

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  // pos
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, px)));
  // normal
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, nx)));
  // uv
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, u)));
  // color
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, r)));
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glClearColor(0.45f, 0.65f, 0.95f, 1.f);
  return true;
}

void Renderer::resize(int width, int height) {
  width_ = width;
  height_ = height;
}

void Renderer::begin_shadow_pass(const Light& light, const Vec3& focus) {
  // Orthographic volume around the scene focus, looking along the light
  Vec3 dir = light.direction.normalized();
  Vec3 eye = focus - dir * 25.f;
  Mat4 light_view = Mat4::look_at(eye, focus, {0.f, 1.f, 0.f});
  float e = 18.f;
  Mat4 light_proj = Mat4::ortho(-e, e, -e, e, 1.f, 60.f);
  light_view_proj_ = light_proj * light_view;

  // Bias matrix: clip -> [0,1]
  Mat4 bias{};
  bias.m[0] = 0.5f; bias.m[5] = 0.5f; bias.m[10] = 0.5f;
  bias.m[12] = 0.5f; bias.m[13] = 0.5f; bias.m[14] = 0.5f;
  light_matrix_ = bias * light_view_proj_;

  glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
  glViewport(0, 0, kShadowSize, kShadowSize);
  glClear(GL_DEPTH_BUFFER_BIT);
  // Front-face culling reduces shadow acne on solid meshes
  glCullFace(GL_FRONT);
  glUseProgram(depth_program_);
}

void Renderer::draw_shadow(const std::vector<Vertex>& vertices, const Mat4& model) {
  if (vertices.empty()) return;
  Mat4 mvp = light_view_proj_ * model;
  glUniformMatrix4fv(u_depth_mvp_, 1, GL_FALSE, mvp.m);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
}

void Renderer::end_shadow_pass() {
  glCullFace(GL_BACK);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::begin_frame(const Light& light, const Vec3& view_pos) {
  view_pos_ = view_pos;
  glViewport(0, 0, width_, height_);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(lit_program_);

  Vec3 ld = light.direction.normalized();
  glUniform3f(u_light_dir_, ld.x, ld.y, ld.z);
  glUniform3f(u_light_color_, light.color.x, light.color.y, light.color.z);
  glUniform1f(u_ambient_, light.ambient);
  glUniform1f(u_intensity_, light.intensity);
  glUniformMatrix4fv(u_light_matrix_, 1, GL_FALSE, light_matrix_.m);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, albedo_tex_);
  glUniform1i(u_albedo_, 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, shadow_tex_);
  glUniform1i(u_shadow_map_, 1);
}

void Renderer::draw(const std::vector<Vertex>& vertices, const Mat4& model,
                    const Mat4& view, const Mat4& proj) {
  if (vertices.empty()) return;

  // Normal matrix: for uniform scale this is just the upper 3x3 of model.
  // We pass it as a mat4 with the 3x3 in the upper-left.
  Mat4 normal_mat = model;
  normal_mat.m[12] = normal_mat.m[13] = normal_mat.m[14] = 0.f;
  normal_mat.m[3] = normal_mat.m[7] = normal_mat.m[11] = 0.f;
  normal_mat.m[15] = 1.f;

  glUniformMatrix4fv(u_model_, 1, GL_FALSE, model.m);
  glUniformMatrix4fv(u_view_, 1, GL_FALSE, view.m);
  glUniformMatrix4fv(u_proj_, 1, GL_FALSE, proj.m);
  glUniformMatrix4fv(u_normal_mat_, 1, GL_FALSE, normal_mat.m);
  glUniform3f(u_view_pos_, view_pos_.x, view_pos_.y, view_pos_.z);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
}

void Renderer::end_frame() {
  // swap is done by the caller
}

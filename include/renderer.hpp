// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "math.hpp"
#include <cstdint>
#include <vector>

struct Vertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
  float r, g, b; // albedo tint
};

struct Light {
  Vec3 direction{-0.4f, -1.0f, -0.3f}; // points toward scene
  Vec3 color{1.0f, 0.96f, 0.90f};
  float ambient = 0.22f;
  float intensity = 1.0f;
};

class Renderer {
public:
  Renderer();
  ~Renderer();

  bool init(int width, int height);
  void resize(int width, int height);

  // Call once per frame before drawing the scene into the shadow map
  void begin_shadow_pass(const Light& light, const Vec3& focus);
  void draw_shadow(const std::vector<Vertex>& vertices, const Mat4& model);
  void end_shadow_pass();

  // Main lit + textured + shadowed pass
  void begin_frame(const Light& light, const Vec3& view_pos);
  void draw(const std::vector<Vertex>& vertices, const Mat4& model,
            const Mat4& view, const Mat4& proj);
  void end_frame();

  int width() const { return width_; }
  int height() const { return height_; }

  // Bias matrix * light_view_proj from last shadow pass (for debugging/external use)
  const Mat4& light_matrix() const { return light_matrix_; }

private:
  static constexpr int kShadowSize = 2048;

  unsigned int lit_program_ = 0;
  unsigned int depth_program_ = 0;
  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  unsigned int albedo_tex_ = 0;
  unsigned int shadow_fbo_ = 0;
  unsigned int shadow_tex_ = 0;

  int width_ = 0;
  int height_ = 0;

  // Uniform locations — lit program
  int u_model_ = -1, u_view_ = -1, u_proj_ = -1;
  int u_normal_mat_ = -1;
  int u_light_dir_ = -1, u_light_color_ = -1, u_ambient_ = -1, u_intensity_ = -1;
  int u_view_pos_ = -1;
  int u_albedo_ = -1, u_shadow_map_ = -1, u_light_matrix_ = -1;

  // Depth program
  int u_depth_mvp_ = -1;

  Mat4 light_matrix_{};
  Mat4 light_view_proj_{};
  Vec3 view_pos_{};

  bool load_programs();
  bool create_shadow_map();
  bool create_albedo_texture();
  static unsigned int compile_shader(unsigned int type, const char* src);
  static unsigned int link_program(unsigned int vs, unsigned int fs);
};

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "math.hpp"
#include "renderer.hpp"
#include <vector>

class Scene {
public:
  Scene();

  void update(float dt);
  // Draw into the shadow map (depth only)
  void draw_shadow(Renderer& r);
  // Draw lit + textured + shadowed
  void draw(Renderer& r, const Mat4& view, const Mat4& proj);

  Vec3 cam_pos{0.f, 1.6f, 6.f};
  float cam_yaw = 0.f;
  float cam_pitch = -0.15f;

  Mat4 view_matrix() const;
  Light light;

private:
  std::vector<Vertex> house_;
  std::vector<Vertex> floor_;
  std::vector<Vertex> cubes_;
  float time_ = 0.f;

  void build_geometry();
  static void add_cube(std::vector<Vertex>& out, const Vec3& center,
                       const Vec3& half_extents, const Vec3& color,
                       float uv_scale = 1.f);
  static void add_quad(std::vector<Vertex>& out,
                       const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                       const Vec3& normal, const Vec3& color, float uv_scale);
};

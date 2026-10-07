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
  void draw(Renderer& r, const Mat4& view_proj);

  // Simple first-person camera state (mutated by main)
  Vec3 cam_pos{0.f, 1.6f, 5.f};
  float cam_yaw = 0.f;   // radians, 0 looks -Z
  float cam_pitch = 0.f;

  Mat4 view_matrix() const;

private:
  std::vector<Vertex> house_;
  std::vector<Vertex> floor_;
  std::vector<Vertex> cubes_;
  float time_ = 0.f;

  void build_geometry();
  static void add_cube(std::vector<Vertex>& out, const Vec3& center,
                       const Vec3& half_extents, const Vec3& color);
  static void add_quad(std::vector<Vertex>& out,
                       const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                       const Vec3& color);
};

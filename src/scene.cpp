// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "scene.hpp"
#include <cmath>

void Scene::add_quad(std::vector<Vertex>& out,
                     const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     const Vec3& normal, const Vec3& color, float uv_scale) {
  // Explicit normal (outward). Vertices MUST be CCW when viewed from outside
  // (i.e. looking along -normal).
  const Vec3 n = normal.normalized();
  auto push = [&](const Vec3& p, float u, float v) {
    out.push_back({p.x, p.y, p.z,
                   n.x, n.y, n.z,
                   u * uv_scale, v * uv_scale,
                   color.x, color.y, color.z});
  };
  push(a, 0.f, 0.f);
  push(b, 1.f, 0.f);
  push(c, 1.f, 1.f);
  push(a, 0.f, 0.f);
  push(c, 1.f, 1.f);
  push(d, 0.f, 1.f);
}

void Scene::add_cube(std::vector<Vertex>& out, const Vec3& center,
                     const Vec3& half, const Vec3& color, float uv_scale) {
  const float x = half.x, y = half.y, z = half.z;
  const float cx = center.x, cy = center.y, cz = center.z;

  // 8 corners
  const Vec3 p000{cx - x, cy - y, cz - z};
  const Vec3 p001{cx - x, cy - y, cz + z};
  const Vec3 p010{cx - x, cy + y, cz - z};
  const Vec3 p011{cx - x, cy + y, cz + z};
  const Vec3 p100{cx + x, cy - y, cz - z};
  const Vec3 p101{cx + x, cy - y, cz + z};
  const Vec3 p110{cx + x, cy + y, cz - z};
  const Vec3 p111{cx + x, cy + y, cz + z};

  // Each face: CCW when viewed from outside; normal is explicit outward.
  // Orders verified so cross(b-a, c-a) points along the given normal.
  // -X
  add_quad(out, p001, p011, p010, p000, {-1, 0, 0}, color, uv_scale);
  // +X
  add_quad(out, p100, p110, p111, p101, { 1, 0, 0}, color, uv_scale);
  // -Y
  add_quad(out, p000, p100, p101, p001, { 0,-1, 0}, color, uv_scale);
  // +Y
  add_quad(out, p010, p011, p111, p110, { 0, 1, 0}, color, uv_scale);
  // -Z
  add_quad(out, p000, p010, p110, p100, { 0, 0,-1}, color, uv_scale);
  // +Z
  add_quad(out, p001, p101, p111, p011, { 0, 0, 1}, color, uv_scale);
}

Scene::Scene() {
  light.direction = {-0.55f, -1.0f, -0.35f};
  light.color = {1.0f, 0.96f, 0.88f};
  light.ambient = 0.28f;
  light.intensity = 1.15f;
  build_geometry();
}

void Scene::build_geometry() {
  {
    float s = 20.f;
    Vec3 green{0.35f, 0.55f, 0.30f};
    // CCW from +Y (outside / above)
    add_quad(floor_,
             {-s, 0.f, -s}, {-s, 0.f,  s}, { s, 0.f,  s}, { s, 0.f, -s},
             {0.f, 1.f, 0.f}, green, 8.f);
  }

  {
    Vec3 wall{0.85f, 0.60f, 0.45f};
    Vec3 roof{0.55f, 0.22f, 0.15f};
    Vec3 door{0.30f, 0.20f, 0.14f};
    add_cube(house_, {0.f, 1.5f, -3.f}, {2.f, 1.5f, 2.f}, wall, 2.f);
    add_cube(house_, {0.f, 3.4f, -3.f}, {2.3f, 0.4f, 2.3f}, roof, 1.5f);
    add_cube(house_, {0.f, 0.9f, -0.95f}, {0.5f, 0.9f, 0.08f}, door, 1.f);
  }

  {
    add_cube(cubes_, {-3.f, 0.5f, 1.f}, {0.4f, 0.4f, 0.4f}, {0.95f, 0.35f, 0.25f}, 1.f);
    add_cube(cubes_, { 3.f, 0.7f, 0.f}, {0.5f, 0.5f, 0.5f}, {0.25f, 0.50f, 0.95f}, 1.f);
    add_cube(cubes_, {-1.5f, 0.4f, 2.5f}, {0.3f, 0.3f, 0.3f}, {0.95f, 0.85f, 0.25f}, 1.f);
    add_cube(cubes_, { 2.f, 1.2f, -6.f}, {0.6f, 0.6f, 0.6f}, {0.65f, 0.25f, 0.85f}, 1.f);
  }
}

void Scene::update(float dt) {
  time_ += dt;
}

Mat4 Scene::view_matrix() const {
  float cy = std::cos(cam_yaw), sy = std::sin(cam_yaw);
  float cp = std::cos(cam_pitch), sp = std::sin(cam_pitch);
  Vec3 forward{sy * cp, -sp, -cy * cp};
  return Mat4::look_at(cam_pos, cam_pos + forward, {0.f, 1.f, 0.f});
}

void Scene::draw_shadow(Renderer& r) {
  Mat4 id = Mat4::identity();
  r.draw_shadow(floor_, id);
  r.draw_shadow(house_, id);
  Mat4 rot = Mat4::rotate_y(time_ * 0.6f);
  Mat4 model = Mat4::translate({0.f, 0.15f * std::sin(time_ * 1.2f), 0.f}) * rot;
  r.draw_shadow(cubes_, model);
}

void Scene::draw(Renderer& r, const Mat4& view, const Mat4& proj) {
  Mat4 id = Mat4::identity();
  r.draw(floor_, id, view, proj);
  r.draw(house_, id, view, proj);
  Mat4 rot = Mat4::rotate_y(time_ * 0.6f);
  Mat4 model = Mat4::translate({0.f, 0.15f * std::sin(time_ * 1.2f), 0.f}) * rot;
  r.draw(cubes_, model, view, proj);
}

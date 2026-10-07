// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "scene.hpp"
#include <cmath>

void Scene::add_quad(std::vector<Vertex>& out,
                     const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     const Vec3& color) {
  // two triangles: a-b-c and a-c-d
  auto push = [&](const Vec3& p) {
    out.push_back({p.x, p.y, p.z, color.x, color.y, color.z});
  };
  push(a); push(b); push(c);
  push(a); push(c); push(d);
}

void Scene::add_cube(std::vector<Vertex>& out, const Vec3& center,
                     const Vec3& half, const Vec3& color) {
  // 6 faces, each a quad. Order: outward facing, CCW when viewed from outside.
  Vec3 p[8] = {
    {center.x - half.x, center.y - half.y, center.z - half.z},
    {center.x + half.x, center.y - half.y, center.z - half.z},
    {center.x + half.x, center.y + half.y, center.z - half.z},
    {center.x - half.x, center.y + half.y, center.z - half.z},
    {center.x - half.x, center.y - half.y, center.z + half.z},
    {center.x + half.x, center.y - half.y, center.z + half.z},
    {center.x + half.x, center.y + half.y, center.z + half.z},
    {center.x - half.x, center.y + half.y, center.z + half.z},
  };
  // -Z
  add_quad(out, p[0], p[3], p[2], p[1], color);
  // +Z
  add_quad(out, p[4], p[5], p[6], p[7], color);
  // -X
  add_quad(out, p[0], p[4], p[7], p[3], color);
  // +X
  add_quad(out, p[1], p[2], p[6], p[5], color);
  // -Y
  add_quad(out, p[0], p[1], p[5], p[4], color);
  // +Y
  add_quad(out, p[3], p[7], p[6], p[2], color);
}

Scene::Scene() {
  build_geometry();
}

void Scene::build_geometry() {
  // Floor (large plane)
  {
    float s = 20.f;
    Vec3 green{0.25f, 0.55f, 0.25f};
    add_quad(floor_,
             {-s, 0.f, -s}, { s, 0.f, -s}, { s, 0.f,  s}, {-s, 0.f,  s},
             green);
  }

  // Simple house: base + roof
  {
    Vec3 wall{0.75f, 0.55f, 0.40f};   // warm brick
    Vec3 roof{0.55f, 0.20f, 0.15f};   // dark red
    // Base 4x3x4
    add_cube(house_, {0.f, 1.5f, -3.f}, {2.f, 1.5f, 2.f}, wall);
    // Roof as a slightly larger, flatter box sitting on top (simple "house")
    add_cube(house_, {0.f, 3.4f, -3.f}, {2.3f, 0.4f, 2.3f}, roof);
    // Door suggestion (darker inset)
    Vec3 door{0.25f, 0.18f, 0.12f};
    add_cube(house_, {0.f, 0.9f, -0.95f}, {0.5f, 0.9f, 0.08f}, door);
  }

  // A few floating / decorative cubes
  {
    add_cube(cubes_, {-3.f, 0.5f, 1.f}, {0.4f, 0.4f, 0.4f}, {0.9f, 0.3f, 0.2f});
    add_cube(cubes_, { 3.f, 0.7f, 0.f}, {0.5f, 0.5f, 0.5f}, {0.2f, 0.5f, 0.9f});
    add_cube(cubes_, {-1.5f, 0.4f, 2.5f}, {0.3f, 0.3f, 0.3f}, {0.95f, 0.85f, 0.2f});
    add_cube(cubes_, { 2.f, 1.2f, -6.f}, {0.6f, 0.6f, 0.6f}, {0.6f, 0.2f, 0.8f});
  }
}

void Scene::update(float dt) {
  time_ += dt;
}

Mat4 Scene::view_matrix() const {
  // Camera looks along -Z by default; yaw/pitch applied
  float cy = std::cos(cam_yaw), sy = std::sin(cam_yaw);
  float cp = std::cos(cam_pitch), sp = std::sin(cam_pitch);
  Vec3 forward{sy * cp, -sp, -cy * cp};
  Vec3 center = cam_pos + forward;
  return Mat4::look_at(cam_pos, center, {0.f, 1.f, 0.f});
}

void Scene::draw(Renderer& r, const Mat4& view_proj) {
  // Static geometry
  r.draw(floor_, view_proj);
  r.draw(house_, view_proj);

  // Animated cubes (orbit a bit)
  Mat4 rot = Mat4::rotate_y(time_ * 0.7f);
  Mat4 model = Mat4::translate({0.f, 0.5f + 0.3f * std::sin(time_), 0.f}) * rot;
  // For simplicity we just draw the pre-built cubes with a shared spin offset
  // (full per-cube transform would require uploading model matrices; this is fine for hello)
  r.draw(cubes_, view_proj);
  (void)model; // silence unused for now; visual spin can be added later
}

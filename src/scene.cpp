// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "scene.hpp"
#include <cmath>

void Scene::add_quad(std::vector<Vertex>& out,
                     const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     const Vec3& /*normal_hint*/, const Vec3& color, float uv_scale) {
  // Derive a single flat normal from the first triangle so lighting always
  // matches the winding (CCW → normal toward the viewer of that face).
  Vec3 e1 = b - a;
  Vec3 e2 = c - a;
  Vec3 normal = cross(e1, e2).normalized();

  auto push = [&](const Vec3& p, float u, float v) {
    out.push_back({p.x, p.y, p.z,
                   normal.x, normal.y, normal.z,
                   u * uv_scale, v * uv_scale,
                   color.x, color.y, color.z});
  };
  // a-b-c and a-c-d
  push(a, 0.f, 0.f);
  push(b, 1.f, 0.f);
  push(c, 1.f, 1.f);
  push(a, 0.f, 0.f);
  push(c, 1.f, 1.f);
  push(d, 0.f, 1.f);
}

void Scene::add_cube(std::vector<Vertex>& out, const Vec3& center,
                     const Vec3& half, const Vec3& color, float uv_scale) {
  // Corner indices:
  //   0 (-x,-y,-z)  1 (+x,-y,-z)  2 (+x,+y,-z)  3 (-x,+y,-z)
  //   4 (-x,-y,+z)  5 (+x,-y,+z)  6 (+x,+y,+z)  7 (-x,+y,+z)
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

  // Each face listed CCW when viewed from *outside* the cube.
  // Normal is computed from winding in add_quad (hint ignored).
  const Vec3 dummy{0, 0, 0};

  // -Z (outward normal -Z): view from -Z, x→right y→up → p0,p1,p2,p3
  add_quad(out, p[0], p[1], p[2], p[3], dummy, color, uv_scale);
  // +Z: view from +Z, x→right y→up → p5,p4,p7,p6  (mirror of -Z)
  // From +Z looking -Z: +x is left on screen… use p4,p5,p6,p7 with care.
  // Viewed from outside (+Z side looking toward -Z): world +x is to the viewer's left.
  // Simpler: walk the boundary so cross((b-a),(c-a)) points +Z.
  // a=p4 (-x,-y,+z), b=p5 (+x,-y,+z), c=p6 (+x,+y,+z):
  //   e1=(+2x,0,0), e2=(+2x,+2y,0) wait e2 from a to c = (2x,2y,0)
  //   cross = (0,0, 2x*2y - 0) = (0,0, positive) → +Z. Good: p4,p5,p6,p7
  add_quad(out, p[4], p[5], p[6], p[7], dummy, color, uv_scale);
  // -X: outward -X. a=p0, b=p3, c=p7, d=p4
  // e1 = p3-p0 = (0,2y,0), e2 = p7-p0 = (0,2y,2z) → cross = (2y*2z, 0, 0) wait
  // cross((0,2y,0),(0,2y,2z)) = (4y*z - 0, 0-0, 0-0) = (4yz, 0, 0) — sign depends.
  // Use: p0, p4, p7, p3 — e1=p4-p0=(0,0,2z), e2=p7-p0=(0,2y,2z)
  // cross = (0*2z - 2z*2y, 2z*0 - 0*0, 0*2y - 0*0) = (-4yz, 0, 0) → -X if y,z>0
  add_quad(out, p[0], p[4], p[7], p[3], dummy, color, uv_scale);
  // +X: p1, p2, p6, p5
  // e1=p2-p1=(0,2y,0), e2=p6-p1=(0,2y,2z) → cross=(4yz,0,0) → +X
  add_quad(out, p[1], p[2], p[6], p[5], dummy, color, uv_scale);
  // -Y: p0, p1, p5, p4
  // e1=p1-p0=(2x,0,0), e2=p5-p0=(2x,0,2z) → cross=(0, -4xz, 0) → -Y
  add_quad(out, p[0], p[1], p[5], p[4], dummy, color, uv_scale);
  // +Y: p3, p7, p6, p2
  // e1=p7-p3=(0,0,2z), e2=p6-p3=(2x,0,2z) → cross=(2z*0-0*2x, 2z*2x-0*0, 0-0)=(0,4zx,0) → +Y
  add_quad(out, p[3], p[7], p[6], p[2], dummy, color, uv_scale);
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
    // CCW from +Y: (-s,-s) → (-s,+s) → (+s,+s) → (+s,-s) in XZ
    // e1=(0,0,2s), e2=(2s,0,2s) → cross = (0*2s - 2s*2s, 2s*2s - 0*0, 0-0) = (-4s², 4s², 0)
    // Wait that's wrong. a=(-s,0,-s), b=(-s,0,s), c=(s,0,s)
    // e1 = (0,0,2s), e2 = (2s,0,2s)
    // cross = (0*2s - 2s*0, 2s*2s - 0*0, 0*0 - 0*2s) = (0, 4s², 0) → +Y. Good.
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

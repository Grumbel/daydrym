// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cmath>
#include <cstring>

struct Vec2 {
  float x = 0.f, y = 0.f;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
  float x = 0.f, y = 0.f, z = 0.f;

  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

  float length() const { return std::sqrt(x * x + y * y + z * z); }
  Vec3 normalized() const {
    float l = length();
    if (l < 1e-8f) return {};
    return *this * (1.f / l);
  }
};

inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Column-major 4x4 matrix (OpenGL style)
struct Mat4 {
  float m[16] = {
    1,0,0,0,
    0,1,0,0,
    0,0,1,0,
    0,0,0,1
  };

  static Mat4 identity() { return {}; }

  static Mat4 perspective(float fovy_rad, float aspect, float znear, float zfar) {
    Mat4 r{};
    float f = 1.f / std::tan(fovy_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.f;
    r.m[14] = (2.f * zfar * znear) / (znear - zfar);
    r.m[15] = 0.f;
    return r;
  }

  static Mat4 ortho(float left, float right, float bottom, float top,
                    float znear, float zfar) {
    Mat4 r{};
    r.m[0]  =  2.f / (right - left);
    r.m[5]  =  2.f / (top - bottom);
    r.m[10] = -2.f / (zfar - znear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zfar + znear) / (zfar - znear);
    r.m[15] = 1.f;
    return r;
  }

  static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = (center - eye).normalized();
    Vec3 s = cross(f, up).normalized();
    Vec3 u = cross(s, f);
    Mat4 r{};
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] =  dot(f, eye);
    return r;
  }

  static Mat4 translate(const Vec3& t) {
    Mat4 r{};
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }

  static Mat4 scale(const Vec3& s) {
    Mat4 r{};
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
  }

  static Mat4 rotate_y(float rad) {
    Mat4 r{};
    float c = std::cos(rad), s = std::sin(rad);
    r.m[0] = c;  r.m[8] = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
  }

  Mat4 operator*(const Mat4& o) const {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        r.m[col * 4 + row] =
          m[0 * 4 + row] * o.m[col * 4 + 0] +
          m[1 * 4 + row] * o.m[col * 4 + 1] +
          m[2 * 4 + row] * o.m[col * 4 + 2] +
          m[3 * 4 + row] * o.m[col * 4 + 3];
      }
    }
    return r;
  }

  Vec3 transform_dir(const Vec3& v) const {
    return {
      m[0]*v.x + m[4]*v.y + m[8]*v.z,
      m[1]*v.x + m[5]*v.y + m[9]*v.z,
      m[2]*v.x + m[6]*v.y + m[10]*v.z
    };
  }
};

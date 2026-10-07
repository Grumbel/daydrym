// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "math.hpp"
#include <cstdint>
#include <vector>

struct Vertex {
  float px, py, pz;
  float r, g, b;
};

class Renderer {
public:
  Renderer();
  ~Renderer();

  bool init(int width, int height);
  void resize(int width, int height);
  void begin_frame();
  void end_frame();

  // Draw a list of triangles (3 vertices each)
  void draw(const std::vector<Vertex>& vertices, const Mat4& mvp);

  int width() const { return width_; }
  int height() const { return height_; }

private:
  unsigned int program_ = 0;
  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  int width_ = 0;
  int height_ = 0;
  int u_mvp_ = -1;

  bool load_shaders();
};

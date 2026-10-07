// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "math.hpp"
#include <cstdint>

// Thin wrapper around the open-source Google Cardboard NDK SDK.
// On non-Android / builds without the SDK, init() fails and callers fall back.

class CardboardVr {
public:
  CardboardVr() = default;
  ~CardboardVr();

  CardboardVr(const CardboardVr&) = delete;
  CardboardVr& operator=(const CardboardVr&) = delete;

  // Call once after the GL context exists. Returns false if unavailable.
  bool init(int screen_width, int screen_height);

  void pause();
  void resume();

  // Refresh lens params after resize / QR scan
  void set_screen_size(int screen_width, int screen_height);

  // Predicted head pose → view matrix (world-from-head inverted for camera)
  Mat4 head_view(int64_t timestamp_ns) const;

  // Per-eye matrices from CardboardLensDistortion (column-major)
  Mat4 eye_from_head(int eye /*0=left,1=right*/) const;
  Mat4 eye_projection(int eye, float z_near, float z_far) const;

  bool ok() const { return ok_; }
  int width() const { return width_; }
  int height() const { return height_; }

private:
  bool ok_ = false;
  int width_ = 0;
  int height_ = 0;
  void* head_tracker_ = nullptr;   // CardboardHeadTracker*
  void* lens_distortion_ = nullptr; // CardboardLensDistortion*
  void destroy_lens();
  void create_lens();
};

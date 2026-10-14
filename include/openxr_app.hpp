// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Optional Linux OpenXR path (desktop GL + XR_KHR_opengl_enable / Xlib).
// Not used on Android / Daydream.

#include "math.hpp"
#include "renderer.hpp"
#include "scene.hpp"

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

// Run the OpenXR frame loop. Returns process exit code (0 on clean exit).
// Requires an SDL video subsystem already initialized and a GL-capable window
// whose context is current (X11/GLX). The window is used for event pumping
// and as the GLX drawable for the graphics binding.
int run_openxr(SDL_Window* window, SDL_GLContext gl_ctx);

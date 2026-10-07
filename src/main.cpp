// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "renderer.hpp"
#include "scene.hpp"
#include "math.hpp"

#include <SDL.h>
#include <cstdio>
#include <cmath>

#if defined(USE_GLES) || defined(__ANDROID__)
#  include <GLES3/gl3.h>
#else
#  include <GL/gl.h>
#endif

static bool g_running = true;
static bool g_stereo = false; // side-by-side for phone VR testing

static void handle_event(const SDL_Event& e, Scene& scene, float& mouse_sens) {
  if (e.type == SDL_QUIT) {
    g_running = false;
  } else if (e.type == SDL_KEYDOWN) {
    switch (e.key.keysym.sym) {
      case SDLK_ESCAPE: g_running = false; break;
      case SDLK_v: g_stereo = !g_stereo; break;
      default: break;
    }
  } else if (e.type == SDL_MOUSEMOTION && (SDL_GetRelativeMouseMode() == SDL_TRUE)) {
    scene.cam_yaw   += e.motion.xrel * mouse_sens;
    scene.cam_pitch -= e.motion.yrel * mouse_sens;
    // clamp pitch
    const float lim = 1.4f;
    if (scene.cam_pitch >  lim) scene.cam_pitch = lim;
    if (scene.cam_pitch < -lim) scene.cam_pitch = -lim;
  } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
    // handled in main loop via SDL_GL_GetDrawableSize
  }
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

#if defined(USE_GLES) || defined(__ANDROID__)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  int win_w = 1280, win_h = 720;
  SDL_Window* window = SDL_CreateWindow(
      "GLES VR Hello — house & cubes (V = toggle stereo)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      win_w, win_h,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  if (!ctx) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetSwapInterval(1); // vsync

  // Relative mouse for look
  SDL_SetRelativeMouseMode(SDL_TRUE);

  int draw_w = 0, draw_h = 0;
  SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);

  Renderer renderer;
  if (!renderer.init(draw_w, draw_h)) {
    std::fprintf(stderr, "Renderer init failed\n");
    return 1;
  }

  Scene scene;
  float mouse_sens = 0.0025f;
  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

  std::printf("Controls: WASD move, mouse look, V = stereo side-by-side, Esc = quit\n");
  std::printf("Scene: stand in front of a simple house with colored cubes.\n");

  while (g_running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      handle_event(e, scene, mouse_sens);
    }

    // Movement
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float speed = 3.5f;
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>((now - prev) / freq);
    prev = now;
    if (dt > 0.05f) dt = 0.05f; // clamp

    float cy = std::cos(scene.cam_yaw), sy = std::sin(scene.cam_yaw);
    Vec3 forward{sy, 0.f, -cy};
    Vec3 right{cy, 0.f, sy};
    if (keys[SDL_SCANCODE_W]) scene.cam_pos += forward * (speed * dt);
    if (keys[SDL_SCANCODE_S]) scene.cam_pos -= forward * (speed * dt);
    if (keys[SDL_SCANCODE_A]) scene.cam_pos -= right * (speed * dt);
    if (keys[SDL_SCANCODE_D]) scene.cam_pos += right * (speed * dt);
    if (keys[SDL_SCANCODE_SPACE]) scene.cam_pos.y += speed * dt;
    if (keys[SDL_SCANCODE_LCTRL]) scene.cam_pos.y -= speed * dt;

    scene.update(dt);

    SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
    if (draw_w != renderer.width() || draw_h != renderer.height()) {
      renderer.resize(draw_w, draw_h);
    }

    renderer.begin_frame();

    Mat4 view = scene.view_matrix();
    float aspect = static_cast<float>(draw_w) / static_cast<float>(draw_h > 0 ? draw_h : 1);

    if (g_stereo) {
      // Simple side-by-side stereo (no lens distortion — good enough for hello / Cardboard test)
      float eye_sep = 0.065f;
      int half_w = draw_w / 2;

      // Left eye
      glViewport(0, 0, half_w, draw_h);
      Mat4 proj_l = Mat4::perspective(1.0f, aspect * 0.5f, 0.1f, 100.f);
      Mat4 eye_off_l = Mat4::translate({ eye_sep * 0.5f, 0.f, 0.f});
      scene.draw(renderer, proj_l * eye_off_l * view);

      // Right eye
      glViewport(half_w, 0, half_w, draw_h);
      Mat4 proj_r = Mat4::perspective(1.0f, aspect * 0.5f, 0.1f, 100.f);
      Mat4 eye_off_r = Mat4::translate({-eye_sep * 0.5f, 0.f, 0.f});
      scene.draw(renderer, proj_r * eye_off_r * view);
    } else {
      glViewport(0, 0, draw_w, draw_h);
      Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.f);
      scene.draw(renderer, proj * view);
    }

    renderer.end_frame();
    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

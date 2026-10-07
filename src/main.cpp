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
#  define GL_GLEXT_PROTOTYPES 1
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

enum class StereoMode {
  Mono = 0,
  SideBySide,
  SideBySideSwapped,
  AnaglyphRedCyan,
  Count
};

static const char* stereo_mode_name(StereoMode m) {
  switch (m) {
    case StereoMode::Mono:              return "mono";
    case StereoMode::SideBySide:        return "side-by-side";
    case StereoMode::SideBySideSwapped: return "side-by-side (L/R swapped)";
    case StereoMode::AnaglyphRedCyan:   return "anaglyph red/cyan";
    default:                            return "?";
  }
}

static bool g_running = true;
static StereoMode g_stereo = StereoMode::Mono;

static void handle_event(const SDL_Event& e, Scene& scene) {
  if (e.type == SDL_QUIT) {
    g_running = false;
  } else if (e.type == SDL_KEYDOWN) {
    switch (e.key.keysym.sym) {
      case SDLK_ESCAPE:
        g_running = false;
        break;
      case SDLK_v: {
        int next = (static_cast<int>(g_stereo) + 1) % static_cast<int>(StereoMode::Count);
        g_stereo = static_cast<StereoMode>(next);
        std::printf("Stereo mode: %s\n", stereo_mode_name(g_stereo));
        break;
      }
      default:
        break;
    }
  } else if (e.type == SDL_MOUSEMOTION && (SDL_GetRelativeMouseMode() == SDL_TRUE)) {
    const float sens = 0.0025f;
    scene.cam_yaw   += e.motion.xrel * sens;
    scene.cam_pitch -= e.motion.yrel * sens;
    const float lim = 1.4f;
    if (scene.cam_pitch >  lim) scene.cam_pitch = lim;
    if (scene.cam_pitch < -lim) scene.cam_pitch = -lim;
  }
}

static Mat4 eye_translate(float sep_sign, float eye_sep) {
  // Positive sep_sign shifts camera toward +X (right); for left eye use +0.5 * sep
  // Convention: left eye is at -eye_sep/2 in camera space after view, approximated
  // by translating world opposite before view: +offset for left when looking -Z...
  // We apply a simple horizontal offset in view space via a pre-view translation.
  return Mat4::translate({sep_sign * eye_sep * 0.5f, 0.f, 0.f});
}

static void render_eye(Renderer& renderer, Scene& scene,
                       const Mat4& view, const Mat4& proj) {
  scene.draw(renderer, view, proj);
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
      "daydrym — V cycles stereo modes",
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
  SDL_GL_SetSwapInterval(1);
  SDL_SetRelativeMouseMode(SDL_TRUE);

  int draw_w = 0, draw_h = 0;
  SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);

  Renderer renderer;
  if (!renderer.init(draw_w, draw_h)) {
    std::fprintf(stderr, "Renderer init failed\n");
    return 1;
  }

  Scene scene;
  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  const float eye_sep = 0.065f;

  std::printf("daydrym — Blinn-Phong, shadow map, textures\n");
  std::printf("Controls: WASD move, mouse look, Space/Ctrl up/down\n");
  std::printf("          V = cycle stereo mode, Esc = quit\n");
  std::printf("Stereo mode: %s\n", stereo_mode_name(g_stereo));

  while (g_running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      handle_event(e, scene);
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float speed = 3.5f;
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>((now - prev) / freq);
    prev = now;
    if (dt > 0.05f) dt = 0.05f;

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

    // Shadow pass (shared; mono camera is good enough for the light)
    renderer.begin_shadow_pass(scene.light, {0.f, 1.f, -2.f});
    scene.draw_shadow(renderer);
    renderer.end_shadow_pass();

    Mat4 view = scene.view_matrix();
    float aspect = static_cast<float>(draw_w) / static_cast<float>(draw_h > 0 ? draw_h : 1);

    // Left eye = camera shifted right in world before view (sep +), right eye opposite
    Mat4 view_left  = eye_translate(+1.f, eye_sep) * view;
    Mat4 view_right = eye_translate(-1.f, eye_sep) * view;

    renderer.begin_frame(scene.light, scene.cam_pos);

    switch (g_stereo) {
      case StereoMode::Mono: {
        glViewport(0, 0, draw_w, draw_h);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.f);
        render_eye(renderer, scene, view, proj);
        break;
      }
      case StereoMode::SideBySide:
      case StereoMode::SideBySideSwapped: {
        const bool swap = (g_stereo == StereoMode::SideBySideSwapped);
        int half_w = draw_w / 2;
        Mat4 proj = Mat4::perspective(1.0f, aspect * 0.5f, 0.1f, 100.f);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // Left half of the window
        glViewport(0, 0, half_w, draw_h);
        render_eye(renderer, scene, swap ? view_right : view_left, proj);

        // Right half
        glViewport(half_w, 0, half_w, draw_h);
        render_eye(renderer, scene, swap ? view_left : view_right, proj);
        break;
      }
      case StereoMode::AnaglyphRedCyan: {
        // Full-frame anaglyph: left → red, right → cyan (G+B)
        Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.f);
        glViewport(0, 0, draw_w, draw_h);

        // Left eye into red channel
        glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
        glDepthFunc(GL_LESS);
        render_eye(renderer, scene, view_left, proj);

        // Right eye into green+blue; allow equal depth so both eyes composite
        glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        render_eye(renderer, scene, view_right, proj);

        // Restore defaults for next frame / UI
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_LESS);
        break;
      }
      default:
        break;
    }

    renderer.end_frame();
    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "renderer.hpp"
#include "scene.hpp"
#include "math.hpp"

#include <SDL.h>
#include <cstdio>
#include <cmath>
#define DAYDRYM_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define DAYDRYM_LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#include <new>

#if defined(USE_GLES)
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

// What is actually running: build flavour, SDL, and the GL context we got.
static void print_startup_info(SDL_Window* window, int draw_w, int draw_h) {
  SDL_version compiled, linked;
  SDL_VERSION(&compiled);
  SDL_GetVersion(&linked);

  int major = 0, minor = 0, profile = 0;
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
  const char* profile_name = profile == SDL_GL_CONTEXT_PROFILE_ES ? "ES"
                           : profile == SDL_GL_CONTEXT_PROFILE_CORE ? "core"
                           : profile == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY ? "compatibility"
                           : "unknown";
  int win_w = 0, win_h = 0;
  SDL_GetWindowSize(window, &win_w, &win_h);

  auto gl = [](GLenum name) {
    const GLubyte* str = glGetString(name);
    return str ? reinterpret_cast<const char*>(str) : "(null)";
  };

  DAYDRYM_LOGI("daydrym %s build (%s shaders)",
#if defined(USE_GLES)
               "GLES", "ES 3.00"
#else
               "desktop GL", "GLSL 3.30 core"
#endif
  );
  DAYDRYM_LOGI("SDL:      %d.%d.%d (compiled %d.%d.%d), video driver %s",
               linked.major, linked.minor, linked.patch,
               compiled.major, compiled.minor, compiled.patch,
               SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
  DAYDRYM_LOGI("Context:  OpenGL %s %d.%d, vsync %d", profile_name, major, minor,
               SDL_GL_GetSwapInterval());
  DAYDRYM_LOGI("GL_VENDOR:   %s", gl(GL_VENDOR));
  DAYDRYM_LOGI("GL_RENDERER: %s", gl(GL_RENDERER));
  DAYDRYM_LOGI("GL_VERSION:  %s", gl(GL_VERSION));
  DAYDRYM_LOGI("GLSL:        %s", gl(GL_SHADING_LANGUAGE_VERSION));
  DAYDRYM_LOGI("Window:   %dx%d, drawable %dx%d", win_w, win_h, draw_w, draw_h);
  std::fflush(stdout);
}

static bool g_running = true;
static bool g_paused = false;
static StereoMode g_stereo = StereoMode::Mono;

static void cycle_stereo_mode() {
  int next = (static_cast<int>(g_stereo) + 1) % static_cast<int>(StereoMode::Count);
  g_stereo = static_cast<StereoMode>(next);
  DAYDRYM_LOGI("Stereo mode: %s", stereo_mode_name(g_stereo));
}

static bool mouse_grabbed() {
  return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

static void set_mouse_grab(bool grab) {
  if (mouse_grabbed() == grab) return;
  SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
  DAYDRYM_LOGI(grab ? "Mouse grabbed (Esc releases)"
                    : "Mouse released (click to grab, Q quits)");
  std::fflush(stdout);
}

static void handle_event(const SDL_Event& e, Scene& scene) {
  switch (e.type) {
    case SDL_QUIT:
      g_running = false;
      break;
    case SDL_APP_TERMINATING:
      g_running = false;
      break;

    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
      g_paused = true;
      std::printf("Paused (background)\n");
      break;

    case SDL_APP_WILLENTERFOREGROUND:
      break;

    case SDL_APP_DIDENTERFOREGROUND:
      g_paused = false;
      std::printf("Resumed (foreground), stereo=%s\n", stereo_mode_name(g_stereo));
      break;

    case SDL_RENDER_DEVICE_RESET:
    case SDL_RENDER_TARGETS_RESET:
      std::printf("GL device/targets reset — will recreate on next frame\n");
      break;

    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE:
          set_mouse_grab(false);
          break;
        case SDLK_q:
          g_running = false;
          break;
        case SDLK_v:
          cycle_stereo_mode();
          break;
        default:
          break;
      }
      break;

    case SDL_MOUSEBUTTONDOWN:
      set_mouse_grab(true);  // the click that grabs is not otherwise used
      break;

    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) set_mouse_grab(false);
      break;

    case SDL_MOUSEMOTION:
      if (mouse_grabbed()) {
        const float sens = 0.0025f;
        scene.cam_yaw   += e.motion.xrel * sens;
        scene.cam_pitch -= e.motion.yrel * sens;
        const float lim = 1.4f;
        if (scene.cam_pitch >  lim) scene.cam_pitch = lim;
        if (scene.cam_pitch < -lim) scene.cam_pitch = -lim;
      }
      break;


    default:
      break;
  }
  (void)scene;
}

static Mat4 eye_translate(float sep_sign, float eye_sep) {
  return Mat4::translate({sep_sign * eye_sep * 0.5f, 0.f, 0.f});
}

static void render_eye(Renderer& renderer, Scene& scene,
                       const Mat4& view, const Mat4& proj) {
  scene.draw(renderer, view, proj);
}


int main(int argc, char** argv) {
  (void)argc; (void)argv;

  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_SENSOR) != 0) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
      std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
      return 1;
    }
  }

#if defined(USE_GLES)
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
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
  flags |= SDL_WINDOW_RESIZABLE;

  SDL_Window* window = SDL_CreateWindow(
      "daydrym",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      win_w, win_h, flags);

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
  set_mouse_grab(true);

  int draw_w = 0, draw_h = 0;
  SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);

  print_startup_info(window, draw_w, draw_h);

  Renderer renderer;
  if (!renderer.init(draw_w, draw_h)) {
    std::fprintf(stderr, "Renderer init failed\n");
    return 1;
  }

  Scene scene;
  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  const float eye_sep = 0.065f;

  DAYDRYM_LOGI("daydrym — Blinn-Phong, shadow map, textures");
  DAYDRYM_LOGI("Stereo mode: %s", stereo_mode_name(g_stereo));
  std::fflush(stdout);

  while (g_running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      handle_event(e, scene);
    }

    if (g_paused) {
      SDL_Delay(50);
      prev = SDL_GetPerformanceCounter();
      continue;
    }


    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const bool grabbed = mouse_grabbed();  // released = window input is paused
    float speed = 3.5f;
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>((now - prev) / freq);
    prev = now;
    if (dt > 0.05f) dt = 0.05f;

    float cy = std::cos(scene.cam_yaw), sy = std::sin(scene.cam_yaw);
    Vec3 forward{sy, 0.f, -cy};
    Vec3 right{cy, 0.f, sy};
    if (grabbed && keys[SDL_SCANCODE_W]) scene.cam_pos += forward * (speed * dt);
    if (grabbed && keys[SDL_SCANCODE_S]) scene.cam_pos -= forward * (speed * dt);
    if (grabbed && keys[SDL_SCANCODE_A]) scene.cam_pos -= right * (speed * dt);
    if (grabbed && keys[SDL_SCANCODE_D]) scene.cam_pos += right * (speed * dt);
    if (grabbed && keys[SDL_SCANCODE_SPACE]) scene.cam_pos.y += speed * dt;
    if (grabbed && keys[SDL_SCANCODE_LCTRL]) scene.cam_pos.y -= speed * dt;

    scene.update(dt);

    SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
    if (draw_w <= 0 || draw_h <= 0) {
      SDL_Delay(16);
      continue;
    }
    if (draw_w != renderer.width() || draw_h != renderer.height()) {
      renderer.resize(draw_w, draw_h);
    }

    // Make sure the GL context is current after resume
    if (SDL_GL_MakeCurrent(window, ctx) != 0) {
      std::fprintf(stderr, "SDL_GL_MakeCurrent failed: %s — recreating context\n",
                   SDL_GetError());
      if (ctx) SDL_GL_DeleteContext(ctx);
      ctx = SDL_GL_CreateContext(window);
      if (!ctx) {
        std::fprintf(stderr, "context recreate failed: %s\n", SDL_GetError());
        SDL_Delay(100);
        continue;
      }
      SDL_GL_SetSwapInterval(1);
      // Full GPU resource rebuild
      renderer.~Renderer();
      new (&renderer) Renderer();
      if (!renderer.init(draw_w, draw_h)) {
        std::fprintf(stderr, "renderer re-init failed\n");
        SDL_Delay(100);
        continue;
      }
    }

    renderer.begin_shadow_pass(scene.light, {0.f, 1.f, -2.f});
    scene.draw_shadow(renderer);
    renderer.end_shadow_pass();

    float aspect = static_cast<float>(draw_w) / static_cast<float>(draw_h > 0 ? draw_h : 1);
    Mat4 view = scene.view_matrix();
    Mat4 view_left  = eye_translate(+1.f, eye_sep) * view;
    Mat4 view_right = eye_translate(-1.f, eye_sep) * view;
    Mat4 proj_left  = Mat4::perspective(1.0f, aspect * 0.5f, 0.1f, 100.f);
    Mat4 proj_right = proj_left;


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
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glViewport(0, 0, half_w, draw_h);
        render_eye(renderer, scene, swap ? view_right : view_left,
                   swap ? proj_right : proj_left);
        glViewport(half_w, 0, half_w, draw_h);
        render_eye(renderer, scene, swap ? view_left : view_right,
                   swap ? proj_left : proj_right);
        break;
      }
      case StereoMode::AnaglyphRedCyan: {
        Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.f);
        glViewport(0, 0, draw_w, draw_h);
        glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
        glDepthFunc(GL_LESS);
        render_eye(renderer, scene, view_left, proj);
        glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        render_eye(renderer, scene, view_right, proj);
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

  if (ctx) SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

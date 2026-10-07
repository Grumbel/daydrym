// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "renderer.hpp"
#include "scene.hpp"
#include "math.hpp"
#include "cardboard_vr.hpp"

#include <SDL.h>
#include <cstdio>
#include <cmath>
#if defined(__ANDROID__)
#  include <android/log.h>
#  define DAYDRYM_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "daydrym", __VA_ARGS__)
#  define DAYDRYM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "daydrym", __VA_ARGS__)
#else
#  define DAYDRYM_LOGI(...) std::printf(__VA_ARGS__); std::printf("\n")
#  define DAYDRYM_LOGE(...) std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n")
#endif
#include <new>

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
static bool g_paused = false;
static CardboardVr* g_cardboard = nullptr;
#if defined(__ANDROID__)
// Phone / Daydream View: always start in SBS. Anaglyph is desktop-only.
static StereoMode g_stereo = StereoMode::SideBySide;
#else
static StereoMode g_stereo = StereoMode::Mono;
#endif

static void cycle_stereo_mode() {
#if defined(__ANDROID__)
  // Only cycle modes that work in a headset: SBS <-> SBS swapped.
  // (Tap / controller click used to walk into anaglyph and look "broken".)
  if (g_stereo == StereoMode::SideBySide)
    g_stereo = StereoMode::SideBySideSwapped;
  else
    g_stereo = StereoMode::SideBySide;
#else
  int next = (static_cast<int>(g_stereo) + 1) % static_cast<int>(StereoMode::Count);
  g_stereo = static_cast<StereoMode>(next);
#endif
  DAYDRYM_LOGI("Stereo mode: %s", stereo_mode_name(g_stereo));
}

static void handle_event(const SDL_Event& e, Scene& scene) {
  switch (e.type) {
    case SDL_QUIT:
      // On Android, surfaceDestroyed often synthesizes SDL_QUIT. Treat as pause
      // so the process can resume when the VR compositor gives us a surface again.
#if defined(__ANDROID__)
      g_paused = true;
      if (g_cardboard) g_cardboard->pause();
      std::printf("SDL_QUIT while on Android — pausing instead of exiting\n");
#else
      g_running = false;
#endif
      break;
    case SDL_APP_TERMINATING:
      g_running = false;
      break;

    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
      g_paused = true;
      if (g_cardboard) g_cardboard->pause();
      std::printf("Paused (background)\n");
      break;

    case SDL_APP_WILLENTERFOREGROUND:
      break;

    case SDL_APP_DIDENTERFOREGROUND:
      g_paused = false;
#if defined(__ANDROID__)
      g_stereo = StereoMode::SideBySide;
      if (g_cardboard) g_cardboard->resume();
#endif
      std::printf("Resumed (foreground), stereo=%s\n", stereo_mode_name(g_stereo));
      break;

    case SDL_RENDER_DEVICE_RESET:
    case SDL_RENDER_TARGETS_RESET:
      std::printf("GL device/targets reset — will recreate on next frame\n");
      break;

    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_AC_BACK:
          g_running = false;
          break;
        case SDLK_v:
          cycle_stereo_mode();
          break;
        default:
          break;
      }
      break;

#if !defined(__ANDROID__)
    case SDL_MOUSEMOTION:
      if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
        const float sens = 0.0025f;
        scene.cam_yaw   += e.motion.xrel * sens;
        scene.cam_pitch -= e.motion.yrel * sens;
        const float lim = 1.4f;
        if (scene.cam_pitch >  lim) scene.cam_pitch = lim;
        if (scene.cam_pitch < -lim) scene.cam_pitch = -lim;
      }
      break;
#endif

#if defined(__ANDROID__)
    // Do NOT cycle stereo on every finger/controller click — that pushed
    // users into anaglyph. Long-press or V (USB keyboard) can still cycle.
    case SDL_FINGERDOWN:
    case SDL_MOUSEBUTTONDOWN:
      (void)e;
      break;
#endif

    default:
      break;
  }
#if !defined(__ANDROID__)
  (void)scene;
#endif
}

static Mat4 eye_translate(float sep_sign, float eye_sep) {
  return Mat4::translate({sep_sign * eye_sep * 0.5f, 0.f, 0.f});
}

static void render_eye(Renderer& renderer, Scene& scene,
                       const Mat4& view, const Mat4& proj) {
  scene.draw(renderer, view, proj);
}

#if defined(__ANDROID__)
static void update_camera_from_sensors(Scene& scene) {
  const int count = SDL_NumSensors();
  for (int i = 0; i < count; ++i) {
    if (SDL_SensorGetDeviceType(i) != SDL_SENSOR_ACCEL)
      continue;
    SDL_Sensor* accel = SDL_SensorOpen(i);
    if (!accel) continue;
    float data[3] = {0, 0, 0};
    if (SDL_SensorGetData(accel, data, 3) == 0) {
      float ax = data[0], ay = data[1], az = data[2];
      scene.cam_pitch = std::atan2(-az, std::sqrt(ax * ax + ay * ay));
      scene.cam_yaw   = std::atan2(ax, ay);
      const float lim = 1.4f;
      if (scene.cam_pitch >  lim) scene.cam_pitch = lim;
      if (scene.cam_pitch < -lim) scene.cam_pitch = -lim;
    }
    break;
  }
}

static void android_set_immersive(SDL_Window* window) {
  SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
  // Hide system UI as much as SDL allows
  SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
}
#endif

int main(int argc, char** argv) {
  (void)argc; (void)argv;

  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#if defined(__ANDROID__)
  // Mirage Solo / Daydream: stay landscape. SDL was flipping to portrait
  // (1440x2560) via requestedOrientation=FULL_USER and then the activity died.
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_SENSOR) != 0) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
      std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
      return 1;
    }
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
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(__ANDROID__)
  flags |= SDL_WINDOW_FULLSCREEN;
#else
  flags |= SDL_WINDOW_RESIZABLE;
#endif

  SDL_Window* window = SDL_CreateWindow(
      "daydrym",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      win_w, win_h, flags);

  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }
#if defined(__ANDROID__)
  // Hard-lock landscape after create (Mirage was ending up in portrait)
  SDL_SetWindowDisplayMode(window, nullptr);
  SDL_GetWindowSize(window, &win_w, &win_h);
  DAYDRYM_LOGI("Window after create: %dx%d", win_w, win_h);
#endif

  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  if (!ctx) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetSwapInterval(1);
#if !defined(__ANDROID__)
  SDL_SetRelativeMouseMode(SDL_TRUE);
#else
  android_set_immersive(window);
#endif

  int draw_w = 0, draw_h = 0;
  SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);

  Renderer renderer;
  if (!renderer.init(draw_w, draw_h)) {
    std::fprintf(stderr, "Renderer init failed\n");
    return 1;
  }

  CardboardVr cardboard;
  g_cardboard = &cardboard;
#if defined(__ANDROID__)
  if (cardboard.init(draw_w, draw_h)) {
    DAYDRYM_LOGI("Cardboard SDK active (head tracking + lens eye matrices)");
    g_stereo = StereoMode::SideBySide;
  } else {
    DAYDRYM_LOGI("Cardboard SDK unavailable — fallback head tracking");
  }
#endif

  Scene scene;
  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  const float eye_sep = 0.065f;

  DAYDRYM_LOGI("daydrym — Blinn-Phong, shadow map, textures");
  DAYDRYM_LOGI("Stereo mode: %s", stereo_mode_name(g_stereo));

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

#if defined(__ANDROID__)
    if (!(g_cardboard && g_cardboard->ok())) {
      update_camera_from_sensors(scene);
    }
#endif

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

#if defined(__ANDROID__)
    if (g_cardboard && g_cardboard->ok()) {
      g_cardboard->set_screen_size(draw_w, draw_h);
      Mat4 head = g_cardboard->head_view(0);
      // Place the scene in front of the tracked head
      Mat4 world = Mat4::translate({-scene.cam_pos.x, -scene.cam_pos.y, -scene.cam_pos.z});
      Mat4 head_view = head * world;
      view_left  = g_cardboard->eye_from_head(0) * head_view;
      view_right = g_cardboard->eye_from_head(1) * head_view;
      proj_left  = g_cardboard->eye_projection(0, 0.1f, 100.f);
      proj_right = g_cardboard->eye_projection(1, 0.1f, 100.f);
      view = head_view;
    }
#endif

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

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Android entry point: renders the scene through the Google VR (GVR) NDK so
// that the Daydream compositor (async reprojection, lens distortion, head
// tracking) is used. Java (DaydrymActivity) owns the GvrLayout + GLSurfaceView
// and forwards the lifecycle and GL-thread callbacks here.
#include "renderer.hpp"
#include "scene.hpp"
#include "math.hpp"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <memory>
#include <vector>

#include "vr/gvr/capi/include/gvr.h"
#include "vr/gvr/capi/include/gvr_controller.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "daydrym", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "daydrym", __VA_ARGS__)

namespace {

constexpr float kZNear = 0.1f;
constexpr float kZFar = 100.f;
// How far ahead the head pose is predicted (no vsync info available).
constexpr int64_t kPredictionTimeNanos = 50 * 1000 * 1000;

// GVR matrices are row-major, ours are column-major.
Mat4 to_mat4(const gvr::Mat4f& g) {
  Mat4 r{};
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      r.m[col * 4 + row] = g.m[row][col];
  return r;
}

// Off-axis projection from GVR's per-eye field of view (degrees).
Mat4 projection_from_fov(const gvr::Rectf& fov, float znear, float zfar) {
  constexpr float kDeg2Rad = 3.14159265358979f / 180.f;
  const float l = -std::tan(fov.left * kDeg2Rad) * znear;
  const float r =  std::tan(fov.right * kDeg2Rad) * znear;
  const float b = -std::tan(fov.bottom * kDeg2Rad) * znear;
  const float t =  std::tan(fov.top * kDeg2Rad) * znear;
  Mat4 m{};
  m.m[0] = 2.f * znear / (r - l);
  m.m[5] = 2.f * znear / (t - b);
  m.m[8] = (r + l) / (r - l);
  m.m[9] = (t + b) / (t - b);
  m.m[10] = (zfar + znear) / (znear - zfar);
  m.m[11] = -1.f;
  m.m[14] = 2.f * zfar * znear / (znear - zfar);
  m.m[15] = 0.f;
  return m;
}

struct App {
  std::unique_ptr<gvr::GvrApi> gvr;
  gvr::BufferViewportList viewports;
  gvr::BufferViewport viewport;
  std::unique_ptr<gvr::SwapChain> swapchain;
  gvr::Sizei render_size{0, 0};

  std::unique_ptr<gvr::ControllerApi> controller;
  gvr::ControllerState controller_state;

  std::unique_ptr<Renderer> renderer;
  Scene scene;
  gvr::ClockTimePoint last_frame{};
  bool have_last_frame = false;
  bool night = false;

  explicit App(gvr_context_* ctx)
      : gvr(gvr::GvrApi::WrapNonOwned(ctx)),
        viewports(gvr->CreateEmptyBufferViewportList()),
        viewport(gvr->CreateBufferViewport()) {
    // Arm model + follow-gaze needs orientation, gyro and the arm model flag.
    const int32_t options = gvr::ControllerApi::DefaultOptions() |
                            GVR_CONTROLLER_ENABLE_TOUCH |
                            GVR_CONTROLLER_ENABLE_GYRO |
                            GVR_CONTROLLER_ENABLE_ARM_MODEL;
    controller = std::make_unique<gvr::ControllerApi>();
    if (!controller->Init(options, ctx)) {
      LOGE("Controller API init failed; running without controller");
      controller.reset();
    }
  }

  void resume() {
    gvr->RefreshViewerProfile();
    gvr->ResumeTracking();
    if (controller) controller->Resume();
  }

  void pause() {
    gvr->PauseTracking();
    if (controller) controller->Pause();
  }

  struct ControllerPose {
    bool valid = false;
    Mat4 model;          // controller -> world
    float ray_length = 5.f;
    bool floor_hit = false;
    Vec3 floor_point;
    ControllerInput input;
  };

  // Reads the controller, handles input (click/trigger teleports to where the
  // ray meets the floor) and returns its pose in world space.
  ControllerPose update_controller(const gvr::Mat4f& head_rotation, const Mat4& head,
                                   float dt) {
    ControllerPose pose;
    if (!controller) return pose;

    controller->ApplyArmModel(gvr->GetUserPrefs().GetControllerHandedness(),
                              gvr::kArmModelBehaviorFollowGaze, head_rotation);
    controller_state.Update(*controller);
    if (controller_state.GetApiStatus() != gvr::kControllerApiOk ||
        controller_state.GetConnectionState() != gvr::kControllerConnected)
      return pose;

    const gvr_quatf q = controller_state.GetOrientation();
    const gvr_vec3f p = controller_state.GetPosition();
    const Mat4 rot = Mat4::from_quat(q.qx, q.qy, q.qz, q.qw);

    // Arm-model position is relative to the head origin. `head` maps start
    // space to head space, so the head origin in start space is -R^T * t.
    const Vec3 t{head.m[12], head.m[13], head.m[14]};
    const Vec3 head_origin{-(head.m[0] * t.x + head.m[1] * t.y + head.m[2] * t.z),
                           -(head.m[4] * t.x + head.m[5] * t.y + head.m[6] * t.z),
                           -(head.m[8] * t.x + head.m[9] * t.y + head.m[10] * t.z)};
    // Start space -> world: the scene camera offset.
    const Vec3 origin = scene.cam_pos + head_origin + Vec3{p.x, p.y, p.z};
    const Vec3 dir{-rot.m[8], -rot.m[9], -rot.m[10]};  // controller -Z

    pose.valid = true;
    pose.model = Mat4::translate(origin) * rot;
    if (dir.y < -0.02f) {
      const float dist = -origin.y / dir.y;
      if (dist > 0.f && dist < 30.f) {
        pose.floor_hit = true;
        pose.floor_point = origin + dir * dist;
        pose.ray_length = dist;
      }
    }

    ControllerInput& in = pose.input;
    const gvr_vec2f tp = controller_state.GetTouchPos();
    in.touching = controller_state.IsTouching();
    in.touch = {tp.x, tp.y};
    in.click = controller_state.GetButtonState(gvr::kControllerButtonClick);
    in.app = controller_state.GetButtonState(gvr::kControllerButtonApp);
    in.home = controller_state.GetButtonState(gvr::kControllerButtonHome);
    in.trigger = controller_state.GetButtonState(gvr::kControllerButtonTrigger);

    // App button: toggle day / night lighting.
    if (controller_state.GetButtonDown(gvr::kControllerButtonApp)) {
      night = !night;
      scene.light.intensity = night ? 0.35f : 1.15f;
      scene.light.ambient = night ? 0.10f : 0.28f;
      LOGI("App button: %s", night ? "night" : "day");
    }

    // Touchpad drag (without clicking): walk relative to the gaze direction.
    if (in.touching && !in.click) {
      const float ox = in.touch.x - 0.5f;
      const float oy = in.touch.y - 0.5f;
      if (std::fabs(ox) > 0.1f || std::fabs(oy) > 0.1f) {
        Vec3 fwd{-head.m[2], 0.f, -head.m[10]};   // gaze, projected on the floor
        Vec3 right{head.m[0], 0.f, head.m[8]};
        fwd = fwd.normalized();
        right = right.normalized();
        const float speed = 4.f * dt;  // 2 m/s at the pad edge
        scene.cam_pos += fwd * (-oy * speed) + right * (ox * speed);
      }
    }

    if (pose.floor_hit &&
        (controller_state.GetButtonDown(gvr::kControllerButtonClick) ||
         controller_state.GetButtonDown(gvr::kControllerButtonTrigger))) {
      // Put the head above the target point (keep the current height).
      scene.cam_pos.x += pose.floor_point.x - (scene.cam_pos.x + head_origin.x);
      scene.cam_pos.z += pose.floor_point.z - (scene.cam_pos.z + head_origin.z);
    }
    return pose;
  }

  // GL thread, once per EGL context.
  void on_surface_created() {
    gvr->InitializeGl();

    render_size = gvr->GetMaximumEffectiveRenderTargetSize();
    std::vector<gvr::BufferSpec> specs;
    specs.push_back(gvr->CreateBufferSpec());
    specs[0].SetSize(render_size);
    specs[0].SetColorFormat(GVR_COLOR_FORMAT_RGBA_8888);
    specs[0].SetDepthStencilFormat(GVR_DEPTH_STENCIL_FORMAT_DEPTH_16);
    specs[0].SetSamples(1);
    swapchain = std::make_unique<gvr::SwapChain>(gvr->CreateSwapChain(specs));

    renderer = std::make_unique<Renderer>();
    if (!renderer->init(render_size.width, render_size.height)) {
      LOGE("Renderer init failed");
      renderer.reset();
    }
    have_last_frame = false;
    LOGI("GL_VENDOR: %s, GL_RENDERER: %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    LOGI("GL_VERSION: %s, GLSL: %s", glGetString(GL_VERSION),
         glGetString(GL_SHADING_LANGUAGE_VERSION));
    LOGI("GVR GL ready, render target %dx%d", render_size.width, render_size.height);
  }

  // GL thread, once per frame.
  void on_draw_frame() {
    if (!renderer) return;

    gvr::ClockTimePoint now = gvr::GvrApi::GetTimePointNow();
    float dt = 0.f;
    if (have_last_frame)
      dt = static_cast<float>(now.monotonic_system_time_nanos -
                              last_frame.monotonic_system_time_nanos) * 1e-9f;
    last_frame = now;
    have_last_frame = true;
    if (dt > 0.05f) dt = 0.05f;
    scene.update(dt);

    gvr::ClockTimePoint target = now;
    target.monotonic_system_time_nanos += kPredictionTimeNanos;
    const gvr::Mat4f head_pose = gvr->GetHeadSpaceFromStartSpaceRotation(target);
    const Mat4 head = to_mat4(gvr->ApplyNeckModel(head_pose, 1.0f));

    const ControllerPose ctrl = update_controller(head_pose, head, dt);

    viewports.SetToRecommendedBufferViewports();

    // The shadow pass renders into its own FBO; do it before binding GVR's.
    renderer->begin_shadow_pass(scene.light, {0.f, 1.f, -2.f});
    scene.draw_shadow(*renderer);
    renderer->end_shadow_pass();

    gvr::Frame frame = swapchain->AcquireFrame();
    frame.BindBuffer(0);
    renderer->begin_frame(scene.light, scene.cam_pos);  // clears the target

    const Mat4 world = Mat4::translate(
        {-scene.cam_pos.x, -scene.cam_pos.y, -scene.cam_pos.z});
    for (int eye = 0; eye < 2; ++eye) {
      viewports.GetBufferViewport(eye, &viewport);
      const gvr::Rectf uv = viewport.GetSourceUv();
      const int x = static_cast<int>(uv.left * render_size.width);
      const int y = static_cast<int>(uv.bottom * render_size.height);
      const int w = static_cast<int>((uv.right - uv.left) * render_size.width);
      const int h = static_cast<int>((uv.top - uv.bottom) * render_size.height);
      glViewport(x, y, w, h);

      const Mat4 eye_from_head = to_mat4(gvr->GetEyeFromHeadMatrix(
          static_cast<gvr::Eye>(eye)));
      const Mat4 view = eye_from_head * head * world;
      const Mat4 proj = projection_from_fov(viewport.GetSourceFov(), kZNear, kZFar);
      scene.draw(*renderer, view, proj);
      if (ctrl.valid) {
        scene.draw_controller(*renderer, view, proj, ctrl.model, ctrl.ray_length,
                               ctrl.input);
        if (ctrl.floor_hit) scene.draw_marker(*renderer, view, proj, ctrl.floor_point);
      }
    }
    renderer->end_frame();

    frame.Unbind();
    frame.Submit(viewports, gvr->GetHeadSpaceFromStartSpaceRotation(target));
  }
};

App* g_app = nullptr;

}  // namespace

#define JNI_FN(name) Java_com_daydrym_DaydrymActivity_##name

extern "C" {

JNIEXPORT void JNICALL JNI_FN(nativeInit)(JNIEnv*, jobject, jlong gvr_context) {
  delete g_app;
  g_app = new App(reinterpret_cast<gvr_context_*>(gvr_context));
  LOGI("GVR initialised (viewer: %s)", g_app->gvr->GetViewerVendor());
}

JNIEXPORT void JNICALL JNI_FN(nativeDestroy)(JNIEnv*, jobject) {
  delete g_app;
  g_app = nullptr;
}

JNIEXPORT void JNICALL JNI_FN(nativeOnResume)(JNIEnv*, jobject) {
  if (g_app) g_app->resume();
}

JNIEXPORT void JNICALL JNI_FN(nativeOnPause)(JNIEnv*, jobject) {
  if (g_app) g_app->pause();
}

JNIEXPORT void JNICALL JNI_FN(nativeOnSurfaceCreated)(JNIEnv*, jobject) {
  if (g_app) g_app->on_surface_created();
}

JNIEXPORT void JNICALL JNI_FN(nativeOnDrawFrame)(JNIEnv*, jobject) {
  if (g_app) g_app->on_draw_frame();
}

}  // extern "C"

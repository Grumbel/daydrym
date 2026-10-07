// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
#include "cardboard_vr.hpp"

#include <cstdio>

#if defined(DAYDRYM_USE_CARDBOARD) && defined(__ANDROID__)

#include <cardboard.h>
#include <time.h>

#if defined(SDL_VIDEO_DRIVER_ANDROID) || defined(__ANDROID__)
#  include <SDL.h>
#  include <SDL_system.h>
#  include <jni.h>
#endif

static int64_t boottime_ns() {
  timespec ts{};
  clock_gettime(CLOCK_BOOTTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
         static_cast<int64_t>(ts.tv_nsec);
}

static bool init_android_cardboard() {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
  if (!env) {
    std::fprintf(stderr, "cardboard: no JNIEnv\n");
    return false;
  }
  JavaVM* jvm = nullptr;
  if (env->GetJavaVM(&jvm) != JNI_OK || !jvm) {
    std::fprintf(stderr, "cardboard: GetJavaVM failed\n");
    return false;
  }
  jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
  if (!activity) {
    std::fprintf(stderr, "cardboard: no Activity\n");
    return false;
  }
  Cardboard_initializeAndroid(jvm, activity);
  env->DeleteLocalRef(activity);
  return true;
}

void CardboardVr::destroy_lens() {
  if (lens_distortion_) {
    CardboardLensDistortion_destroy(
        static_cast<CardboardLensDistortion*>(lens_distortion_));
    lens_distortion_ = nullptr;
  }
}

void CardboardVr::create_lens() {
  destroy_lens();
  uint8_t* params = nullptr;
  int size = 0;
  CardboardQrCode_getSavedDeviceParams(&params, &size);
  if (!params || size <= 0) {
    CardboardQrCode_getCardboardV1DeviceParams(&params, &size);
  }
  if (!params || size <= 0) {
    std::fprintf(stderr, "cardboard: no device params\n");
    return;
  }
  lens_distortion_ = CardboardLensDistortion_create(params, size, width_, height_);
  CardboardQrCode_destroy(params);
  std::fprintf(stderr, "cardboard: lens distortion created %dx%d\n", width_, height_);
}

bool CardboardVr::init(int screen_width, int screen_height) {
  width_ = screen_width;
  height_ = screen_height;
  if (!init_android_cardboard()) {
    ok_ = false;
    return false;
  }
  head_tracker_ = CardboardHeadTracker_create();
  if (!head_tracker_) {
    ok_ = false;
    return false;
  }
  CardboardHeadTracker_resume(static_cast<CardboardHeadTracker*>(head_tracker_));
  create_lens();
  ok_ = (head_tracker_ != nullptr && lens_distortion_ != nullptr);
  std::fprintf(stderr, "cardboard: init %s\n", ok_ ? "ok" : "failed");
  return ok_;
}

CardboardVr::~CardboardVr() {
  destroy_lens();
  if (head_tracker_) {
    CardboardHeadTracker_destroy(static_cast<CardboardHeadTracker*>(head_tracker_));
    head_tracker_ = nullptr;
  }
}

void CardboardVr::pause() {
  if (head_tracker_)
    CardboardHeadTracker_pause(static_cast<CardboardHeadTracker*>(head_tracker_));
}

void CardboardVr::resume() {
  if (head_tracker_)
    CardboardHeadTracker_resume(static_cast<CardboardHeadTracker*>(head_tracker_));
}

void CardboardVr::set_screen_size(int screen_width, int screen_height) {
  if (width_ == screen_width && height_ == screen_height) return;
  width_ = screen_width;
  height_ = screen_height;
  if (ok_) create_lens();
}

Mat4 CardboardVr::head_view(int64_t timestamp_ns) const {
  if (!head_tracker_) return Mat4::identity();
  float pos[3] = {0, 0, 0};
  float ori[4] = {0, 0, 0, 1};
  if (timestamp_ns <= 0) timestamp_ns = boottime_ns();
  // Landscape left is typical for phone VR in a landscape activity
  CardboardHeadTracker_getPose(
      static_cast<CardboardHeadTracker*>(head_tracker_),
      timestamp_ns,
      kLandscapeLeft,
      pos, ori);
  // Cardboard gives head-from-world orientation as quaternion; build view =
  // inverse rotation * -translation. For 3DoF, position is usually origin.
  Mat4 R = Mat4::from_quat(ori[0], ori[1], ori[2], ori[3]);
  // View matrix: inverse of head pose. For pure rotation, transpose == inverse.
  Mat4 view{};
  // transpose upper 3x3
  view.m[0] = R.m[0]; view.m[4] = R.m[1]; view.m[8]  = R.m[2];
  view.m[1] = R.m[4]; view.m[5] = R.m[5]; view.m[9]  = R.m[6];
  view.m[2] = R.m[8]; view.m[6] = R.m[9]; view.m[10] = R.m[10];
  // apply translation in camera space
  view.m[12] = -(view.m[0] * pos[0] + view.m[4] * pos[1] + view.m[8]  * pos[2]);
  view.m[13] = -(view.m[1] * pos[0] + view.m[5] * pos[1] + view.m[9]  * pos[2]);
  view.m[14] = -(view.m[2] * pos[0] + view.m[6] * pos[1] + view.m[10] * pos[2]);
  return view;
}

Mat4 CardboardVr::eye_from_head(int eye) const {
  float m[16];
  for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.f : 0.f;
  if (lens_distortion_) {
    CardboardLensDistortion_getEyeFromHeadMatrix(
        static_cast<CardboardLensDistortion*>(lens_distortion_),
        eye == 0 ? kLeft : kRight, m);
  }
  return Mat4::from_gl_array(m);
}

Mat4 CardboardVr::eye_projection(int eye, float z_near, float z_far) const {
  float m[16];
  for (int i = 0; i < 16; ++i) m[i] = 0.f;
  m[0] = m[5] = m[10] = m[15] = 1.f;
  if (lens_distortion_) {
    CardboardLensDistortion_getProjectionMatrix(
        static_cast<CardboardLensDistortion*>(lens_distortion_),
        eye == 0 ? kLeft : kRight, z_near, z_far, m);
  }
  return Mat4::from_gl_array(m);
}

#else // stub

CardboardVr::~CardboardVr() = default;
bool CardboardVr::init(int, int) { return false; }
void CardboardVr::pause() {}
void CardboardVr::resume() {}
void CardboardVr::set_screen_size(int, int) {}
void CardboardVr::destroy_lens() {}
void CardboardVr::create_lens() {}
Mat4 CardboardVr::head_view(int64_t) const { return Mat4::identity(); }
Mat4 CardboardVr::eye_from_head(int) const { return Mat4::identity(); }
Mat4 CardboardVr::eye_projection(int, float, float) const {
  return Mat4::perspective(1.0f, 1.0f, 0.1f, 100.f);
}

#endif

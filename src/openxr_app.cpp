// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "openxr_app.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_XLIB
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define DAYDRYM_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define DAYDRYM_LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)

namespace {

const char* xr_str(XrInstance inst, XrResult r) {
  static char buf[XR_MAX_RESULT_STRING_SIZE];
  if (inst != XR_NULL_HANDLE)
    xrResultToString(inst, r, buf);
  else
    std::snprintf(buf, sizeof(buf), "XrResult(%d)", static_cast<int>(r));
  return buf;
}

bool xr_check(XrInstance inst, XrResult r, const char* what) {
  if (XR_SUCCEEDED(r)) return true;
  DAYDRYM_LOGE("OpenXR: %s failed: %s", what, xr_str(inst, r));
  return false;
}

struct Swapchain {
  XrSwapchain handle = XR_NULL_HANDLE;
  int32_t width = 0;
  int32_t height = 0;
  std::vector<XrSwapchainImageOpenGLKHR> images;
};

enum { kHandLeft = 0, kHandRight = 1, kHandCount = 2 };

struct HandInput {
  bool aim_active = false;
  Mat4 aim_model = Mat4::identity();
  ControllerInput buttons{};
  bool select_down = false;
  bool select_clicked = false;  // edge this frame
  float move_x = 0.f;
  float move_y = 0.f;
  bool floor_hit = false;
  Vec3 floor_point{};
  float ray_length = 4.f;
};

struct OpenXRState {
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system_id = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace ref_space = XR_NULL_HANDLE;
  XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
  bool session_running = false;
  bool exit_requested = false;

  XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  std::vector<XrViewConfigurationView> config_views;
  std::vector<XrView> views;
  std::vector<XrCompositionLayerProjectionView> proj_views;
  std::vector<Swapchain> swapchains;

  // Per-eye FBO + shared depth renderbuffer
  std::vector<GLuint> eye_fbos;
  GLuint depth_rbo = 0;
  int depth_w = 0;
  int depth_h = 0;

  XrGraphicsBindingOpenGLXlibKHR graphics_binding{};

  // Actions / controllers
  XrActionSet action_set = XR_NULL_HANDLE;
  XrAction aim_pose_action = XR_NULL_HANDLE;
  XrAction grip_pose_action = XR_NULL_HANDLE;
  XrAction select_action = XR_NULL_HANDLE;
  XrAction menu_action = XR_NULL_HANDLE;
  XrAction move_action = XR_NULL_HANDLE;
  XrPath hand_path[kHandCount]{};
  XrSpace aim_space[kHandCount]{XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace grip_space[kHandCount]{XR_NULL_HANDLE, XR_NULL_HANDLE};
  bool actions_attached = false;

  // Play-space locomotion offset (stage/local), applied to views
  Vec3 locomotion{0.f, 0.f, 0.f};
  bool night = false;
  bool menu_was_down = false;
  HandInput hands[kHandCount]{};
};

bool fill_glx_binding(SDL_Window* window, XrGraphicsBindingOpenGLXlibKHR* out) {
  SDL_SysWMinfo wminfo;
  SDL_VERSION(&wminfo.version);
  if (!SDL_GetWindowWMInfo(window, &wminfo)) {
    DAYDRYM_LOGE("SDL_GetWindowWMInfo: %s", SDL_GetError());
    return false;
  }
  if (wminfo.subsystem != SDL_SYSWM_X11) {
    DAYDRYM_LOGE("OpenXR path requires an X11 window (got subsystem %d). "
                 "Use X11 or XWayland.", static_cast<int>(wminfo.subsystem));
    return false;
  }

  Display* dpy = wminfo.info.x11.display;
  Window xwin = wminfo.info.x11.window;
  GLXContext glx_ctx = glXGetCurrentContext();
  if (!dpy || !xwin || !glx_ctx) {
    DAYDRYM_LOGE("Missing X11 Display, Window, or current GLX context");
    return false;
  }

  XWindowAttributes attrs{};
  if (!XGetWindowAttributes(dpy, xwin, &attrs)) {
    DAYDRYM_LOGE("XGetWindowAttributes failed");
    return false;
  }
  const VisualID visualid = XVisualIDFromVisual(attrs.visual);

  int nconfigs = 0;
  GLXFBConfig* configs = glXGetFBConfigs(dpy, DefaultScreen(dpy), &nconfigs);
  if (!configs || nconfigs <= 0) {
    DAYDRYM_LOGE("glXGetFBConfigs returned nothing");
    return false;
  }
  GLXFBConfig chosen = nullptr;
  for (int i = 0; i < nconfigs; ++i) {
    XVisualInfo* vi = glXGetVisualFromFBConfig(dpy, configs[i]);
    if (!vi) continue;
    if (vi->visualid == visualid) {
      chosen = configs[i];
      XFree(vi);
      break;
    }
    XFree(vi);
  }
  XFree(configs);
  if (!chosen) {
    DAYDRYM_LOGE("No GLXFBConfig matching window visual 0x%lx",
                 static_cast<unsigned long>(visualid));
    return false;
  }

  std::memset(out, 0, sizeof(*out));
  out->type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
  out->xDisplay = dpy;
  out->visualid = static_cast<uint32_t>(visualid);
  out->glxFBConfig = chosen;
  out->glxDrawable = static_cast<GLXDrawable>(xwin);
  out->glxContext = glx_ctx;
  return true;
}

void print_runtime_load_help() {
  DAYDRYM_LOGE(
      "OpenXR loader could not load an active runtime.\n"
      "  Common causes on Nix / nix run:\n"
      "  - SteamVR is selected in ~/.config/openxr/1/active_runtime.json but its\n"
      "    libraries (e.g. libuuid) are not visible to the Nix dynamic linker.\n"
      "  Workarounds:\n"
      "  1) Prefer Monado (fully Nix-friendly):\n"
      "       export XR_RUNTIME_JSON=$MONADO_RUNTIME   # or path to openxr_monado.json\n"
      "       daydrym --openxr\n"
      "     On NixOS: services.monado.enable = true; defaultRuntime = true;\n"
      "  2) Keep SteamVR but run under an FHS env so system/Steam libs resolve:\n"
      "       steam-run daydrym --openxr\n"
      "     or:  nix run .#daydrym-steamvr -- --openxr\n"
      "  3) Point XR_RUNTIME_JSON at any other OpenXR runtime manifest.\n"
      "  Active config is usually ~/.config/openxr/1/active_runtime.json");
}

bool create_instance(OpenXRState& st) {
  if (const char* j = std::getenv("XR_RUNTIME_JSON"))
    DAYDRYM_LOGI("XR_RUNTIME_JSON=%s", j);
  else
    DAYDRYM_LOGI("XR_RUNTIME_JSON unset (loader uses active_runtime.json)");

  // Enumerate extensions; require XR_KHR_opengl_enable
  uint32_t ext_count = 0;
  XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
  if (!xr_check(XR_NULL_HANDLE, r, "xrEnumerateInstanceExtensionProperties(count)")) {
    if (r == XR_ERROR_RUNTIME_UNAVAILABLE)
      print_runtime_load_help();
    return false;
  }
  std::vector<XrExtensionProperties> exts(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});
  r = xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, exts.data());
  if (!xr_check(XR_NULL_HANDLE, r, "xrEnumerateInstanceExtensionProperties"))
    return false;

  bool has_opengl = false;
  for (const auto& e : exts) {
    if (std::strcmp(e.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0)
      has_opengl = true;
  }
  if (!has_opengl) {
    DAYDRYM_LOGE("Runtime lacks %s", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
    return false;
  }

  const char* enabled[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};

  XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strncpy(ci.applicationInfo.applicationName, "daydrym", XR_MAX_APPLICATION_NAME_SIZE - 1);
  ci.applicationInfo.applicationVersion = 1;
  std::strncpy(ci.applicationInfo.engineName, "daydrym", XR_MAX_ENGINE_NAME_SIZE - 1);
  ci.applicationInfo.engineVersion = 1;
  ci.enabledExtensionCount = 1;
  ci.enabledExtensionNames = enabled;

  // Prefer current API version; fall back to 1.0 if the runtime rejects it.
  ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  r = xrCreateInstance(&ci, &st.instance);
  if (XR_FAILED(r)) {
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    r = xrCreateInstance(&ci, &st.instance);
  }
  if (!xr_check(XR_NULL_HANDLE, r, "xrCreateInstance")) {
    DAYDRYM_LOGE(
        "xrCreateInstance failed (%s).\n"
        "  If the active runtime is SteamVR:\n"
        "  - Start SteamVR first (headset on / streaming client connected), then retry.\n"
        "  - Prefer the official manifest (not a hand-edited path):\n"
        "      ln -sf ~/.steam/steam/steamapps/common/SteamVR/steamxr_linux64.json \\\n"
        "         ~/.config/openxr/1/active_runtime.json\n"
        "    (adjust path if Steam lives under ~/.local/share/Steam or debian-installation)\n"
        "  - Or point at that file for this process only:\n"
        "      XR_RUNTIME_JSON=.../SteamVR/steamxr_linux64.json daydrym --openxr\n"
        "  - Check SteamVR logs under ~/.steam/steam/logs/ (vrclient / openxr).\n"
        "  For a Nix-native path without SteamVR, use Monado instead.",
        xr_str(XR_NULL_HANDLE, r));
    return false;
  }

  XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_SUCCEEDED(xrGetInstanceProperties(st.instance, &props))) {
    DAYDRYM_LOGI("OpenXR runtime: %s (v%u.%u.%u)",
                 props.runtimeName,
                 XR_VERSION_MAJOR(props.runtimeVersion),
                 XR_VERSION_MINOR(props.runtimeVersion),
                 XR_VERSION_PATCH(props.runtimeVersion));
  }
  return true;
}


bool create_actions(OpenXRState& st) {
  XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strncpy(asci.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
  std::strncpy(asci.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
  asci.priority = 0;
  XrResult r = xrCreateActionSet(st.instance, &asci, &st.action_set);
  if (!xr_check(st.instance, r, "xrCreateActionSet"))
    return false;

  xrStringToPath(st.instance, "/user/hand/left", &st.hand_path[kHandLeft]);
  xrStringToPath(st.instance, "/user/hand/right", &st.hand_path[kHandRight]);
  XrPath hands[2] = {st.hand_path[kHandLeft], st.hand_path[kHandRight]};

  auto make_action = [&](const char* name, const char* localized, XrActionType type,
                         XrAction* out) -> bool {
    XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
    std::strncpy(aci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(aci.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    aci.actionType = type;
    aci.countSubactionPaths = 2;
    aci.subactionPaths = hands;
    return xr_check(st.instance, xrCreateAction(st.action_set, &aci, out), name);
  };

  if (!make_action("aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT, &st.aim_pose_action))
    return false;
  if (!make_action("grip_pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT, &st.grip_pose_action))
    return false;
  if (!make_action("select", "Select", XR_ACTION_TYPE_BOOLEAN_INPUT, &st.select_action))
    return false;
  if (!make_action("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, &st.menu_action))
    return false;
  if (!make_action("move", "Move", XR_ACTION_TYPE_VECTOR2F_INPUT, &st.move_action))
    return false;
  // Action spaces created in attach_actions after xrBeginSession + attach.

  struct Binding { XrAction action; const char* path; };
  auto suggest = [&](const char* profile, std::initializer_list<Binding> binds) {
    XrPath profile_path{};
    if (XR_FAILED(xrStringToPath(st.instance, profile, &profile_path)))
      return;
    std::vector<XrActionSuggestedBinding> sb;
    for (const Binding& b : binds) {
      XrPath p{};
      if (XR_FAILED(xrStringToPath(st.instance, b.path, &p)))
        continue;
      sb.push_back(XrActionSuggestedBinding{b.action, p});
    }
    if (sb.empty())
      return;
    XrInteractionProfileSuggestedBinding sug{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sug.interactionProfile = profile_path;
    sug.countSuggestedBindings = static_cast<uint32_t>(sb.size());
    sug.suggestedBindings = sb.data();
    XrResult sr = xrSuggestInteractionProfileBindings(st.instance, &sug);
    if (XR_FAILED(sr))
      DAYDRYM_LOGI("OpenXR: bindings for %s not accepted (%s)", profile, xr_str(st.instance, sr));
    else
      DAYDRYM_LOGI("OpenXR: suggested bindings for %s", profile);
  };

  suggest("/interaction_profiles/khr/simple_controller", {
      {st.aim_pose_action, "/user/hand/left/input/aim/pose"},
      {st.aim_pose_action, "/user/hand/right/input/aim/pose"},
      {st.grip_pose_action, "/user/hand/left/input/grip/pose"},
      {st.grip_pose_action, "/user/hand/right/input/grip/pose"},
      {st.select_action, "/user/hand/left/input/select/click"},
      {st.select_action, "/user/hand/right/input/select/click"},
      {st.menu_action, "/user/hand/left/input/menu/click"},
      {st.menu_action, "/user/hand/right/input/menu/click"},
  });
  suggest("/interaction_profiles/oculus/touch_controller", {
      {st.aim_pose_action, "/user/hand/left/input/aim/pose"},
      {st.aim_pose_action, "/user/hand/right/input/aim/pose"},
      {st.grip_pose_action, "/user/hand/left/input/grip/pose"},
      {st.grip_pose_action, "/user/hand/right/input/grip/pose"},
      {st.select_action, "/user/hand/left/input/x/click"},
      {st.select_action, "/user/hand/right/input/a/click"},
      {st.menu_action, "/user/hand/left/input/menu/click"},
      {st.menu_action, "/user/hand/right/input/b/click"},
      {st.move_action, "/user/hand/left/input/thumbstick"},
      {st.move_action, "/user/hand/right/input/thumbstick"},
  });
  // Also map trigger as select via a second suggestion is not allowed per profile;
  // use a/x for teleport (clear digital click). Thumbstick for locomotion.
  suggest("/interaction_profiles/valve/index_controller", {
      {st.aim_pose_action, "/user/hand/left/input/aim/pose"},
      {st.aim_pose_action, "/user/hand/right/input/aim/pose"},
      {st.grip_pose_action, "/user/hand/left/input/grip/pose"},
      {st.grip_pose_action, "/user/hand/right/input/grip/pose"},
      {st.select_action, "/user/hand/left/input/trigger/click"},
      {st.select_action, "/user/hand/right/input/trigger/click"},
      {st.menu_action, "/user/hand/left/input/b/click"},
      {st.menu_action, "/user/hand/right/input/b/click"},
      {st.move_action, "/user/hand/left/input/thumbstick"},
      {st.move_action, "/user/hand/right/input/thumbstick"},
  });
  suggest("/interaction_profiles/htc/vive_controller", {
      {st.aim_pose_action, "/user/hand/left/input/aim/pose"},
      {st.aim_pose_action, "/user/hand/right/input/aim/pose"},
      {st.grip_pose_action, "/user/hand/left/input/grip/pose"},
      {st.grip_pose_action, "/user/hand/right/input/grip/pose"},
      {st.select_action, "/user/hand/left/input/trigger/click"},
      {st.select_action, "/user/hand/right/input/trigger/click"},
      {st.menu_action, "/user/hand/left/input/menu/click"},
      {st.menu_action, "/user/hand/right/input/menu/click"},
      {st.move_action, "/user/hand/left/input/trackpad"},
      {st.move_action, "/user/hand/right/input/trackpad"},
  });

  return true;
}

bool attach_actions(OpenXRState& st) {
  if (st.actions_attached || st.action_set == XR_NULL_HANDLE)
    return true;
  XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  ai.countActionSets = 1;
  ai.actionSets = &st.action_set;
  if (!xr_check(st.instance, xrAttachSessionActionSets(st.session, &ai),
                "xrAttachSessionActionSets"))
    return false;
  st.actions_attached = true;
  DAYDRYM_LOGI("OpenXR action sets attached");

  // Create action spaces after begin+attach so the runtime can resolve poses.
  for (int h = 0; h < kHandCount; ++h) {
    XrActionSpaceCreateInfo spci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spci.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
    spci.subactionPath = st.hand_path[h];
    if (st.aim_space[h] == XR_NULL_HANDLE) {
      spci.action = st.aim_pose_action;
      if (!xr_check(st.instance, xrCreateActionSpace(st.session, &spci, &st.aim_space[h]),
                    "xrCreateActionSpace(aim)"))
        return false;
    }
    if (st.grip_space[h] == XR_NULL_HANDLE) {
      spci.action = st.grip_pose_action;
      if (!xr_check(st.instance, xrCreateActionSpace(st.session, &spci, &st.grip_space[h]),
                    "xrCreateActionSpace(grip)"))
        return false;
    }
  }
  return true;
}

std::string path_str(XrInstance inst, XrPath path) {
  if (path == XR_NULL_PATH)
    return "(null)";
  char buf[XR_MAX_PATH_LENGTH];
  uint32_t len = 0;
  if (XR_FAILED(xrPathToString(inst, path, sizeof(buf), &len, buf)) || len == 0)
    return "(unresolved)";
  return std::string(buf);
}

void log_interaction_profiles(OpenXRState& st, const char* reason) {
  if (st.session == XR_NULL_HANDLE)
    return;
  DAYDRYM_LOGI("OpenXR interaction profiles (%s):", reason);
  static const char* user_paths[] = {
      "/user/hand/left",
      "/user/hand/right",
      "/user/head",
      "/user/gamepad",
      "/user/treadmill",
  };
  for (const char* up : user_paths) {
    XrPath user{};
    if (XR_FAILED(xrStringToPath(st.instance, up, &user)))
      continue;
    XrInteractionProfileState ips{XR_TYPE_INTERACTION_PROFILE_STATE};
    XrResult r = xrGetCurrentInteractionProfile(st.session, user, &ips);
    if (XR_FAILED(r)) {
      DAYDRYM_LOGI("  %s: query failed (%s)", up, xr_str(st.instance, r));
      continue;
    }
    DAYDRYM_LOGI("  %s -> %s", up, path_str(st.instance, ips.interactionProfile).c_str());
  }
}

void log_bound_sources(OpenXRState& st, XrAction action, const char* action_name) {
  if (action == XR_NULL_HANDLE)
    return;
  uint32_t count = 0;
  XrBoundSourcesForActionEnumerateInfo info{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
  info.action = action;
  if (XR_FAILED(xrEnumerateBoundSourcesForAction(st.session, &info, 0, &count, nullptr)))
    return;
  if (count == 0) {
    DAYDRYM_LOGI("  action '%s': (no bound sources)", action_name);
    return;
  }
  std::vector<XrPath> sources(count);
  if (XR_FAILED(xrEnumerateBoundSourcesForAction(st.session, &info, count, &count, sources.data())))
    return;
  DAYDRYM_LOGI("  action '%s' bound sources (%u):", action_name, count);
  for (uint32_t i = 0; i < count; ++i)
    DAYDRYM_LOGI("    - %s", path_str(st.instance, sources[i]).c_str());
}

void log_input_diagnostics(OpenXRState& st) {
  log_interaction_profiles(st, "diagnostics");
  DAYDRYM_LOGI("OpenXR action bindings currently active:");
  log_bound_sources(st, st.aim_pose_action, "aim_pose");
  log_bound_sources(st, st.grip_pose_action, "grip_pose");
  log_bound_sources(st, st.select_action, "select");
  log_bound_sources(st, st.menu_action, "menu");
  log_bound_sources(st, st.move_action, "move");
}

void update_hand_ray(HandInput& hand) {
  hand.floor_hit = false;
  hand.ray_length = 4.f;
  if (!hand.aim_active)
    return;
  const Mat4& m = hand.aim_model;
  Vec3 origin{m.m[12], m.m[13], m.m[14]};
  // Column 2 of the rotation is local +Z; controller aims along -Z.
  Vec3 dir{-m.m[8], -m.m[9], -m.m[10]};
  dir = dir.normalized();
  if (std::fabs(dir.y) < 1e-4f)
    return;
  const float t = -origin.y / dir.y;
  if (t <= 0.05f || t > 12.f)
    return;
  hand.floor_point = {origin.x + dir.x * t, 0.f, origin.z + dir.z * t};
  hand.floor_hit = true;
  hand.ray_length = t;
}

void sync_actions(OpenXRState& st, XrTime time, float dt, Scene& scene,
                  const Mat4& head_view) {
  if (!st.actions_attached || st.action_set == XR_NULL_HANDLE)
    return;

  XrActiveActionSet active{st.action_set, XR_NULL_PATH};
  XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets = 1;
  sync.activeActionSets = &active;
  if (XR_FAILED(xrSyncActions(st.session, &sync)))
    return;

  // Periodic action-state dump (every ~2s) so SteamVR/ALVR binding gaps are visible
  static double accum = 0.0;
  accum += dt;
  if (accum >= 2.0) {
    accum = 0.0;
    DAYDRYM_LOGI("OpenXR action states:");
    for (int h = 0; h < kHandCount; ++h) {
      const char* hand = (h == kHandLeft) ? "left" : "right";
      XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
      gi.subactionPath = st.hand_path[h];

      gi.action = st.aim_pose_action;
      XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
      bool pose_ok = XR_SUCCEEDED(xrGetActionStatePose(st.session, &gi, &pose));
      gi.action = st.select_action;
      XrActionStateBoolean sel{XR_TYPE_ACTION_STATE_BOOLEAN};
      bool sel_ok = XR_SUCCEEDED(xrGetActionStateBoolean(st.session, &gi, &sel));
      gi.action = st.menu_action;
      XrActionStateBoolean menu{XR_TYPE_ACTION_STATE_BOOLEAN};
      bool menu_ok = XR_SUCCEEDED(xrGetActionStateBoolean(st.session, &gi, &menu));
      gi.action = st.move_action;
      XrActionStateVector2f mv{XR_TYPE_ACTION_STATE_VECTOR2F};
      bool mv_ok = XR_SUCCEEDED(xrGetActionStateVector2f(st.session, &gi, &mv));

      DAYDRYM_LOGI(
          "  %s: pose_active=%d select_active=%d/%d menu_active=%d/%d move_active=%d (%.2f,%.2f)",
          hand,
          pose_ok && pose.isActive,
          sel_ok && sel.isActive, sel_ok ? (int)sel.currentState : 0,
          menu_ok && menu.isActive, menu_ok ? (int)menu.currentState : 0,
          mv_ok && mv.isActive,
          (mv_ok && mv.isActive) ? mv.currentState.x : 0.f,
          (mv_ok && mv.isActive) ? mv.currentState.y : 0.f);
    }
  }

  bool menu_down = false;
  float move_x = 0.f, move_y = 0.f;

  for (int h = 0; h < kHandCount; ++h) {
    HandInput prev = st.hands[h];
    HandInput& hand = st.hands[h];
    hand = HandInput{};

    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.subactionPath = st.hand_path[h];

    // Aim pose preferred; fall back to grip if aim has no tracked position.
    auto try_locate = [&](XrAction action, XrSpace space, const char* label) -> bool {
      gi.action = action;
      XrActionStatePose pose_st{XR_TYPE_ACTION_STATE_POSE};
      if (XR_FAILED(xrGetActionStatePose(st.session, &gi, &pose_st)) || !pose_st.isActive)
        return false;
      if (space == XR_NULL_HANDLE)
        return false;
      XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
      if (XR_FAILED(xrLocateSpace(space, st.ref_space, time, &loc)))
        return false;
      const auto f = loc.locationFlags;
      const bool ori = (f & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
      const bool pos = (f & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
      const bool pos_trk = (f & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
      if (!ori)
        return false;
      const auto& p = loc.pose.position;
      const auto& o = loc.pose.orientation;
      // Use pose even if position is only VALID (not TRACKED); still better than origin.
      const float px = pos ? p.x : 0.f;
      const float py = pos ? p.y : 0.f;
      const float pz = pos ? p.z : 0.f;
      hand.aim_active = true;
      hand.aim_model = Mat4::model_from_pose(px, py, pz, o.x, o.y, o.z, o.w);
      static double pose_log_accum = 0.0;
      pose_log_accum += dt;
      if (pose_log_accum >= 2.0 && h == kHandRight) {
        pose_log_accum = 0.0;
        DAYDRYM_LOGI(
            "  pose[%s] flags=0x%x pos_tracked=%d pos=(%.3f,%.3f,%.3f)",
            label, static_cast<unsigned>(f), pos_trk ? 1 : 0, px, py, pz);
      }
      return pos_trk || pos;
    };
    if (!try_locate(st.aim_pose_action, st.aim_space[h], "aim"))
      try_locate(st.grip_pose_action, st.grip_space[h], "grip");

    // Select
    gi.action = st.select_action;
    XrActionStateBoolean sel{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(st.session, &gi, &sel)) && sel.isActive) {
      hand.select_down = sel.currentState;
      hand.buttons.trigger = sel.currentState;
      hand.buttons.click = sel.currentState;
      hand.select_clicked = sel.currentState && !prev.select_down;
    }

    // Menu
    gi.action = st.menu_action;
    XrActionStateBoolean menu{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(xrGetActionStateBoolean(st.session, &gi, &menu)) && menu.isActive) {
      if (menu.currentState)
        menu_down = true;
      hand.buttons.app = menu.currentState;
    }

    // Move (thumbstick / trackpad)
    gi.action = st.move_action;
    XrActionStateVector2f mv{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_SUCCEEDED(xrGetActionStateVector2f(st.session, &gi, &mv)) && mv.isActive) {
      hand.move_x = mv.currentState.x;
      hand.move_y = mv.currentState.y;
      // Prefer the stick with larger magnitude
      if (std::fabs(hand.move_x) + std::fabs(hand.move_y) >
          std::fabs(move_x) + std::fabs(move_y)) {
        move_x = hand.move_x;
        move_y = hand.move_y;
      }
      hand.buttons.touching = (std::fabs(move_x) > 0.05f || std::fabs(move_y) > 0.05f);
      hand.buttons.touch = {move_x * 0.5f + 0.5f, move_y * 0.5f + 0.5f};
    }

    update_hand_ray(hand);

    // Teleport on select click when aiming at the floor
    if (hand.select_clicked && hand.floor_hit) {
      st.locomotion.x = hand.floor_point.x;
      st.locomotion.z = hand.floor_point.z;
      // Keep locomotion.y at 0 (floor); HMD height comes from tracking
      DAYDRYM_LOGI("Teleport to (%.2f, %.2f)", st.locomotion.x, st.locomotion.z);
    }
  }

  if (menu_down && !st.menu_was_down) {
    st.night = !st.night;
    scene.light.intensity = st.night ? 0.35f : 1.15f;
    scene.light.ambient = st.night ? 0.10f : 0.28f;
    DAYDRYM_LOGI("Menu: %s", st.night ? "night" : "day");
  }
  st.menu_was_down = menu_down;

  // Smooth locomotion relative to head gaze on the floor plane
  const float dead = 0.15f;
  if (std::fabs(move_x) > dead || std::fabs(move_y) > dead) {
    // head_view maps world->view; its inverse rotation columns give basis.
    // Forward on floor: -Z of camera projected on XZ.
    Vec3 fwd{-head_view.m[2], 0.f, -head_view.m[10]};
    Vec3 right{head_view.m[0], 0.f, head_view.m[8]};
    // head_view is world-to-view; rows/cols: easier use inverse of upper 3x3
    // view_from_pose stores R^T in upper 3x3, so camera forward in world is
    // -third row of R^T = - (m[2], m[6], m[10]) ... use look direction:
    fwd = Vec3{-head_view.m[2], 0.f, -head_view.m[6]};
    // Actually for column-major view matrix V = [R^T | -R^T t], the camera
    // axes in world are rows of R^T (= columns of R):
    // right = (m[0], m[4], m[8]), up = (m[1], m[5], m[9]), fwd = -(m[2], m[6], m[10])
    right = Vec3{head_view.m[0], 0.f, head_view.m[8]};
    fwd = Vec3{-head_view.m[2], 0.f, -head_view.m[10]};
    right = right.normalized();
    fwd = fwd.normalized();
    const float speed = 3.0f * dt;
    st.locomotion += fwd * (move_y * speed) + right * (move_x * speed);
  }

  (void)scene;
}

bool create_session(OpenXRState& st, SDL_Window* window) {
  XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};
  sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XrResult r = xrGetSystem(st.instance, &sys, &st.system_id);
  if (!xr_check(st.instance, r, "xrGetSystem"))
    return false;

  // Graphics requirements
  PFN_xrGetOpenGLGraphicsRequirementsKHR pfn = nullptr;
  r = xrGetInstanceProcAddr(st.instance, "xrGetOpenGLGraphicsRequirementsKHR",
                            reinterpret_cast<PFN_xrVoidFunction*>(&pfn));
  if (!xr_check(st.instance, r, "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)"))
    return false;
  XrGraphicsRequirementsOpenGLKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  r = pfn(st.instance, st.system_id, &reqs);
  if (!xr_check(st.instance, r, "xrGetOpenGLGraphicsRequirementsKHR"))
    return false;
  DAYDRYM_LOGI("OpenXR OpenGL requirements: min %u.%u max %u.%u",
               XR_VERSION_MAJOR(reqs.minApiVersionSupported),
               XR_VERSION_MINOR(reqs.minApiVersionSupported),
               XR_VERSION_MAJOR(reqs.maxApiVersionSupported),
               XR_VERSION_MINOR(reqs.maxApiVersionSupported));

  if (!fill_glx_binding(window, &st.graphics_binding))
    return false;

  XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
  sci.next = &st.graphics_binding;
  sci.systemId = st.system_id;
  r = xrCreateSession(st.instance, &sci, &st.session);
  if (!xr_check(st.instance, r, "xrCreateSession"))
    return false;

  // Prefer STAGE, fall back to LOCAL
  XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  rsci.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};
  rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
  r = xrCreateReferenceSpace(st.session, &rsci, &st.ref_space);
  if (XR_FAILED(r)) {
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    r = xrCreateReferenceSpace(st.session, &rsci, &st.ref_space);
    if (!xr_check(st.instance, r, "xrCreateReferenceSpace(LOCAL)"))
      return false;
    DAYDRYM_LOGI("OpenXR reference space: LOCAL");
  } else {
    DAYDRYM_LOGI("OpenXR reference space: STAGE");
  }
  return true;
}

int64_t pick_swapchain_format(XrSession session) {
  uint32_t count = 0;
  xrEnumerateSwapchainFormats(session, 0, &count, nullptr);
  std::vector<int64_t> formats(count);
  xrEnumerateSwapchainFormats(session, count, &count, formats.data());

  // Prefer linear RGBA8; SRGB is fine too
  const int64_t preferred[] = {
    GL_RGBA8,
    GL_SRGB8_ALPHA8,
    GL_RGB10_A2,
  };
  for (int64_t want : preferred) {
    for (int64_t f : formats) {
      if (f == want) return f;
    }
  }
  return formats.empty() ? GL_RGBA8 : formats[0];
}

bool create_swapchains(OpenXRState& st) {
  uint32_t view_count = 0;
  XrResult r = xrEnumerateViewConfigurationViews(
      st.instance, st.system_id, st.view_type, 0, &view_count, nullptr);
  if (!xr_check(st.instance, r, "xrEnumerateViewConfigurationViews(count)"))
    return false;

  st.config_views.assign(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  r = xrEnumerateViewConfigurationViews(
      st.instance, st.system_id, st.view_type, view_count, &view_count,
      st.config_views.data());
  if (!xr_check(st.instance, r, "xrEnumerateViewConfigurationViews"))
    return false;

  st.views.assign(view_count, {XR_TYPE_VIEW});
  st.proj_views.resize(view_count);
  st.swapchains.resize(view_count);
  st.eye_fbos.resize(view_count, 0);

  const int64_t format = pick_swapchain_format(st.session);
  DAYDRYM_LOGI("OpenXR swapchain format: 0x%llx, views: %u",
               static_cast<unsigned long long>(format), view_count);

  int max_w = 0, max_h = 0;
  for (uint32_t i = 0; i < view_count; ++i) {
    const auto& cv = st.config_views[i];
    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                     XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    sci.format = format;
    sci.sampleCount = 1;
    sci.width = cv.recommendedImageRectWidth;
    sci.height = cv.recommendedImageRectHeight;
    sci.faceCount = 1;
    sci.arraySize = 1;
    sci.mipCount = 1;

    Swapchain& sc = st.swapchains[i];
    sc.width = static_cast<int32_t>(sci.width);
    sc.height = static_cast<int32_t>(sci.height);
    max_w = std::max(max_w, sc.width);
    max_h = std::max(max_h, sc.height);

    r = xrCreateSwapchain(st.session, &sci, &sc.handle);
    if (!xr_check(st.instance, r, "xrCreateSwapchain"))
      return false;

    uint32_t img_count = 0;
    xrEnumerateSwapchainImages(sc.handle, 0, &img_count, nullptr);
    sc.images.resize(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    r = xrEnumerateSwapchainImages(
        sc.handle, img_count, &img_count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data()));
    if (!xr_check(st.instance, r, "xrEnumerateSwapchainImages"))
      return false;

    DAYDRYM_LOGI("  view %u: %dx%d, %u images", i, sc.width, sc.height, img_count);

    glGenFramebuffers(1, &st.eye_fbos[i]);
  }

  // Shared depth RBO large enough for any eye
  glGenRenderbuffers(1, &st.depth_rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, st.depth_rbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, max_w, max_h);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  st.depth_w = max_w;
  st.depth_h = max_h;
  return true;
}

void destroy_gl_resources(OpenXRState& st) {
  for (GLuint fbo : st.eye_fbos) {
    if (fbo) glDeleteFramebuffers(1, &fbo);
  }
  st.eye_fbos.clear();
  if (st.depth_rbo) {
    glDeleteRenderbuffers(1, &st.depth_rbo);
    st.depth_rbo = 0;
  }
}

void destroy_openxr(OpenXRState& st) {
  destroy_gl_resources(st);
  for (auto& sc : st.swapchains) {
    if (sc.handle != XR_NULL_HANDLE)
      xrDestroySwapchain(sc.handle);
    sc.handle = XR_NULL_HANDLE;
  }
  st.swapchains.clear();
  for (int h = 0; h < kHandCount; ++h) {
    if (st.aim_space[h] != XR_NULL_HANDLE) {
      xrDestroySpace(st.aim_space[h]);
      st.aim_space[h] = XR_NULL_HANDLE;
    }
    if (st.grip_space[h] != XR_NULL_HANDLE) {
      xrDestroySpace(st.grip_space[h]);
      st.grip_space[h] = XR_NULL_HANDLE;
    }
  }
  if (st.action_set != XR_NULL_HANDLE) {
    xrDestroyActionSet(st.action_set);
    st.action_set = XR_NULL_HANDLE;
  }
  if (st.ref_space != XR_NULL_HANDLE) {
    xrDestroySpace(st.ref_space);
    st.ref_space = XR_NULL_HANDLE;
  }
  if (st.session != XR_NULL_HANDLE) {
    xrDestroySession(st.session);
    st.session = XR_NULL_HANDLE;
  }
  if (st.instance != XR_NULL_HANDLE) {
    xrDestroyInstance(st.instance);
    st.instance = XR_NULL_HANDLE;
  }
}

void poll_events(OpenXRState& st) {
  XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
  while (true) {
    XrResult r = xrPollEvent(st.instance, &ev);
    if (r == XR_EVENT_UNAVAILABLE)
      break;
    if (XR_FAILED(r)) {
      DAYDRYM_LOGE("xrPollEvent: %s", xr_str(st.instance, r));
      break;
    }
    switch (ev.type) {
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
        st.session_state = s->state;
        DAYDRYM_LOGI("OpenXR session state: %d", static_cast<int>(s->state));
        switch (s->state) {
          case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
            bi.primaryViewConfigurationType = st.view_type;
            if (xr_check(st.instance, xrBeginSession(st.session, &bi), "xrBeginSession")) {
              st.session_running = true;
              attach_actions(st);
              // Profiles often still null until FOCUSED; dump anyway.
              log_input_diagnostics(st);
            }
            break;
          }
          case XR_SESSION_STATE_FOCUSED:
            log_input_diagnostics(st);
            break;
          case XR_SESSION_STATE_STOPPING:
            xrEndSession(st.session);
            st.session_running = false;
            break;
          case XR_SESSION_STATE_EXITING:
          case XR_SESSION_STATE_LOSS_PENDING:
            st.exit_requested = true;
            break;
          default:
            break;
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
        DAYDRYM_LOGI("OpenXR: interaction profile changed");
        log_input_diagnostics(st);
        break;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
        st.exit_requested = true;
        break;
      default:
        break;
    }
    ev = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
  }
}

bool render_frame(OpenXRState& st, Renderer& renderer, Scene& scene) {
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};
  XrResult r = xrWaitFrame(st.session, &wait, &frame_state);
  if (!xr_check(st.instance, r, "xrWaitFrame"))
    return false;

  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  r = xrBeginFrame(st.session, &begin);
  if (!xr_check(st.instance, r, "xrBeginFrame"))
    return false;

  std::vector<XrCompositionLayerBaseHeader*> layers;
  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  layer.space = st.ref_space;

  if (frame_state.shouldRender) {
    XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
    vli.viewConfigurationType = st.view_type;
    vli.displayTime = frame_state.predictedDisplayTime;
    vli.space = st.ref_space;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t view_count = static_cast<uint32_t>(st.views.size());
    r = xrLocateViews(st.session, &vli, &vs, view_count, &view_count, st.views.data());
    if (!xr_check(st.instance, r, "xrLocateViews")) {
      xrEndFrame(st.session, nullptr);
      return false;
    }

    // Head position for specular (average of eyes, or left)
    Vec3 head_pos{0.f, 1.6f, 0.f};
    if (view_count > 0) {
      head_pos = {
          st.views[0].pose.position.x,
          st.views[0].pose.position.y,
          st.views[0].pose.position.z,
      };
      if (view_count > 1) {
        head_pos.x = 0.5f * (head_pos.x + st.views[1].pose.position.x);
        head_pos.y = 0.5f * (head_pos.y + st.views[1].pose.position.y);
        head_pos.z = 0.5f * (head_pos.z + st.views[1].pose.position.z);
      }
    }

    // Head view (left eye) for locomotion basis; include current locomotion so
    // stick movement stays aligned with the played-offset avatar.
    const XrPosef& hp = st.views[0].pose;
    Mat4 head_view = Mat4::view_from_pose(
        hp.position.x, hp.position.y, hp.position.z,
        hp.orientation.x, hp.orientation.y, hp.orientation.z, hp.orientation.w);
    head_view = head_view * Mat4::translate(
        {-st.locomotion.x, -st.locomotion.y, -st.locomotion.z});

    // dt is not in render_frame — use a small fixed step for stick speed; the
    // main loop also advances scene time. Approximate via display period if set.
    float dt = 1.f / 72.f;
    if (frame_state.predictedDisplayPeriod > 0)
      dt = static_cast<float>(frame_state.predictedDisplayPeriod) * 1e-9f;
    if (dt < 0.001f || dt > 0.1f)
      dt = 1.f / 72.f;

    sync_actions(st, frame_state.predictedDisplayTime, dt, scene, head_view);

    // One shared shadow pass
    renderer.begin_shadow_pass(scene.light, {0.f, 1.f, -2.f});
    scene.draw_shadow(renderer);
    renderer.end_shadow_pass();

    for (uint32_t i = 0; i < view_count; ++i) {
      Swapchain& sc = st.swapchains[i];
      XrSwapchainImageAcquireInfo acq{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      uint32_t img_index = 0;
      r = xrAcquireSwapchainImage(sc.handle, &acq, &img_index);
      if (!xr_check(st.instance, r, "xrAcquireSwapchainImage"))
        return false;

      XrSwapchainImageWaitInfo wait_img{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      wait_img.timeout = XR_INFINITE_DURATION;
      r = xrWaitSwapchainImage(sc.handle, &wait_img);
      if (!xr_check(st.instance, r, "xrWaitSwapchainImage"))
        return false;

      const GLuint color_tex = sc.images[img_index].image;
      glBindFramebuffer(GL_FRAMEBUFFER, st.eye_fbos[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, color_tex, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, st.depth_rbo);
      GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
        DAYDRYM_LOGE("Eye FBO incomplete: 0x%x", status);
      }

      glViewport(0, 0, sc.width, sc.height);
      glClearColor(0.45f, 0.65f, 0.95f, 1.f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      renderer.setup_lit_pass(scene.light, {
          head_pos.x + st.locomotion.x,
          head_pos.y + st.locomotion.y,
          head_pos.z + st.locomotion.z});

      const XrPosef& pose = st.views[i].pose;
      Mat4 view = Mat4::view_from_pose(
          pose.position.x, pose.position.y, pose.position.z,
          pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);
      // Locomotion: shift the world under the tracked HMD
      view = view * Mat4::translate(
          {-st.locomotion.x, -st.locomotion.y, -st.locomotion.z});
      const XrFovf& fov = st.views[i].fov;
      Mat4 proj = Mat4::perspective_fov(
          fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown, 0.1f, 100.f);

      scene.draw(renderer, view, proj);
      for (int h = 0; h < kHandCount; ++h) {
        const HandInput& hand = st.hands[h];
        if (!hand.aim_active)
          continue;
        scene.draw_controller(renderer, view, proj, hand.aim_model, hand.ray_length,
                              hand.buttons);
        if (hand.floor_hit)
          scene.draw_marker(renderer, view, proj, hand.floor_point);
      }

      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      XrSwapchainImageReleaseInfo rel{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(sc.handle, &rel);

      st.proj_views[i] = XrCompositionLayerProjectionView{
          XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
      st.proj_views[i].pose = st.views[i].pose;
      st.proj_views[i].fov = st.views[i].fov;
      st.proj_views[i].subImage.swapchain = sc.handle;
      st.proj_views[i].subImage.imageRect.offset = {0, 0};
      st.proj_views[i].subImage.imageRect.extent = {sc.width, sc.height};
      st.proj_views[i].subImage.imageArrayIndex = 0;
    }

    layer.viewCount = view_count;
    layer.views = st.proj_views.data();
    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
  }

  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
  end.displayTime = frame_state.predictedDisplayTime;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = static_cast<uint32_t>(layers.size());
  end.layers = layers.empty() ? nullptr : layers.data();
  r = xrEndFrame(st.session, &end);
  return xr_check(st.instance, r, "xrEndFrame");
}

}  // namespace

int run_openxr(SDL_Window* window, SDL_GLContext gl_ctx) {
  (void)gl_ctx;
  OpenXRState st;

  if (!create_instance(st)) {
    destroy_openxr(st);
    return 1;
  }
  if (!create_session(st, window)) {
    destroy_openxr(st);
    return 1;
  }
  if (!create_actions(st)) {
    destroy_openxr(st);
    return 1;
  }
  if (!create_swapchains(st)) {
    destroy_openxr(st);
    return 1;
  }

  // Renderer sized to max eye resolution (shadow map independent)
  int max_w = st.depth_w, max_h = st.depth_h;
  Renderer renderer;
  if (!renderer.init(max_w > 0 ? max_w : 1024, max_h > 0 ? max_h : 1024)) {
    DAYDRYM_LOGE("Renderer init failed");
    destroy_openxr(st);
    return 1;
  }

  Scene scene;
  // Place the scene relative to stage origin: house in front of user
  // Stage origin is typically floor center; keep default scene layout.
  DAYDRYM_LOGI("OpenXR session ready");
  DAYDRYM_LOGI("Controls: thumbstick/trackpad move, trigger/A select teleport, menu day/night");
  DAYDRYM_LOGI("Press Q or close window to quit");

  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  bool running = true;

  while (running && !st.exit_requested) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        running = false;
      else if (e.type == SDL_KEYDOWN &&
               (e.key.keysym.sym == SDLK_q || e.key.keysym.sym == SDLK_ESCAPE))
        running = false;
    }

    poll_events(st);
    if (st.exit_requested)
      break;

    Uint64 now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>((now - prev) / freq);
    prev = now;
    if (dt > 0.1f) dt = 0.1f;
    scene.update(dt);

    if (st.session_running) {
      if (!render_frame(st, renderer, scene)) {
        // keep trying unless session died
      }
    } else {
      SDL_Delay(10);
    }

    // Keep the desktop window responsive (optional mirror not drawn yet)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 1, 1);
    glClearColor(0.1f, 0.1f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window);
  }

  if (st.session_running) {
    xrRequestExitSession(st.session);
    // Drain a few events
    for (int i = 0; i < 30; ++i) {
      poll_events(st);
      if (!st.session_running) break;
      SDL_Delay(16);
    }
  }

  destroy_openxr(st);
  DAYDRYM_LOGI("OpenXR shutdown complete");
  return 0;
}

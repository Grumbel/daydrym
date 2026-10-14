// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "openxr_app.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
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
            if (xr_check(st.instance, xrBeginSession(st.session, &bi), "xrBeginSession"))
              st.session_running = true;
            break;
          }
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

      renderer.setup_lit_pass(scene.light, head_pos);

      const XrPosef& pose = st.views[i].pose;
      Mat4 view = Mat4::view_from_pose(
          pose.position.x, pose.position.y, pose.position.z,
          pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);
      const XrFovf& fov = st.views[i].fov;
      Mat4 proj = Mat4::perspective_fov(
          fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown, 0.1f, 100.f);

      scene.draw(renderer, view, proj);

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
  DAYDRYM_LOGI("OpenXR session ready — put on headset / start monado-service");
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

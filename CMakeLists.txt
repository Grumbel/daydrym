# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
cmake_minimum_required(VERSION 3.16)
project(daydrym VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_GLES "Force OpenGL ES instead of desktop GL" OFF)
option(DAYDRYM_OPENXR "Build optional OpenXR (--openxr) support on Linux desktop" ON)

set(DAYDRYM_COMMON_SOURCES
  src/renderer.cpp
  src/scene.cpp
  src/math.cpp
)

if(ANDROID)
  # Android build: Google VR (GVR) NDK, driven from Java (DaydrymActivity).
  # GVR_PREFIX must contain include/vr/gvr/capi/include/*.h and lib/libgvr.so.
  if(NOT DEFINED GVR_PREFIX)
    message(FATAL_ERROR "Set -DGVR_PREFIX to the extracted GVR NDK")
  endif()

  add_library(daydrym SHARED ${DAYDRYM_COMMON_SOURCES} src/gvr_app.cpp)
  target_include_directories(daydrym PRIVATE include ${GVR_PREFIX}/include)
  target_compile_definitions(daydrym PRIVATE USE_GLES=1)

  add_library(gvr SHARED IMPORTED)
  set_target_properties(gvr PROPERTIES IMPORTED_LOCATION ${GVR_PREFIX}/lib/libgvr.so)
  target_link_libraries(daydrym PRIVATE gvr android log GLESv3)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  find_package(OpenGL REQUIRED)

  set(DAYDRYM_DESKTOP_SOURCES src/main.cpp ${DAYDRYM_COMMON_SOURCES})
  set(DAYDRYM_OPENXR_ENABLED OFF)

  if(DAYDRYM_OPENXR AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(OpenXR QUIET)
    if(NOT OpenXR_FOUND)
      # Fallback: pkg-config (openxr.pc from openxr-loader)
      pkg_check_modules(OPENXR IMPORTED_TARGET openxr)
      if(OPENXR_FOUND)
        set(OpenXR_FOUND TRUE)
      endif()
    endif()
    find_package(X11 REQUIRED)
    if(OpenXR_FOUND)
      set(DAYDRYM_OPENXR_ENABLED ON)
      list(APPEND DAYDRYM_DESKTOP_SOURCES src/openxr_app.cpp)
      message(STATUS "OpenXR support: enabled")
    else()
      message(STATUS "OpenXR support: disabled (OpenXR loader not found)")
    endif()
  endif()

  add_executable(daydrym ${DAYDRYM_DESKTOP_SOURCES})
  target_include_directories(daydrym PRIVATE include ${SDL2_INCLUDE_DIRS})
  target_compile_options(daydrym PRIVATE ${SDL2_CFLAGS_OTHER})
  if(USE_GLES)
    target_compile_definitions(daydrym PRIVATE USE_GLES=1)
  endif()
  target_link_libraries(daydrym PRIVATE ${SDL2_LIBRARIES} OpenGL::GL)

  if(DAYDRYM_OPENXR_ENABLED)
    target_compile_definitions(daydrym PRIVATE DAYDRYM_OPENXR=1)
    if(TARGET OpenXR::openxr_loader)
      target_link_libraries(daydrym PRIVATE OpenXR::openxr_loader)
    elseif(TARGET PkgConfig::OPENXR)
      target_link_libraries(daydrym PRIVATE PkgConfig::OPENXR)
    else()
      target_include_directories(daydrym PRIVATE ${OPENXR_INCLUDE_DIRS})
      target_link_libraries(daydrym PRIVATE ${OPENXR_LIBRARIES})
    endif()
    target_link_libraries(daydrym PRIVATE X11::X11 OpenGL::GL)
    # GLX is part of libGL on most systems; ensure X11 headers available
  endif()

  include(GNUInstallDirs)
  install(TARGETS daydrym RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES data/daydrym.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/daydrym.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES data/daydrym.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

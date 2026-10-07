# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ingo Ruhnke <grumbel@gmail.com>
cmake_minimum_required(VERSION 3.16)
project(daydrym VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_GLES "Force OpenGL ES instead of desktop GL" OFF)

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

  add_executable(daydrym src/main.cpp ${DAYDRYM_COMMON_SOURCES})
  target_include_directories(daydrym PRIVATE include ${SDL2_INCLUDE_DIRS})
  target_compile_options(daydrym PRIVATE ${SDL2_CFLAGS_OTHER})
  if(USE_GLES)
    target_compile_definitions(daydrym PRIVATE USE_GLES=1)
  endif()
  target_link_libraries(daydrym PRIVATE ${SDL2_LIBRARIES} OpenGL::GL)
  install(TARGETS daydrym RUNTIME DESTINATION bin)
endif()

cmake_minimum_required(VERSION 3.16)
project(daydrym VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_GLES "Force OpenGL ES instead of desktop GL" OFF)

set(DAYDRYM_SOURCES
  src/main.cpp
  src/renderer.cpp
  src/scene.cpp
  src/math.cpp
)

if(ANDROID)
  add_library(main SHARED ${DAYDRYM_SOURCES})
  target_include_directories(main PRIVATE include)

  # Prefer an explicit SDL2 prefix from the Android packaging derivation
  if(DEFINED SDL2_ANDROID_PREFIX)
    target_include_directories(main PRIVATE
      ${SDL2_ANDROID_PREFIX}/include
      ${SDL2_ANDROID_PREFIX}/include/SDL2)
    target_link_directories(main PRIVATE ${SDL2_ANDROID_PREFIX}/lib)
    target_link_libraries(main PRIVATE SDL2 android log EGL GLESv3)
  else()
    find_library(SDL2_LIB SDL2 REQUIRED)
    find_library(LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(EGL_LIB EGL)
    find_library(GLESV3_LIB GLESv3)
    target_link_libraries(main PRIVATE
      ${SDL2_LIB} ${ANDROID_LIB} ${LOG_LIB} ${EGL_LIB} ${GLESV3_LIB})
  endif()
  target_compile_definitions(main PRIVATE USE_GLES=1)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  find_package(OpenGL REQUIRED)

  add_executable(daydrym ${DAYDRYM_SOURCES})
  target_include_directories(daydrym PRIVATE include ${SDL2_INCLUDE_DIRS})
  target_compile_options(daydrym PRIVATE ${SDL2_CFLAGS_OTHER})
  if(USE_GLES)
    target_compile_definitions(daydrym PRIVATE USE_GLES=1)
  endif()
  target_link_libraries(daydrym PRIVATE ${SDL2_LIBRARIES} OpenGL::GL)
  install(TARGETS daydrym RUNTIME DESTINATION bin)
endif()

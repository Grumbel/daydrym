cmake_minimum_required(VERSION 3.16)
project(gles-vr-hello VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_GLES "Force OpenGL ES instead of desktop GL" OFF)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(gles-vr-hello
  src/main.cpp
  src/renderer.cpp
  src/scene.cpp
  src/math.cpp
)

target_include_directories(gles-vr-hello PRIVATE include ${SDL2_INCLUDE_DIRS})

if(USE_GLES OR ANDROID)
  target_compile_definitions(gles-vr-hello PRIVATE USE_GLES=1)
  # On Android / pure GLES builds the headers come from the NDK / EGL
  target_link_libraries(gles-vr-hello PRIVATE SDL2::SDL2 OpenGL::GL)
else()
  target_link_libraries(gles-vr-hello PRIVATE SDL2::SDL2 OpenGL::GL)
endif()

if(ANDROID)
  # Shared library for the APK
  set_target_properties(gles-vr-hello PROPERTIES
    OUTPUT_NAME "main"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}"
  )
endif()

install(TARGETS gles-vr-hello RUNTIME DESTINATION bin)

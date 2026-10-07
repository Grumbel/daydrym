cmake_minimum_required(VERSION 3.16)
project(daydrym VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_GLES "Force OpenGL ES instead of desktop GL" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
find_package(OpenGL REQUIRED)

add_executable(daydrym
  src/main.cpp
  src/renderer.cpp
  src/scene.cpp
  src/math.cpp
)

target_include_directories(daydrym PRIVATE include ${SDL2_INCLUDE_DIRS})
target_compile_options(daydrym PRIVATE ${SDL2_CFLAGS_OTHER})

if(USE_GLES OR ANDROID)
  target_compile_definitions(daydrym PRIVATE USE_GLES=1)
endif()

target_link_libraries(daydrym PRIVATE ${SDL2_LIBRARIES} OpenGL::GL)

if(ANDROID)
  set_target_properties(daydrym PROPERTIES
    OUTPUT_NAME "main"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}"
  )
endif()

install(TARGETS daydrym RUNTIME DESTINATION bin)

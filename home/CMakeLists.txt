cmake_minimum_required(VERSION 3.16)
project(lgtv_home CXX)

# Cross-builds for the TV use the buildroot-nc4 webOS toolchain (see build.py);
# host builds use the system SDL2 and GLES for a desktop window.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(GLES2 IMPORTED_TARGET glesv2)
if(NOT GLES2_FOUND)  # the webOS sysroot has libGLESv2 but no glesv2.pc
  find_library(GLESV2_LIBRARY GLESv2 REQUIRED)
  add_library(PkgConfig::GLES2 UNKNOWN IMPORTED)
  set_target_properties(PkgConfig::GLES2 PROPERTIES IMPORTED_LOCATION ${GLESV2_LIBRARY})
endif()

add_executable(home
  src/apps.cpp
  src/gfx.cpp
  src/home.cpp
  src/main.cpp
  src/wallpaper.cpp
)
target_compile_options(home PRIVATE -Wall -Wextra)
target_link_libraries(home PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::SDL2_IMAGE PkgConfig::GLES2)
if(CMAKE_CROSSCOMPILING)
  target_compile_definitions(home PRIVATE HOME_WEBOS)
else()
  target_compile_definitions(home PRIVATE HOME_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

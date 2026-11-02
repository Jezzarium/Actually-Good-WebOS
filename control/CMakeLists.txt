cmake_minimum_required(VERSION 3.16)
project(lgtv_control CXX)

# Cross-builds for the TV use the buildroot-nc4 webOS toolchain (see
# common/native_build.py). TV only: it needs LG's SDL and the Luna bus.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "The Control Center only builds for the TV: it needs LG's SDL and the Luna bus.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
# The webOS sysroot has libGLESv2 but no glesv2.pc.
find_library(GLESV2_LIBRARY GLESv2 REQUIRED)
add_library(PkgConfig::GLES2 UNKNOWN IMPORTED)
set_target_properties(PkgConfig::GLES2 PROPERTIES IMPORTED_LOCATION ${GLESV2_LIBRARY})

include(${CMAKE_SOURCE_DIR}/../common/common.cmake)

add_executable(control
  src/bluetooth.cpp
  src/main.cpp
  src/overlay.cpp
  src/panel.cpp
  src/tv.cpp
)
target_compile_options(control PRIVATE -Wall -Wextra)
target_link_libraries(control PRIVATE lgtv_common PkgConfig::SDL2_IMAGE dl)
# The toolchain's GCC is newer than the TV's libstdc++; carry our own.
target_link_options(control PRIVATE -static-libstdc++ -static-libgcc)

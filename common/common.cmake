# Code shared by the native apps (home/, control/): drawing, the Luna bus,
# logging, springs, the status pill, inputs and the state the two apps share.
# include() it after the SDL2, SDL2_ttf, json-c and GLES2 imported targets exist.
set(LGTV_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
add_library(lgtv_common STATIC
  ${LGTV_COMMON_DIR}/gfx.cpp
  ${LGTV_COMMON_DIR}/inputs.cpp
  ${LGTV_COMMON_DIR}/luna.cpp
  ${LGTV_COMMON_DIR}/pill.cpp
  ${LGTV_COMMON_DIR}/shell_state.cpp
)
target_include_directories(lgtv_common PUBLIC ${LGTV_COMMON_DIR})
target_compile_options(lgtv_common PRIVATE -Wall -Wextra)
target_link_libraries(lgtv_common PUBLIC PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::JSONC PkgConfig::GLES2)
if(CMAKE_CROSSCOMPILING)
  # The sysroot has no .pc files for the Luna bus; link it directly.
  pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
  target_link_libraries(lgtv_common PUBLIC luna-service2 PkgConfig::GLIB)
  target_compile_definitions(lgtv_common PUBLIC LGTV_WEBOS)
endif()

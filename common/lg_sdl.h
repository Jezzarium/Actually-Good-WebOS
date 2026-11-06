#pragma once

// What LG's SDL (the webOS port of 2.0.14) keeps private and both apps need:
// the window's wl_webos_shell_surface, to set window properties that
// surface-manager reads. It is found at offsets taken by disassembling the
// copy on the TV (see control/README.md), and only read once the instructions
// that use those offsets are checked to be the expected ones.

#include <SDL.h>

#include <cstddef>

namespace lg_sdl {

// An instruction in LG's SDL: the exported function it is in, its offset from
// that function's start, and its bytes in the build the offsets were taken from.
struct Instruction {
    const char *function;
    size_t offset;
    unsigned char bytes[4];
    size_t size;
};

// Whether this copy of SDL has that instruction there. Reading pointers at
// these offsets in any other build could crash, so nothing is read unless this
// holds for the instructions that use them.
bool matches(const Instruction &ins);

// Sets a property on the window's wl_webos_shell_surface: _WEBOS_WINDOW_TYPE
// (only before the first frame), windowPosX, needFocus, or one of our own for
// tv/Transitions.qml. False if the shell surface isn't where this build of
// SDL is expected to keep it (a desktop build, or another firmware).
bool set_property(SDL_Window *window, const char *name, const char *value);

}  // namespace lg_sdl

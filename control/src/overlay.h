#pragma once

// What LG's SDL (the webOS port of 2.0.14) can't do, beyond window properties
// (common/lg_sdl.h, which make the window an overlay, move it and give the
// remote back): tell the app it was launched again, and let the pointer
// through. The relaunch hook reaches into the library's private data at an
// offset found by disassembling the copy on the TV (see control/README.md),
// and checks what it finds before using it.

#include <SDL.h>

namespace overlay {

// Wraps the function LG's SDL calls when SAM relaunches the app (it sends no
// event). False if the slot doesn't hold a function of LG's SDL.
bool hook_relaunch();
// True once for each relaunch since the last call. Relaunches arrive on SDL's
// Luna thread.
bool take_relaunch();

// Where the window takes the Magic Remote's pointer (its Wayland input
// region), in window pixels; elsewhere the pointer reaches the app below. An
// empty rectangle: nowhere. Through SDL's public window info, so it needs no
// offsets. False if it can't be set.
bool set_input_region(SDL_Window *window, int x, int y, int w, int h);

}  // namespace overlay

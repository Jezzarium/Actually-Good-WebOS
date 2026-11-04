#pragma once

// What LG's SDL (the webOS port of 2.0.14) can't do: make a window an overlay
// over the app in front, move it and give the remote back, tell the app it
// was launched again, and let the pointer through. Most of these reach into
// the library's private data at offsets found by disassembling the copy on the
// TV (see control/README.md), and check what they find before using it.

#include <SDL.h>

namespace overlay {

// Sets a property on the window's wl_webos_shell_surface: _WEBOS_WINDOW_TYPE
// (only before the first frame), windowPosX, needFocus... False if the shell
// surface isn't where this build of SDL is expected to keep it.
bool set_property(SDL_Window *window, const char *name, const char *value);

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

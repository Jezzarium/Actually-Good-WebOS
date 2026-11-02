#pragma once

// The wallpaper and its blurred copy. Glass panels and the scrolled-down page
// show the blurred copy, made once here so nothing is blurred per frame.

#include "gfx.h"

#include <string>

struct Wallpaper {
    Texture sharp;
    Texture blurred;
};

// An image file scaled to cover w x h; a generated gradient if it can't be loaded.
// The blurred copy is also saved to backdrop_path, if given, for the Control
// Center's glass (an overlay can't see what's below it).
Wallpaper load_wallpaper(const std::string &path, int w, int h, const std::string &backdrop_path = "");

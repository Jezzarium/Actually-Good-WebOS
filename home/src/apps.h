#pragma once

// The apps on the home screen. A fixed placeholder list until the app list
// comes from the TV (applicationManager/listLaunchPoints).

#include <cstdint>
#include <string>
#include <vector>

struct App {
    std::string id;
    std::string title;
    uint32_t color;  // tile colour, 0xRRGGBB (LG's iconColor once real)
};

std::vector<App> placeholder_apps();

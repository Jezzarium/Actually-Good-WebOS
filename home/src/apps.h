#pragma once

// The apps on the home screen, from applicationManager/listLaunchPoints, in
// the order LG Home keeps them. Desktop builds use a placeholder list.

#include <json-c/json.h>

#include <cstdint>
#include <string>
#include <vector>

struct App {
    std::string id;
    std::string title;
    std::string icon;  // path to the sharpest icon the app provides; may be empty
    uint32_t color;    // tile background, 0xRRGGBB (LG's iconColor)
};

// From a listLaunchPoints reply. Leaves out hidden entries, inputs (they live
// behind the Inputs button) and this app itself.
std::vector<App> apps_from_launch_points(json_object *reply, const std::string &self_id);

std::vector<App> placeholder_apps();

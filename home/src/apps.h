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

// The last list LG gave, kept on disk so the next start can draw the real
// home screen in its first frame, before the app manager answers.
std::vector<App> load_apps(const std::string &path);
void save_apps(const std::string &path, const std::vector<App> &apps);
bool operator==(const App &a, const App &b);

// An external input (HDMI), from com.webos.service.eim/getAllInputStatus.
struct Input {
    std::string label;   // the device's name over CEC if it has one, else "HDMI 2"
    std::string port;    // "HDMI 2"
    std::string app_id;  // launching it switches to the input
    bool connected;
};

std::vector<Input> inputs_from_status(json_object *reply);
std::vector<Input> placeholder_inputs();

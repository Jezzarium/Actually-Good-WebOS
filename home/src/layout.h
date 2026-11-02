#pragma once

// The user's arrangement of the home screen (app order, hidden apps) and its
// settings, saved as JSON. Apps the layout doesn't know yet (newly installed)
// go at the end in LG's order.

#include "apps.h"

#include <string>
#include <vector>

struct Layout {
    std::vector<std::string> order;   // app ids, dock first
    std::vector<std::string> hidden;  // app ids
    std::string wallpaper;            // file name; empty: the bundled default
    std::string night_wallpaper;      // file name; empty: the day wallpaper, dimmed
    std::string appearance = "auto";  // "auto" (night 7 PM to 7 AM), "day" or "night"

    bool load(const std::string &path);
    bool save(const std::string &path) const;
    // The visible apps in layout order.
    std::vector<App> apply(const std::vector<App> &apps) const;
    bool is_hidden(const std::string &id) const;
};

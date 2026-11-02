#include "apps.h"

#include "luna.h"

#include <cstdlib>

namespace {

constexpr uint32_t kDefaultColor = 0x2b2f3a;

uint32_t parse_color(const std::string &hex) {
    if (hex.size() != 7 || hex[0] != '#') return kDefaultColor;
    char *end;
    unsigned long v = std::strtoul(hex.c_str() + 1, &end, 16);
    return *end ? kDefaultColor : uint32_t(v);
}

}  // namespace

std::vector<App> apps_from_launch_points(json_object *reply, const std::string &self_id) {
    std::vector<App> apps;
    json_object *list;
    if (!json_object_object_get_ex(reply, "launchPoints", &list) || !json_object_is_type(list, json_type_array)) {
        return apps;
    }
    for (size_t i = 0; i < json_object_array_length(list); i++) {
        json_object *lp = json_object_array_get_idx(list, i);
        std::string id = get_string(lp, "id");
        if (id.empty() || id == self_id || get_bool(lp, "hidden")) continue;
        if (id.rfind("com.webos.app.hdmi", 0) == 0) continue;

        App app;
        app.id = id;
        app.title = get_string(lp, "title", id);
        app.icon = get_string(lp, "extraLargeIcon");
        if (app.icon.empty()) app.icon = get_string(lp, "largeIcon");
        if (app.icon.empty()) app.icon = get_string(lp, "icon");
        app.color = parse_color(get_string(lp, "iconColor"));
        apps.push_back(std::move(app));
    }
    return apps;
}

std::vector<App> placeholder_apps() {
    return {
        {"youtube.leanback.v4", "YouTube", "", 0xd92b2b},
        {"netflix", "Netflix", "", 0x141414},
        {"amazon", "Prime Video", "", 0x1a98ff},
        {"com.disney.disneyplus-prod", "Disney+", "", 0x0e2a6b},
        {"com.limelight.webos", "Moonlight", "", 0x4a4f63},
        {"spotify-beehive", "Spotify", "", 0x1db954},
        {"twitch.adamffdev.v1", "Twitch", "", 0x8045f0},
        {"com.webos.app.livetv", "Live TV", "", 0xc8324b},
        {"cdp-30", "Plex", "", 0xe5a00d},
        {"com.apple.appletv", "Apple TV", "", 0x1c1c1e},
        {"com.webos.app.browser", "Browser", "", 0x2f6fd6},
        {"crunchyroll", "Crunchyroll", "", 0xf47521},
        {"com.webos.app.music", "Music", "", 0xf2405e},
        {"com.webos.app.photovideo", "Photos", "", 0xf0b429},
        {"org.webosbrew.hbchannel", "Homebrew", "", 0x2b2f3a},
        {"com.github.k4zmu2a.space-cadet-pinball", "Pinball", "", 0x2e7d6b},
        {"com.halfbrick.fruitninja", "Fruit Ninja", "", 0x7cb342},
        {"com.webos.app.discovery", "Apps", "", 0x7e57c2},
        {"com.webos.app.settings", "Settings", "", 0x5f6673},
        {"com.webos.app.screensaver", "Gallery", "", 0x00897b},
    };
}

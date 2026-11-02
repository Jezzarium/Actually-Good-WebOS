#include "apps.h"

#include "log.h"
#include "luna.h"

#include <cstdio>
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

std::vector<App> load_apps(const std::string &path) {
    std::vector<App> apps;
    json_object *list = json_object_from_file(path.c_str());
    if (!list) return apps;
    if (json_object_is_type(list, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(list); i++) {
            json_object *a = json_object_array_get_idx(list, i);
            json_object *color;
            App app;
            app.id = get_string(a, "id");
            app.title = get_string(a, "title", app.id);
            app.icon = get_string(a, "icon");
            app.color = json_object_object_get_ex(a, "color", &color) ? uint32_t(json_object_get_int64(color))
                                                                       : kDefaultColor;
            if (!app.id.empty()) apps.push_back(std::move(app));
        }
    }
    json_object_put(list);
    return apps;
}

void save_apps(const std::string &path, const std::vector<App> &apps) {
    json_object *list = json_object_new_array();
    for (const App &app : apps) {
        json_object *a = json_object_new_object();
        json_object_object_add(a, "id", json_object_new_string(app.id.c_str()));
        json_object_object_add(a, "title", json_object_new_string(app.title.c_str()));
        json_object_object_add(a, "icon", json_object_new_string(app.icon.c_str()));
        json_object_object_add(a, "color", json_object_new_int64(app.color));
        json_object_array_add(list, a);
    }
    // Write beside it and rename, so a crash never leaves half a file.
    std::string tmp = path + ".tmp";
    if (json_object_to_file_ext(tmp.c_str(), list, JSON_C_TO_STRING_PRETTY) != 0 ||
        std::rename(tmp.c_str(), path.c_str()) != 0) {
        LOG("apps: can't save %s", path.c_str());
    }
    json_object_put(list);
}

bool operator==(const App &a, const App &b) {
    return a.id == b.id && a.title == b.title && a.icon == b.icon && a.color == b.color;
}

std::vector<Input> inputs_from_status(json_object *reply) {
    std::vector<Input> inputs;
    json_object *devices;
    if (!json_object_object_get_ex(reply, "devices", &devices) || !json_object_is_type(devices, json_type_array)) {
        return inputs;
    }
    for (size_t i = 0; i < json_object_array_length(devices); i++) {
        json_object *dev = json_object_array_get_idx(devices, i);
        Input input;
        input.app_id = get_string(dev, "appId");
        if (input.app_id.empty()) continue;
        // "HDMI_2" -> "HDMI 2"
        input.port = get_string(dev, "id");
        for (char &c : input.port) {
            if (c == '_') c = ' ';
        }
        input.label = get_string(dev, "label", input.port);
        input.connected = get_bool(dev, "connected");
        inputs.push_back(std::move(input));
    }
    return inputs;
}

std::vector<Input> placeholder_inputs() {
    return {
        {"HDMI 1", "HDMI 1", "com.webos.app.hdmi1", true},
        {"SR7008", "HDMI 2", "com.webos.app.hdmi2", true},
        {"HDMI 3", "HDMI 3", "com.webos.app.hdmi3", false},
    };
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

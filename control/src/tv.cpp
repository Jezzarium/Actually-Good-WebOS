#include "tv.h"

#include "log.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {

const std::string kSettings = "luna://com.webos.settingsservice/";
const std::string kSleepTimer = "luna://com.webos.service.tvReservationAgent/timer/sleep/";
const std::string kTurnOffScreen = "luna://com.webos.service.tvpower/power/turnOffScreen";
const std::string kLaunch = "luna://com.webos.applicationManager/launch";
const std::string kExec = "luna://org.webosbrew.hbchannel.service/exec";
// Values and learned routes from the last run, so the first frame shows real
// numbers. /tmp is the TV's own (the jail shares it), so it lasts until reboot.
const char *kCache = "/tmp/lgtv-control-cache.json";

constexpr unsigned kWriteInterval = 120;  // ms
// A value read back this soon after a local change is stale; don't apply it.
constexpr unsigned kTouchGuard = 1500;

// "cinema_zoom" -> "Cinema zoom", "mediumHigh" -> "Medium high"
std::string humanize(const std::string &v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        char c = v[i];
        if (c == '_') {
            out += ' ';
            continue;
        }
        bool upper_or_digit = std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c));
        if (i > 0 && upper_or_digit && std::islower(static_cast<unsigned char>(v[i - 1]))) out += ' ';
        out += char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::function<std::string(const std::string &)> from_map(std::map<std::string, std::string> names) {
    return [names](const std::string &v) {
        auto it = names.find(v);
        return it != names.end() ? it->second : humanize(v);
    };
}

std::string minutes(const std::string &v) { return v == "off" ? "Off" : v + " min"; }

// LG's own wording, shortened where it would not fit.
const std::map<std::string, std::string> kPictureModes = {
    {"vivid", "Vivid"}, {"normal", "Standard"}, {"standard", "Standard"}, {"external", "Standard"},
    {"eco", "Eco"}, {"cinema", "Cinema"}, {"cinemaBright", "Cinema Home"}, {"sports", "Sports"},
    {"game", "Game Optimizer"}, {"photo", "Photo"}, {"filmMaker", "Filmmaker Mode"},
    {"personalized", "Personalized"}, {"expert1", "Expert (Bright)"}, {"expert2", "Expert (Dark)"},
    {"effect", "HDR Effect"},
};

std::string picture_mode(const std::string &v) {
    // HDR and Dolby Vision variants: "hdrCinema", "dolbyHdrFilmMaker".
    std::string base = v;
    for (const char *prefix : {"dolbyHdr", "hdr"}) {
        size_t n = std::string(prefix).size();
        if (v.size() > n && v.compare(0, n, prefix) == 0) {
            base = v.substr(n);
            base[0] = char(std::tolower(static_cast<unsigned char>(base[0])));
            break;
        }
    }
    auto it = kPictureModes.find(base);
    return it != kPictureModes.end() ? it->second : humanize(v);
}

std::string color_temperature(const std::string &v) {
    int n = std::atoi(v.c_str());
    return n < 0 ? "W" + std::to_string(-n) : n > 0 ? "C" + std::to_string(n) : "0";
}

const std::vector<std::string> kSleepOptions = {"off", "10", "20", "30", "60", "90", "120", "180", "240"};

std::vector<Item> make_items() {
    auto sound_modes = from_map({{"aiSoundPlus", "AI Sound Pro"}, {"aiSound", "AI Sound"}, {"standard", "Standard"},
                                 {"news", "Clear Voice"}, {"music", "Music"}, {"movie", "Cinema"},
                                 {"sports", "Sports"}, {"game", "Game Optimizer"}, {"personalized", "Personalized"},
                                 {"precision", "Precision Sound"}});
    auto outputs = from_map({{"tv_speaker", "TV Speaker"}, {"external_arc", "HDMI ARC"},
                             {"external_optical", "Optical"}, {"bt_soundbar", "Bluetooth"}, {"bt_audio", "Bluetooth"},
                             {"headphone", "Headphones"}, {"lineout", "Audio Out"}, {"usb_speaker", "USB Audio"},
                             {"mobile_phone", "Phone"}, {"wisa_speaker", "WiSA Speakers"}, {"wow_cast", "WOWCAST"},
                             {"flex_connector", "FlexConnect"}, {"tv_speaker_external_arc", "ARC + TV Speaker"},
                             {"tv_speaker_bluetooth", "Bluetooth + TV Speaker"},
                             {"tv_speaker_headphone", "Headphones + TV Speaker"},
                             {"tv_external_speaker", "Optical + TV Speaker"},
                             {"tv_speaker_wow_cast", "WOWCAST + TV Speaker"}});
    auto energy = from_map({{"auto", "Auto"}, {"off", "Off"}, {"min", "Minimum"}, {"med", "Medium"}, {"max", "Maximum"}});
    auto trumotion = from_map({{"off", "Off"}, {"cinemaClear", "Cinematic Movement"}, {"natural", "Natural"},
                               {"smooth", "Smooth Movement"}, {"user", "User Selection"}});
    auto gamma = from_map({{"low", "Low"}, {"medium", "Medium"}, {"mediumHavingLevel", "Medium"},
                           {"high1", "High 1"}, {"high2", "High 2"}});
    auto aspect = from_map({{"16x9", "16:9"}, {"4x3", "4:3"}, {"21x9", "21:9"}, {"14x9", "14:9"},
                            {"original", "Original"}, {"full_wide", "Full Wide"}, {"zoom", "Zoom"},
                            {"zoom2", "Zoom 2"}, {"cinema_zoom", "Cinema Zoom"}, {"caption_zoom", "Caption Zoom"}});
    auto tone = from_map({{"on", "On"}, {"off", "Off"}, {"HGIG", "HGIG"}});
    auto minutes_fn = [](const std::string &v) { return minutes(v); };

    return {
        // "Brightness" in LG's menu on OLED is OLED Pixel Brightness, i.e.
        // `backlight`; the `brightness` key is labelled "Black Level".
        {"backlight", "picture", "", "Brightness", "sun", Kind::kRange, nullptr, {}, {}},
        {"pictureMode", "picture", "", "Picture mode", "picture", Kind::kChoice, picture_mode, {}, {}},
        {"eyeComfortMode", "picture", "", "Eye comfort", "eye", Kind::kToggle, nullptr, {}, {}},
        {"energySaving", "picture", "", "Energy saving", "leaf", Kind::kChoice, energy, {}, {}},
        {"contrast", "picture", "", "Contrast", "", Kind::kRange, nullptr, {}, {}},
        {"brightness", "picture", "", "Black level", "", Kind::kRange, nullptr, {}, {}},
        {"color", "picture", "", "Color", "", Kind::kRange, nullptr, {}, {}},
        {"sharpness", "picture", "", "Sharpness", "", Kind::kRange, nullptr, {}, {}},
        {"colorTemperature", "picture", "", "Color temperature", "", Kind::kRange, color_temperature, {}, {}},
        {"peakBrightness", "picture", "", "Peak brightness", "", Kind::kChoice, nullptr, {}, {}},
        {"hdrDynamicToneMapping", "picture", "", "Dynamic tone mapping", "", Kind::kChoice, tone, {}, {}},
        {"gamma", "picture", "", "Gamma", "", Kind::kChoice, gamma, {}, {}},
        {"dynamicContrast", "picture", "", "Dynamic contrast", "", Kind::kChoice, nullptr, {}, {}},
        {"superResolution", "picture", "", "Super resolution", "", Kind::kChoice, nullptr, {}, {}},
        {"noiseReduction", "picture", "", "Noise reduction", "", Kind::kChoice, nullptr, {}, {}},
        {"truMotionMode", "picture", "", "TruMotion", "", Kind::kChoice, trumotion, {}, {}},
        {"realCinema", "picture", "", "Real Cinema", "", Kind::kToggle, nullptr, {}, {}},
        {"motionEyeCare", "picture", "", "Motion eye care", "", Kind::kToggle, nullptr, {}, {}},
        {"ai_Brightness", "aiPicture", "", "AI Brightness", "", Kind::kToggle, nullptr, {}, {}},
        {"aspectRatio", "picture", "", "Aspect ratio", "", Kind::kChoice, aspect, {}, {}},
        {"screenShift", "picture", "", "Screen shift", "", Kind::kToggle, nullptr, {}, {}},
        {"logoLuminanceAdjust", "picture", "", "Logo luminance", "", Kind::kChoice, nullptr, {}, {}},

        {"soundMode", "sound", "", "Sound mode", "sound", Kind::kChoice, sound_modes, {}, {}},
        // The description lists every output the firmware knows. Offer the
        // main wired ones, as LG's own list does; Bluetooth has its own page.
        {"soundOutput", "sound", "", "Sound out", "speaker", Kind::kChoice, outputs,
         {"tv_speaker", "external_arc", "external_optical"}, {}},
        {"autoVolume", "sound", "", "Auto volume", "", Kind::kToggle, nullptr, {}, {}},

        {"sleepTimer", "timer", "", "Sleep timer", "moon", Kind::kChoice, minutes_fn, {}, kSleepOptions},
        {"quickStartMode", "option", "", "Quick Start+", "", Kind::kToggle, nullptr, {}, {}},
        {"screenSaverTimer", "option", "", "Screen saver after", "", Kind::kChoice, minutes_fn, {}, {}},
        {"remotePointer", "general", "", "Remote pointer", "", Kind::kToggle, nullptr, {}, {}},
        {"standByLight", "option", "", "Standby light", "", Kind::kToggle, nullptr, {}, {}},

        {"highContrast", "option", "", "High contrast", "", Kind::kToggle, nullptr, {}, {}},
        {"greyscale", "option", "", "Grayscale", "", Kind::kToggle, nullptr, {}, {}},
        {"menuTransparency", "option", "", "Menu transparency", "", Kind::kToggle, nullptr, {}, {}},
        {"audioGuidance", "option", "", "Audio guidance", "", Kind::kToggle, nullptr, {}, {}},
    };
}

const std::vector<Item> &items() {
    static const std::vector<Item> all = make_items();
    return all;
}

// Writes in one group can change other keys in it (a new picture mode brings
// its own brightness, a new output its own sound mode), so a successful
// choice or toggle write re-reads the group.
std::string group(const std::string &cat) { return cat == "aiPicture" ? "picture" : cat; }

std::string key_of(const Item &item) { return item.key.empty() ? item.id : item.key; }

// Single-quoted for the shell: only the JSON payload is variable.
std::string shell_quote(const std::string &s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string to_json(json_object *obj) {
    std::string s = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
    json_object_put(obj);
    return s;
}

// getSystemSettings answers returnValue false when any key is unknown on this
// model (it names them in errorKey) but still returns the others.
bool succeeded(json_object *reply) {
    json_object *settings;
    return get_bool(reply, "returnValue", true) || json_object_object_get_ex(reply, "settings", &settings);
}

std::string value_text(json_object *v, bool *numeric) {
    *numeric = json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double);
    if (json_object_is_type(v, json_type_double)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", json_object_get_double(v));
        return buf;
    }
    return json_object_get_string(v);
}

}  // namespace

Tv::Tv(Luna &luna) : luna_(luna) {
    // Methods whose ACL excludes "public" on webOS 10 (checked with
    // ls-monitor -i); the rest is learned from whichever route answers.
    exec_first_ = {kSleepTimer + "add", kSleepTimer + "getRemainTime", kTurnOffScreen};
    load_cache();
}

const Item *Tv::item(const std::string &id) {
    for (const Item &item : items()) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

const Setting &Tv::get(const std::string &id) const {
    static const Setting none;
    auto it = state_.find(id);
    return it != state_.end() ? it->second : none;
}

bool Tv::visible(const std::string &id) const {
    const Setting &s = get(id);
    return !s.missing && !s.hidden;
}

std::string Tv::text(const std::string &id) const {
    const Item *it = item(id);
    const Setting &s = get(id);
    if (!it || s.value.empty()) return "—";
    if (id == "sleepTimer" && s.remain > 0) return std::to_string(s.remain) + " min left";
    if (it->kind == Kind::kToggle) return s.value == "on" ? "On" : "Off";
    if (it->format) return it->format(s.value);
    return it->kind == Kind::kRange ? s.value : humanize(s.value);
}

std::string Tv::option_text(const std::string &id, const std::string &option) const {
    const Item *it = item(id);
    if (it && it->format) return it->format(option);
    return humanize(option);
}

std::vector<std::string> Tv::options(const std::string &id) const {
    const Item *it = item(id);
    const Setting &s = get(id);
    std::vector<std::string> out = !s.options.empty() ? s.options : it ? it->options : std::vector<std::string>{};
    // The current value may be one the list leaves out (e.g. a Bluetooth output).
    if (!s.value.empty() && std::find(out.begin(), out.end(), s.value) == out.end()) out.insert(out.begin(), s.value);
    return out;
}

// --- Luna routes -----------------------------------------------------------

void Tv::direct(const std::string &uri, const std::string &payload, Luna::Reply ok,
                std::function<void(const std::string &)> fail) {
    luna_.call(uri, payload, [ok, fail](json_object *reply) {
        if (succeeded(reply)) {
            if (ok) ok(reply);
        } else if (fail) {
            fail(get_string(reply, "errorText", "failed"));
        }
    });
}

void Tv::exec(const std::string &uri, const std::string &payload, Luna::Reply ok,
              std::function<void(const std::string &)> fail, int replies, int wait_ms) {
    // stdin must be redirected or luna-send prints nothing.
    std::string command = "luna-send -n " + std::to_string(replies) +
                          (wait_ms > 0 ? " -w " + std::to_string(wait_ms) : "") + " " + uri + " " +
                          shell_quote(payload) + " </dev/null";
    json_object *params = json_object_new_object();
    json_object_object_add(params, "command", json_object_new_string(command.c_str()));
    luna_.call(kExec, to_json(params), [ok, fail, uri](json_object *reply) {
        // The last line that is a JSON object is luna-send's last reply.
        std::string out = get_string(reply, "stdoutString");
        json_object *last = nullptr;
        size_t start = 0;
        while (start < out.size()) {
            size_t end = out.find('\n', start);
            if (end == std::string::npos) end = out.size();
            if (out[start] == '{') {
                if (json_object *line = json_tokener_parse(out.substr(start, end - start).c_str())) {
                    if (last) json_object_put(last);
                    last = line;
                }
            }
            start = end + 1;
        }
        if (!last) {
            if (fail) fail("no reply from " + uri);
            return;
        }
        if (succeeded(last)) {
            if (ok) ok(last);
        } else if (fail) {
            fail(get_string(last, "errorText", "failed"));
        }
        json_object_put(last);
    });
}

void Tv::call(const std::string &uri, const std::string &payload, Luna::Reply ok,
              std::function<void(const std::string &)> fail) {
    bool via_exec = exec_first_.count(uri) > 0;
    auto retry = [this, uri, payload, ok, fail, via_exec](const std::string &first_error) {
        auto learned = [this, uri, ok, via_exec](json_object *reply) {
            if (via_exec) exec_first_.erase(uri);
            else exec_first_.insert(uri);
            cache_due_ = SDL_GetTicks() + 1000;
            if (ok) ok(reply);
        };
        auto give_up = [fail, first_error](const std::string &) {
            if (fail) fail(first_error);
        };
        if (via_exec) direct(uri, payload, learned, give_up);
        else exec(uri, payload, learned, give_up);
    };
    if (via_exec) exec(uri, payload, ok, retry);
    else direct(uri, payload, ok, retry);
}

// --- Reads -----------------------------------------------------------------

void Tv::received(const std::string &id, Setting changes, bool has_value) {
    Setting &s = state_[id];
    // Unless the user changed it moments ago.
    bool fresh = SDL_GetTicks() - touched_[id] >= kTouchGuard || !touched_[id];
    if (has_value && fresh) {
        s.value = changes.value;
        s.numeric = changes.numeric;
    }
    s.missing = changes.missing;
    if (id == "sleepTimer") s.remain = changes.remain;
    patch(id);
}

void Tv::patch(const std::string &id) {
    if (!cache_due_) cache_due_ = SDL_GetTicks() + 800;
    if (changed) changed(id);
}

void Tv::load(const std::vector<std::string> &ids) {
    std::map<std::string, std::vector<std::string>> by_cat;
    for (const std::string &id : ids) {
        const Item *it = item(id);
        if (!it) continue;
        if (id == "sleepTimer") {
            // LG's settings app shows the smallest option that covers the time left.
            call(kSleepTimer + "getRemainTime", "{}", [this](json_object *r) {
                json_object *v;
                int seconds = json_object_object_get_ex(r, "sleepTimeSecond", &v) ? json_object_get_int(v) : 0;
                Setting s;
                s.remain = (seconds + 59) / 60;
                s.value = "off";
                if (s.remain > 0) {
                    s.value = kSleepOptions.back();
                    for (size_t i = 1; i < kSleepOptions.size(); i++) {
                        if (std::atoi(kSleepOptions[i].c_str()) >= s.remain) {
                            s.value = kSleepOptions[i];
                            break;
                        }
                    }
                }
                received("sleepTimer", s, true);
            });
            continue;
        }
        by_cat[it->cat].push_back(id);
    }
    for (auto &entry : by_cat) {
        const std::string cat = entry.first;
        const std::vector<std::string> ids_in_cat = entry.second;
        json_object *keys = json_object_new_array();
        for (const std::string &id : ids_in_cat) json_object_array_add(keys, json_object_new_string(key_of(*item(id)).c_str()));
        json_object *params = json_object_new_object();
        json_object_object_add(params, "category", json_object_new_string(cat.c_str()));
        json_object_object_add(params, "keys", keys);
        std::string payload = to_json(params);

        call(kSettings + "getSystemSettings", payload, [this, ids_in_cat](json_object *r) {
            json_object *settings = nullptr;
            json_object_object_get_ex(r, "settings", &settings);
            for (const std::string &id : ids_in_cat) {
                json_object *v;
                Setting s;
                if (settings && json_object_object_get_ex(settings, key_of(*item(id)).c_str(), &v)) {
                    s.value = value_text(v, &s.numeric);
                    received(id, s, true);
                } else {
                    s.missing = true;
                    received(id, s, false);
                }
            }
        });
        // The per-model, per-signal lists that description.json only has in raw form.
        call(kSettings + "getSystemSettingDesc", payload, [this, ids_in_cat](json_object *r) {
            json_object *results;
            if (!json_object_object_get_ex(r, "results", &results)) return;
            for (size_t i = 0; i < json_object_array_length(results); i++) {
                json_object *d = json_object_array_get_idx(results, i);
                std::string key = get_string(d, "key");
                for (const std::string &id : ids_in_cat) {
                    if (key_of(*item(id)) != key) continue;
                    Setting &s = state_[id];
                    json_object *ui, *values, *range, *array;
                    if (json_object_object_get_ex(d, "ui", &ui)) {
                        s.hidden = !get_bool(ui, "visible", true);
                        s.disabled = !get_bool(ui, "active", true);
                    }
                    // LG offers the pointer switch only in Easy Mode, so its
                    // platform profile hides and disables it; the key works anyway.
                    if (id == "remotePointer") s.hidden = s.disabled = false;
                    if (json_object_object_get_ex(d, "values", &values)) {
                        if (json_object_object_get_ex(values, "range", &range)) {
                            json_object *v;
                            if (json_object_object_get_ex(range, "min", &v)) s.min = json_object_get_double(v);
                            if (json_object_object_get_ex(range, "max", &v)) s.max = json_object_get_double(v);
                            if (json_object_object_get_ex(range, "interval", &v)) s.step = json_object_get_double(v);
                            if (s.step <= 0) s.step = 1;
                        } else if (json_object_object_get_ex(values, "arrayExt", &array)) {
                            std::vector<std::string> offered;
                            for (size_t j = 0; j < json_object_array_length(array); j++) {
                                json_object *o = json_object_array_get_idx(array, j), *v;
                                if (get_bool(o, "active", true) && get_bool(o, "visible", true) &&
                                    json_object_object_get_ex(o, "value", &v)) {
                                    offered.push_back(json_object_get_string(v));
                                }
                            }
                            // `only` narrows the list and sets its order.
                            const std::vector<std::string> &only = item(id)->only;
                            s.options.clear();
                            if (only.empty()) s.options = offered;
                            for (const std::string &o : only) {
                                if (std::find(offered.begin(), offered.end(), o) != offered.end()) s.options.push_back(o);
                            }
                        }
                    }
                    patch(id);
                }
            }
        });
    }
}

void Tv::reload_group(const std::string &cat) {
    std::vector<std::string> ids;
    for (auto &entry : state_) {
        const Item *it = item(entry.first);
        if (it && group(it->cat) == group(cat)) ids.push_back(entry.first);
    }
    load(ids);
}

// --- Writes ----------------------------------------------------------------

void Tv::save(const std::string &id, const std::string &value, std::function<void(bool)> done) {
    const Item *it = item(id);
    auto fail = [this, it, done](const std::string &err) {
        if (error) error("Couldn't change " + it->label + ": " + err);
        done(false);
    };
    if (id == "sleepTimer") {
        std::string minutes = value == "off" ? "0" : value;
        call(kSleepTimer + "add", "{\"sleepTimeMinute\":\"" + minutes + "\"}", [this, value, done](json_object *) {
            state_["sleepTimer"].remain = value == "off" ? 0 : std::atoi(value.c_str());
            patch("sleepTimer");
            done(true);
        }, fail);
        return;
    }
    json_object *settings = json_object_new_object();
    // Ranges go back with the type they were read with (the service returns
    // `backlight` as a number but `contrast` as a string).
    const Setting &s = get(id);
    json_object *v = s.numeric ? json_object_new_int(int(std::lround(std::atof(value.c_str()))))
                               : json_object_new_string(value.c_str());
    json_object_object_add(settings, key_of(*it).c_str(), v);
    json_object *params = json_object_new_object();
    json_object_object_add(params, "category", json_object_new_string(it->cat.c_str()));
    json_object_object_add(params, "settings", settings);
    call(kSettings + "setSystemSettings", to_json(params), [done](json_object *) { done(true); }, fail);
}

void Tv::pump(const std::string &id) {
    Writer &w = writers_[id];
    if (w.busy || !w.queued) return;
    unsigned now = SDL_GetTicks();
    if (now - w.last_sent < kWriteInterval) return;  // update() tries again next frame
    std::string value = w.latest;
    w.queued = false;
    w.busy = true;
    w.last_sent = now;
    save(id, value, [this, id](bool ok) {
        writers_[id].busy = false;
        const Item *it = item(id);
        if (!ok) {
            touched_.erase(id);
            load({id});
        } else if (it->kind != Kind::kRange) {
            reload_group(it->cat);
        }
        pump(id);
    });
}

void Tv::set(const std::string &id, const std::string &value) {
    touched_[id] = SDL_GetTicks();
    state_[id].value = value;
    patch(id);
    Writer &w = writers_[id];
    w.latest = value;
    w.queued = true;
    pump(id);
}

void Tv::set_number(const std::string &id, double value) {
    const Setting &s = get(id);
    double v = std::clamp(value, s.min, s.max);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    set(id, buf);
}

void Tv::update() {
    for (auto &entry : writers_) {
        if (entry.second.queued) pump(entry.first);
    }
    if (cache_due_ && SDL_GetTicks() >= cache_due_) {
        cache_due_ = 0;
        save_cache();
    }
}

// --- Actions ---------------------------------------------------------------

void Tv::screen_off(std::function<void(bool)> done) {
    // LG's own app instead writes option/screenOff; that is the fallback.
    call(kTurnOffScreen, "{}", [done](json_object *) { done(true); }, [this, done](const std::string &) {
        call(kSettings + "setSystemSettings", R"({"category":"option","settings":{"screenOff":"on"}})",
             [done](json_object *) { done(true); }, [this, done](const std::string &err) {
                 if (error) error("Couldn't turn the screen off: " + err);
                 done(false);
             });
    });
}

void Tv::launch(const std::string &app_id, const std::string &params_json, std::function<void(bool)> done) {
    std::string payload = "{\"id\":\"" + app_id + "\"" + (params_json.empty() ? "" : ",\"params\":" + params_json) + "}";
    call(kLaunch, payload, [done](json_object *) { done(true); }, [this, app_id, done](const std::string &err) {
        if (error) error("Couldn't open " + app_id + ": " + err);
        done(false);
    });
}

// --- Cache -----------------------------------------------------------------

void Tv::load_cache() {
    json_object *root = json_object_from_file(kCache);
    if (!root) return;
    json_object *values, *routes;
    if (json_object_object_get_ex(root, "values", &values)) {
        json_object_object_foreach(values, id, v) {
            if (!item(id)) continue;
            Setting &s = state_[id];
            s.value = get_string(v, "value");
            s.numeric = get_bool(v, "numeric");
            s.missing = get_bool(v, "missing");
            s.hidden = get_bool(v, "hidden");
            json_object *n;
            if (json_object_object_get_ex(v, "min", &n)) s.min = json_object_get_double(n);
            if (json_object_object_get_ex(v, "max", &n)) s.max = json_object_get_double(n);
            if (json_object_object_get_ex(v, "step", &n)) s.step = json_object_get_double(n);
            json_object *options;
            if (json_object_object_get_ex(v, "options", &options)) {
                for (size_t i = 0; i < json_object_array_length(options); i++) {
                    s.options.push_back(json_object_get_string(json_object_array_get_idx(options, i)));
                }
            }
        }
    }
    if (json_object_object_get_ex(root, "execFirst", &routes)) {
        exec_first_.clear();
        for (size_t i = 0; i < json_object_array_length(routes); i++) {
            exec_first_.insert(json_object_get_string(json_object_array_get_idx(routes, i)));
        }
    }
    json_object_put(root);
}

void Tv::save_cache() {
    json_object *root = json_object_new_object(), *values = json_object_new_object(), *routes = json_object_new_array();
    for (auto &entry : state_) {
        const Setting &s = entry.second;
        json_object *v = json_object_new_object();
        json_object_object_add(v, "value", json_object_new_string(s.value.c_str()));
        json_object_object_add(v, "numeric", json_object_new_boolean(s.numeric));
        json_object_object_add(v, "missing", json_object_new_boolean(s.missing));
        json_object_object_add(v, "hidden", json_object_new_boolean(s.hidden));
        json_object_object_add(v, "min", json_object_new_double(s.min));
        json_object_object_add(v, "max", json_object_new_double(s.max));
        json_object_object_add(v, "step", json_object_new_double(s.step));
        json_object *options = json_object_new_array();
        for (const std::string &o : s.options) json_object_array_add(options, json_object_new_string(o.c_str()));
        json_object_object_add(v, "options", options);
        json_object_object_add(values, entry.first.c_str(), v);
    }
    for (const std::string &uri : exec_first_) json_object_array_add(routes, json_object_new_string(uri.c_str()));
    json_object_object_add(root, "values", values);
    json_object_object_add(root, "execFirst", routes);
    std::string tmp = std::string(kCache) + ".new";
    if (json_object_to_file(tmp.c_str(), root) == 0) std::rename(tmp.c_str(), kCache);
    json_object_put(root);
}

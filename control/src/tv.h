#pragma once

// The TV's settings, read and written through com.webos.settingsservice as
// LG's own settings app does. Allowed values come from getSystemSettingDesc,
// so the lists match the model and what is playing. Calls the app's "public"
// Luna group can't make go through the Homebrew Channel's root exec service.
//
// The screen changes on every step; the TV gets the newest value at most every
// kWriteInterval, one request at a time, and always the final value.

#include "luna.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class Kind { kRange, kChoice, kToggle };

// What the TV says about one setting.
struct Setting {
    std::string value;      // as text, ranges too ("" until read)
    bool numeric = false;   // the service gave a number, so one goes back
    bool missing = false;   // not on this model or signal
    bool hidden = false;    // the TV hides it right now
    bool disabled = false;  // shown but not changeable right now
    double min = 0, max = 100, step = 1;
    std::vector<std::string> options;
    int remain = 0;         // sleep timer: minutes left
};

struct Item {
    std::string id, cat, key, label, icon;
    Kind kind;
    // Display text for a value; empty: on/off or humanized.
    std::function<std::string(const std::string &value)> format;
    std::vector<std::string> only;     // offered options, in this order (empty: all)
    std::vector<std::string> options;  // fixed options, for items without a description
};

class Tv {
public:
    explicit Tv(Luna &luna);

    static const Item *item(const std::string &id);

    const Setting &get(const std::string &id) const;
    bool visible(const std::string &id) const;
    // The value as shown, and an option as shown in a list.
    std::string text(const std::string &id) const;
    std::string option_text(const std::string &id, const std::string &option) const;
    std::vector<std::string> options(const std::string &id) const;

    // Reads values and allowed values from the TV; changed() follows.
    void load(const std::vector<std::string> &ids);
    void set(const std::string &id, const std::string &value);
    void set_number(const std::string &id, double value);
    void update();  // sends writes that are due; call once per frame

    void screen_off(std::function<void(bool ok)> done);
    void launch(const std::string &app_id, const std::string &params_json, std::function<void(bool ok)> done);

    // A Luna call, through exec when public can't make it. Learns which route
    // works per method and remembers it in the cache.
    void call(const std::string &uri, const std::string &payload, Luna::Reply ok,
              std::function<void(const std::string &error)> fail = nullptr);
    // As root through exec, always. wait_ms > 0 keeps luna-send listening for
    // up to replies replies (for methods that answer progress, like pairing);
    // ok gets the last.
    void exec(const std::string &uri, const std::string &payload, Luna::Reply ok,
              std::function<void(const std::string &error)> fail = nullptr, int replies = 1, int wait_ms = 0);

    std::function<void(const std::string &id)> changed;
    std::function<void(const std::string &message)> error;

private:
    struct Writer {
        std::string latest;
        bool queued = false, busy = false;
        unsigned last_sent = 0;
    };

    void direct(const std::string &uri, const std::string &payload, Luna::Reply ok,
                std::function<void(const std::string &)> fail);
    void received(const std::string &id, Setting changes, bool has_value);
    void patch(const std::string &id);
    void save(const std::string &id, const std::string &value, std::function<void(bool)> done);
    void pump(const std::string &id);
    void reload_group(const std::string &cat);
    void load_cache();
    void save_cache();

    Luna &luna_;
    std::map<std::string, Setting> state_;
    std::map<std::string, unsigned> touched_;  // id -> SDL_GetTicks of the last local change
    std::map<std::string, Writer> writers_;
    std::set<std::string> exec_first_;
    unsigned cache_due_ = 0;
};

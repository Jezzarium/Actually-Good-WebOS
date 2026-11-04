#include "bluetooth.h"

#include "log.h"

#include <SDL.h>

#include <algorithm>

namespace {

const std::string kBt = "luna://com.webos.service.bluetooth2/";
constexpr unsigned kPollInterval = 1500;  // ms
constexpr unsigned kSearchTime = 20000;   // the adapter's own discovery timeout is about as long

// Audio devices: an A2DP sink, or the "audio/video" major device class.
bool is_audio(json_object *device) {
    json_object *classes, *cod;
    if (json_object_object_get_ex(device, "serviceClasses", &classes)) {
        for (size_t i = 0; i < json_object_array_length(classes); i++) {
            if (get_string(json_object_array_get_idx(classes, i), "mnemonic") == "A2DP-sink") return true;
        }
    }
    if (json_object_object_get_ex(device, "classOfDevice", &cod)) return ((json_object_get_int(cod) >> 8) & 0x1f) == 0x04;
    return false;
}

std::string address_payload(const std::string &address, const std::string &extra = "") {
    return "{\"address\":\"" + address + "\"" + extra + "}";
}

}  // namespace

void Bluetooth::set_active(bool active) {
    active_ = active;
    if (active) next_poll_ = 0;
    else if (searching_) search(false);
}

void Bluetooth::update() {
    unsigned now = SDL_GetTicks();
    if (searching_ && now >= search_until_) search(false);
    if (active_ && !polling_ && now >= next_poll_) poll();
}

void Bluetooth::poll() {
    polling_ = true;
    tv_.exec(kBt + "device/getStatus", "{}", [this](json_object *reply) {
        polling_ = false;
        next_poll_ = SDL_GetTicks() + kPollInterval;
        json_object *list;
        if (!json_object_object_get_ex(reply, "devices", &list)) return;
        std::vector<BtDevice> paired, found;
        for (size_t i = 0; i < json_object_array_length(list); i++) {
            json_object *d = json_object_array_get_idx(list, i), *profiles;
            if (!is_audio(d)) continue;
            BtDevice dev;
            dev.address = get_string(d, "address");
            dev.name = get_string(d, "name", dev.address);
            dev.paired = get_bool(d, "paired");
            dev.connected = json_object_object_get_ex(d, "connectedProfiles", &profiles) &&
                            json_object_array_length(profiles) > 0;
            if (dev.name.empty() || dev.address.empty()) continue;
            (dev.paired ? paired : found).push_back(dev);
        }
        // Found ones only while searching: the service keeps them listed for a while after.
        if (!searching_) found.clear();
        paired.insert(paired.end(), found.begin(), found.end());
        bool same = paired.size() == devices_.size();
        for (size_t i = 0; same && i < paired.size(); i++) {
            same = paired[i].address == devices_[i].address && paired[i].name == devices_[i].name &&
                   paired[i].paired == devices_[i].paired && paired[i].connected == devices_[i].connected;
        }
        if (same) return;
        devices_ = paired;
        if (changed) changed();
    }, [this](const std::string &err) {
        polling_ = false;
        next_poll_ = SDL_GetTicks() + kPollInterval;
        LOG("bluetooth: getStatus: %s", err.c_str());
    });
}

void Bluetooth::search(bool on) {
    searching_ = on;
    search_until_ = SDL_GetTicks() + kSearchTime;
    tv_.exec(kBt + (on ? "adapter/startDiscovery" : "adapter/cancelDiscovery"), "{}", nullptr,
             [](const std::string &err) { LOG("bluetooth: discovery: %s", err.c_str()); });
    next_poll_ = 0;
    if (changed) changed();
}

bool Bluetooth::busy(const std::string &address) const {
    return std::find(busy_.begin(), busy_.end(), address) != busy_.end();
}

void Bluetooth::done(const std::string &address) {
    busy_.erase(std::remove(busy_.begin(), busy_.end(), address), busy_.end());
    next_poll_ = 0;
    if (changed) changed();
}

void Bluetooth::connect(const BtDevice &device) {
    if (busy(device.address)) return;
    busy_.push_back(device.address);
    if (changed) changed();
    std::string address = device.address, name = device.name;
    auto fail = [this, address, name](const std::string &err) {
        if (tv_.error) tv_.error("Couldn't connect " + name + ": " + err);
        done(address);
    };
    auto connect_a2dp = [this, address, fail]() {
        tv_.exec(kBt + "a2dp/connect", address_payload(address), [this, address](json_object *) {
            // LG's own sound-out menu calls a Bluetooth device "bt_soundbar".
            tv_.set("soundOutput", "bt_soundbar");
            done(address);
        }, fail);
    };
    if (device.paired) {
        connect_a2dp();
        return;
    }
    if (searching_) search(false);
    // Pairing answers with progress until it finishes; listen for up to 30 s.
    tv_.exec(kBt + "adapter/pair", address_payload(address, ",\"subscribe\":true"), [connect_a2dp](json_object *) {
        connect_a2dp();
    }, fail, 8, 30000);
}

void Bluetooth::disconnect(const BtDevice &device) {
    std::string address = device.address;
    busy_.push_back(address);
    tv_.exec(kBt + "a2dp/disconnect", address_payload(address), [this, address](json_object *) {
        tv_.set("soundOutput", "tv_speaker");
        done(address);
    }, [this, address](const std::string &err) {
        if (tv_.error) tv_.error("Couldn't disconnect: " + err);
        done(address);
    });
}

void Bluetooth::forget(const BtDevice &device) {
    std::string address = device.address;
    tv_.exec(kBt + "adapter/unpair", address_payload(address), [this, address](json_object *) { done(address); },
             [this, address](const std::string &err) {
                 if (tv_.error) tv_.error("Couldn't forget the device: " + err);
                 done(address);
             });
}

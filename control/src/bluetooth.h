#pragma once

// Bluetooth audio devices (headphones, speakers) through
// com.webos.service.bluetooth2. Its methods aren't in the "public" group, so
// every call goes through exec, and the device list is polled while the
// Bluetooth page is open instead of subscribed to.

#include "tv.h"

#include <string>
#include <vector>

struct BtDevice {
    std::string address, name;
    bool paired = false, connected = false;
};

class Bluetooth {
public:
    explicit Bluetooth(Tv &tv) : tv_(tv) {}

    // Polls the device list while active.
    void set_active(bool active);
    void update();  // once per frame

    void search(bool on);
    bool searching() const { return searching_; }
    // Connects a paired device (pairing a new one first) and sends the TV's
    // sound to it.
    void connect(const BtDevice &device);
    void disconnect(const BtDevice &device);
    void forget(const BtDevice &device);
    // Devices with a request in flight ("Connecting…").
    bool busy(const std::string &address) const;

    // Audio devices only: paired ones first, then those a search found.
    const std::vector<BtDevice> &devices() const { return devices_; }
    std::function<void()> changed;

private:
    void poll();
    void done(const std::string &address);

    Tv &tv_;
    bool active_ = false, searching_ = false, polling_ = false;
    unsigned next_poll_ = 0, search_until_ = 0;
    std::vector<BtDevice> devices_;
    std::vector<std::string> busy_;
};

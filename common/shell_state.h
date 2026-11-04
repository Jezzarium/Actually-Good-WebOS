#pragma once

// What Home and the Control Center tell each other, in a small file both map:
// /tmp/lgtv-shell.state (both apps' jails share the TV's /tmp). Home publishes
// how its status pill should look and asks the Control Center to open things;
// the Control Center draws the pill and says whether it is alive.
//
// Each app writes only its own half. A half is guarded by a sequence count
// (odd while its writer is mid-update), so a reader never sees a half-written
// one. Both apps are 32-bit ARM builds of this same struct.

#include <cstdint>

namespace shell {

// Home's requests to the Control Center.
enum Request : uint8_t { kNoRequest = 0, kOpenPanel, kOpenInputs };
// The Control Center's requests to Home.
enum HomeRequest : uint8_t { kNoHomeRequest = 0, kOpenHomeSettings, kFocusPill };

struct HomeState {
    uint64_t alive_ms = 0;      // now_ms() of Home's last frame
    uint8_t front = 0;          // Home is the app in front (and isn't launching another)
    float pill_alpha = 0;       // what Home would have drawn: 0 scrolled into the grid
    float pill_focus[3] = {};   // the pill's focus springs: Inputs, Control Center, Home Settings
    uint32_t request_seq = 0;   // bumped with each request
    uint8_t request = kNoRequest;
    uint64_t request_ms = 0;
    // When Home last launched the Control Center hidden: a relaunch soon after
    // is that launch reaching a copy that was running after all.
    uint64_t preload_ms = 0;
};

struct ControlState {
    uint64_t alive_ms = 0;      // now_ms(), several times a second while running
    int8_t pointer_button = -1;  // the pill button under the pointer, if any
    uint32_t pointer_seq = 0;   // bumped each time the pointer comes onto a button
    uint32_t request_seq = 0;
    uint8_t request = kNoHomeRequest;
    int8_t request_arg = 0;     // kFocusPill: the button
};

// CLOCK_MONOTONIC in milliseconds: the same clock in every process.
uint64_t now_ms();

class State {
public:
    ~State();
    // Maps the file, creating it if needed. False (and every read empty) if it can't.
    bool open();
    bool is_open() const { return map_ != nullptr; }

    HomeState home() const;
    void set_home(const HomeState &s);
    ControlState control() const;
    void set_control(const ControlState &s);
    // Changes with every set_home(): a cheap test for "anything new from Home".
    uint32_t home_version() const;

private:
    struct File;
    File *map_ = nullptr;
};

}  // namespace shell

#pragma once

// App icons, decoded on a worker thread and uploaded to the GPU a few per
// frame, so loading never stalls drawing. Until an icon is ready, get()
// returns an empty texture and the tile shows the app's name.

#include "gfx.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Icons {
public:
    Icons();
    ~Icons();

    // The icon at path, or an empty texture while it loads (or if it can't).
    Texture get(const std::string &path);
    // The solid colour around the icon's edge, if it has one (0xRRGGBB):
    // full-square store icons look seamless on a tile of that colour.
    bool edge_color(const std::string &path, uint32_t *color) const;
    // Uploads finished icons; call once per frame on the GL thread.
    void upload_ready();
    // Decodes and uploads these icons before returning (or gives up after
    // timeout_ms), so the first frame already has them.
    void load_now(const std::vector<std::string> &paths, int timeout_ms);

private:
    struct Decoded {
        std::string path;
        std::vector<uint8_t> rgba;  // empty if decoding failed
        int w = 0, h = 0;
        bool has_edge = false;
        uint32_t edge = 0;
    };
    enum class State { kQueued, kReady, kFailed };
    struct Entry {
        State state = State::kQueued;
        Texture texture;
        bool has_edge = false;
        uint32_t edge = 0;
    };

    void worker();
    void upload(Decoded &d);

    std::map<std::string, Entry> entries_;  // GL thread only

    std::mutex mutex_;
    std::condition_variable wake_, decoded_;
    std::deque<std::string> queue_;  // paths to decode
    std::deque<Decoded> done_;       // decoded, waiting for upload
    int pending_ = 0;                // queued or being decoded
    bool stop_ = false;
    std::thread thread_;
};

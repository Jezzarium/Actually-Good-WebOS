#include "icons.h"

#include "log.h"

#include <SDL_image.h>

#include <algorithm>
#include <cstring>

namespace {

// Uploading a 192x192 icon takes well under a millisecond; a few per frame
// keeps a long app list from costing a dropped frame.
constexpr int kUploadsPerFrame = 4;

// The average colour of the outermost pixels, if they are opaque and nearly
// uniform. Transparent or busy edges mean the icon wants iconColor behind it.
bool find_edge(const std::vector<uint8_t> &rgba, int w, int h, uint32_t *out) {
    if (w < 8 || h < 8) return false;
    long sum[3] = {0, 0, 0};
    int n = 0, lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    bool transparent = false;
    // Two pixels in from the border: icons often have a soft or rounded edge.
    auto add = [&](int x, int y) {
        const uint8_t *p = &rgba[(size_t(y) * w + x) * 4];
        if (p[3] < 250) {
            transparent = true;  // any transparency rules it out
            return;
        }
        for (int c = 0; c < 3; c++) {
            sum[c] += p[c];
            lo[c] = std::min<int>(lo[c], p[c]);
            hi[c] = std::max<int>(hi[c], p[c]);
        }
        n++;
    };
    for (int x = w / 8; x < w - w / 8; x += 4) {
        add(x, 2);
        add(x, h - 3);
    }
    for (int y = h / 8; y < h - h / 8; y += 4) {
        add(2, y);
        add(w - 3, y);
    }
    if (transparent || n == 0) return false;
    for (int c = 0; c < 3; c++) {
        if (hi[c] - lo[c] > 24) return false;
    }
    *out = uint32_t(sum[0] / n) << 16 | uint32_t(sum[1] / n) << 8 | uint32_t(sum[2] / n);
    return true;
}

}  // namespace

Icons::Icons() : thread_(&Icons::worker, this) {}

Icons::~Icons() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    thread_.join();
    for (auto &entry : entries_) destroy(entry.second.texture);
}

Texture Icons::get(const std::string &path) {
    if (path.empty()) return {};
    auto it = entries_.find(path);
    if (it != entries_.end()) return it->second.texture;
    entries_[path] = Entry{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(path);
    }
    wake_.notify_one();
    return {};
}

bool Icons::edge_color(const std::string &path, uint32_t *color) const {
    auto it = entries_.find(path);
    if (it == entries_.end() || !it->second.has_edge) return false;
    *color = it->second.edge;
    return true;
}

void Icons::upload_ready() {
    for (int i = 0; i < kUploadsPerFrame; i++) {
        Decoded d;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (done_.empty()) return;
            d = std::move(done_.front());
            done_.pop_front();
        }
        Entry &entry = entries_[d.path];
        if (d.rgba.empty()) {
            entry.state = State::kFailed;
        } else {
            entry.texture = upload_rgba(d.rgba.data(), d.w, d.h);
            entry.has_edge = d.has_edge;
            entry.edge = d.edge;
            entry.state = State::kReady;
        }
    }
}

void Icons::worker() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            path = std::move(queue_.front());
            queue_.pop_front();
        }

        Decoded d;
        d.path = path;
        if (SDL_Surface *img = IMG_Load(path.c_str())) {
            if (SDL_Surface *rgba = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ABGR8888, 0)) {
                d.w = rgba->w;
                d.h = rgba->h;
                d.rgba.resize(size_t(d.w) * d.h * 4);
                for (int y = 0; y < d.h; y++) {
                    std::memcpy(&d.rgba[size_t(y) * d.w * 4], static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch,
                                size_t(d.w) * 4);
                }
                SDL_FreeSurface(rgba);
                d.has_edge = find_edge(d.rgba, d.w, d.h, &d.edge);
            }
            SDL_FreeSurface(img);
        } else {
            LOG("icon %s: %s", path.c_str(), IMG_GetError());
        }

        std::lock_guard<std::mutex> lock(mutex_);
        done_.push_back(std::move(d));
    }
}

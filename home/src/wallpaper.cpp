#include "wallpaper.h"

#include "log.h"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct Blob {
    float x, y, radius;  // 0..1 of the screen
    float r, g, b, strength;
};

// Night-sky blues and purples with soft glows, like a tvOS aerial at dusk.
const Blob kBlobs[] = {
    {0.18f, 0.25f, 0.55f, 0.20f, 0.45f, 0.75f, 0.55f},
    {0.85f, 0.15f, 0.45f, 0.65f, 0.30f, 0.70f, 0.45f},
    {0.70f, 0.95f, 0.60f, 0.95f, 0.45f, 0.35f, 0.40f},
};

// Three box-blur passes per axis approximate a gaussian.
void box_blur(std::vector<float> &px, int w, int h, int radius) {
    std::vector<float> tmp(px.size());
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float sum[3] = {0, 0, 0};
                int n = 0;
                for (int k = std::max(0, x - radius); k <= std::min(w - 1, x + radius); k++, n++) {
                    for (int c = 0; c < 3; c++) sum[c] += px[(y * w + k) * 3 + c];
                }
                for (int c = 0; c < 3; c++) tmp[(y * w + x) * 3 + c] = sum[c] / n;
            }
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float sum[3] = {0, 0, 0};
                int n = 0;
                for (int k = std::max(0, y - radius); k <= std::min(h - 1, y + radius); k++, n++) {
                    for (int c = 0; c < 3; c++) sum[c] += tmp[(k * w + x) * 3 + c];
                }
                for (int c = 0; c < 3; c++) px[(y * w + x) * 3 + c] = sum[c] / n;
            }
        }
    }
}

// Float RGB to RGBA bytes, with a little noise so gradients do not band.
std::vector<uint8_t> to_rgba(const std::vector<float> &px, int w, int h) {
    std::vector<uint8_t> out(size_t(w) * h * 4);
    uint32_t seed = 12345;
    for (int i = 0; i < w * h; i++) {
        for (int c = 0; c < 3; c++) {
            seed = seed * 1664525u + 1013904223u;
            float noise = (float(seed >> 8) / 16777216.f - 0.5f) / 255.f;
            float v = std::clamp(px[i * 3 + c] + noise, 0.f, 1.f);
            out[i * 4 + c] = uint8_t(v * 255.f + 0.5f);
        }
        out[i * 4 + 3] = 255;
    }
    return out;
}

// Night-sky gradient with soft glows, as float RGB.
std::vector<float> gradient(int w, int h) {
    std::vector<float> px(size_t(w) * h * 3);
    for (int y = 0; y < h; y++) {
        float fy = float(y) / h;
        for (int x = 0; x < w; x++) {
            float fx = float(x) / w;
            // Deep navy at the top to plum at the bottom.
            float r = 0.05f + 0.20f * fy, g = 0.07f + 0.05f * fy, b = 0.18f + 0.14f * fy;
            for (const Blob &blob : kBlobs) {
                float dx = (fx - blob.x) * float(w) / h, dy = fy - blob.y;
                float k = std::exp(-(dx * dx + dy * dy) / (blob.radius * blob.radius)) * blob.strength;
                r += blob.r * k * 0.6f;
                g += blob.g * k * 0.6f;
                b += blob.b * k * 0.6f;
            }
            float *p = &px[(size_t(y) * w + x) * 3];
            p[0] = r;
            p[1] = g;
            p[2] = b;
        }
    }
    return px;
}

// The image as RGBA bytes covering exactly w x h (scaled and centre-cropped).
std::vector<uint8_t> load_image(const std::string &path, int w, int h) {
    SDL_Surface *img = IMG_Load(path.c_str());
    if (!img) {
        LOG("wallpaper %s: %s", path.c_str(), IMG_GetError());
        return {};
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(img);
    if (!rgba) return {};
    if (rgba->w != w || rgba->h != h) {
        float scale = std::max(float(w) / rgba->w, float(h) / rgba->h);
        int cw = int(w / scale), ch = int(h / scale);
        SDL_Rect src{(rgba->w - cw) / 2, (rgba->h - ch) / 2, cw, ch};
        SDL_Surface *fit = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
        SDL_BlitScaled(rgba, &src, fit, nullptr);
        SDL_FreeSurface(rgba);
        rgba = fit;
    }
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) {
        std::memcpy(&out[size_t(y) * w * 4], static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch, size_t(w) * 4);
    }
    SDL_FreeSurface(rgba);
    return out;
}

// Written beside and renamed over, so a reader never sees half a file.
void save_backdrop(std::vector<uint8_t> &rgba, int w, int h, const std::string &path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(rgba.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
    SDL_Surface *rgb = s ? SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0) : nullptr;
    std::string tmp = path + ".tmp";
    if (!rgb || SDL_SaveBMP(rgb, tmp.c_str()) != 0 || std::rename(tmp.c_str(), path.c_str()) != 0) {
        LOG("wallpaper: saving %s: %s", path.c_str(), SDL_GetError());
    }
    SDL_FreeSurface(rgb);
    SDL_FreeSurface(s);
}

}  // namespace

Wallpaper load_wallpaper(const std::string &path, int w, int h, const std::string &backdrop_path) {
    std::vector<uint8_t> rgba = load_image(path, w, h);
    if (rgba.empty()) rgba = to_rgba(gradient(w, h), w, h);

    Wallpaper wp;
    wp.sharp = upload_rgba(rgba.data(), w, h);

    // Blur at a quarter of the size; the GPU's filtering smooths it back up.
    int bw = w / 4, bh = h / 4;
    std::vector<float> small(size_t(bw) * bh * 3, 0.f);
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            for (int c = 0; c < 3; c++) {
                int sum = 0;
                for (int j = 0; j < 4; j++) {
                    for (int i = 0; i < 4; i++) sum += rgba[((size_t(y) * 4 + j) * w + x * 4 + i) * 4 + c];
                }
                small[(size_t(y) * bw + x) * 3 + c] = sum / (16 * 255.f);
            }
        }
    }
    box_blur(small, bw, bh, 10);
    std::vector<uint8_t> blurred = to_rgba(small, bw, bh);
    wp.blurred = upload_rgba(blurred.data(), bw, bh);
    if (!backdrop_path.empty()) save_backdrop(blurred, bw, bh, backdrop_path);
    return wp;
}

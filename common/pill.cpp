#include "pill.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace {

// Right-aligned with Home's dock glass (home/src/home.cpp: kLeft, kDockPad).
constexpr float kRight = 1839, kTop = 64, kHeight = 64;
constexpr float kPad = 26, kGap = 22, kInset = 6;

bool inside(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

}  // namespace

void Pill::init(TTF_Font *font) {
    font_ = font;
    refresh_clock();
}

bool Pill::refresh_clock() {
    std::time_t now = std::time(nullptr);
    std::tm local;
    localtime_r(&now, &local);
    if (local.tm_min == minute_) return false;
    minute_ = local.tm_min;
    char text[16];
    std::strftime(text, sizeof text, "%-I:%M %p", &local);
    destroy(clock_);
    clock_ = render_text(font_, text);
    layout();
    return true;
}

void Pill::set_gear(bool shown) {
    if (shown == gear_) return;
    gear_ = shown;
    layout();
}

void Pill::layout() {
    float bh = kHeight - 2 * kInset;
    Rect inputs{0, 0, bh, bh}, control{0, 0, bh, bh}, gear{0, 0, gear_ ? bh : 0, bh};
    float width = kPad + clock_.w + kGap + inputs.w + kInset + control.w + kInset + (gear_ ? gear.w + kInset : 0);
    rect_ = {kRight - width, kTop, width, kHeight};
    gear.x = rect_.x + rect_.w - (gear_ ? gear.w + kInset : 0);
    control.x = (gear_ ? gear.x : rect_.x + rect_.w) - kInset - control.w;
    inputs.x = control.x - kInset - inputs.w;
    gear.y = control.y = inputs.y = rect_.y + kInset;
    buttons_[kInputs] = inputs;
    buttons_[kControl] = control;
    buttons_[kSettings] = gear;
}

int Pill::hit(float x, float y) const {
    for (int i = 0; i < kButtons; i++) {
        if (buttons_[i].w > 0 && inside(buttons_[i], x, y)) return i;
    }
    return -1;
}

void Pill::draw(Painter &p, float alpha, const float focus[kButtons], const Texture glyphs[kButtons],
                bool glass) const {
    if (alpha < 0.005f) return;
    if (glass) {
        // Darker glass than Home's dock: the top of a photo is often bright sky or sand.
        p.glass(rect_, kHeight / 2, rgb(0x0b1016, 0.38f), alpha);
    } else {
        // The Control Center's glass over apps: one dark colour inside a faint rim.
        p.fill(rect_, kHeight / 2, rgb(0xffffff, 0.16f), rgb(0xffffff, 0.06f), alpha);
        p.fill(rect_.inset(1.5f), kHeight / 2 - 1.5f, rgb(0x15191f), rgb(0x15191f), alpha);
    }
    p.text(std::round(rect_.x + kPad), std::round(rect_.y + (kHeight - clock_.h) / 2), clock_, rgb(0xffffff), alpha);

    for (int i = 0; i < kButtons; i++) {
        if (buttons_[i].w <= 0) continue;
        float pf = std::max(0.f, focus[i]);
        Rect b = buttons_[i].scaled(1 + 0.08f * pf);
        if (pf > 0.01f) p.shadow(b, b.h / 2, 18, rgb(0x000000, 0.4f), pf * alpha);
        p.fill(b, b.h / 2, rgb(0xffffff, 0.10f + 0.90f * pf), rgb(0xffffff, 0.10f + 0.90f * pf), alpha);
        // White on glass, dark on the focused button.
        Color ink = {1 - 0.9f * pf, 1 - 0.9f * pf, 1 - 0.88f * pf, 1};
        if (glyphs[i].id) p.image(b.inset(b.w * 0.2f), 0, glyphs[i], ink, alpha);
    }
}

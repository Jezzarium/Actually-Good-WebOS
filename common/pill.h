#pragma once

// The status pill at the top right: the clock, Inputs, the Control Center and
// Home Settings. Home lays it out for focus and the pointer; the Control
// Center (control/) draws it, over Home with the frosted wallpaper behind it
// and over other apps on dark glass, without the Home Settings gear.

#include "gfx.h"

#include <SDL_ttf.h>

class Pill {
public:
    enum Button { kInputs, kControl, kSettings, kButtons };

    // font: the UI font (common/fonts.h), semibold at 28 px in both apps, so the layouts match.
    void init(TTF_Font *font);
    // Renders the clock again when the minute has changed; true if it did.
    bool refresh_clock();
    // Without the gear the pill is narrower; it stays right-aligned.
    void set_gear(bool shown);
    bool gear() const { return gear_; }

    Rect rect() const { return rect_; }
    Rect button(int i) const { return buttons_[i]; }
    // The button at (x, y), or -1.
    int hit(float x, float y) const;

    // focus: one 0..1 value per button. glyphs: one icon per button, white,
    // tinted as they are drawn. glass: the painter's backdrop (Home's blurred
    // wallpaper) behind it, else plain dark glass.
    void draw(Painter &p, float alpha, const float focus[kButtons], const Texture glyphs[kButtons], bool glass) const;

private:
    void layout();

    TTF_Font *font_ = nullptr;
    Texture clock_;
    int minute_ = -1;
    bool gear_ = true;
    Rect rect_{}, buttons_[kButtons]{};
};

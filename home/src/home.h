#pragma once

// The home screen: wallpaper, a status pill (clock + Inputs) at the top right,
// a glass dock of favourite apps, and the grid of all other apps below it.
// Moving down into the grid scrolls the page up and blurs the wallpaper.

#include "apps.h"
#include "gfx.h"
#include "wallpaper.h"

#include <SDL.h>

#include <string>
#include <vector>

// A damped spring toward target, for every animated value.
struct Spring {
    float x = 0, v = 0, target = 0;

    void step(float dt, float stiffness, float damping);
    void snap(float value) { x = target = value, v = 0; }
};

class Home {
public:
    // assets: the folder holding wallpapers/.
    bool init(int w, int h, const char *font_path, const std::string &assets);
    void key(SDL_Keycode key);
    void update(float dt);
    void render(Painter &p);

private:
    // Focus rows: kPill is the status pill, 0 the dock, 1.. the grid rows.
    static constexpr int kPill = -1;

    struct Tile {
        App app;
        Texture name;   // in-tile title
        Texture label;  // caption under the focused grid tile
        Spring focus;
        Spring lean_x, lean_y;  // -1..1, see kLean* in home.cpp
    };

    int dock_count() const;
    int row_count() const;        // dock + grid rows
    int row_length(int row) const;
    int tile_index(int row, int col) const;
    Rect tile_rect(int row, int col) const;  // unscrolled
    // dx, dy: the direction pressed, for the lean (0, 0 when not from a key).
    void move_focus(int row, int col, int dx, int dy);
    void draw_tile(Painter &p, int row, int col);
    void refresh_clock();

    int w_ = 0, h_ = 0;
    Wallpaper wallpaper_;
    TTF_Font *tile_font_ = nullptr, *label_font_ = nullptr, *pill_font_ = nullptr;
    std::vector<Tile> tiles_;

    int row_ = 0, col_ = 0, dock_col_ = 0;
    Spring scroll_, grid_, pill_focus_;

    Texture clock_, inputs_;
    int clock_minute_ = -1;
};

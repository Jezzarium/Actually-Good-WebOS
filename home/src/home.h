#pragma once

// The home screen: wallpaper, a status pill (clock + Inputs) at the top right,
// a glass dock of favourite apps, and the grid of all other apps below it.
// Moving down into the grid scrolls the page up and blurs the wallpaper.

#include "apps.h"
#include "gfx.h"
#include "icons.h"
#include "wallpaper.h"

#include <SDL.h>

#include <functional>
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
    // The Magic Remote pointer, in screen pixels.
    void pointer_move(float x, float y);
    void pointer_click();
    void wheel(int steps);  // positive: up
    void update(float dt);
    void render(Painter &p);
    // Replaces the apps, keeping focus on the same app where it still exists.
    void set_apps(const std::vector<App> &apps);
    void set_inputs(const std::vector<Input> &inputs);

    std::function<void(const std::string &app_id)> on_launch;  // apps and inputs alike

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
    // scroll: bring the focused row into view (not when the pointer moved it).
    void move_focus(int row, int col, int dx, int dy, bool scroll = true);
    void activate();  // OK on the focused item
    bool hit_tile(float x, float y, int *row, int *col) const;
    float max_scroll() const;
    void draw_tile(Painter &p, int row, int col);
    // The inputs panel, hanging below the status pill.
    void draw_inputs(Painter &p, Rect pill);
    void inputs_key(SDL_Keycode key);
    void refresh_clock();

    int w_ = 0, h_ = 0;
    Wallpaper wallpaper_;
    TTF_Font *tile_font_ = nullptr, *label_font_ = nullptr, *pill_font_ = nullptr;
    std::vector<Tile> tiles_;
    Icons icons_;

    int row_ = 0, col_ = 0, dock_col_ = 0;
    Spring scroll_, grid_, pill_focus_;

    bool pointer_ = false;  // the pointer is in use (until a key is pressed)
    float px_ = 0, py_ = 0;
    Rect button_rect_{}, panel_rect_{};  // where render() last drew them

    Texture clock_, inputs_;
    int clock_minute_ = -1;

    struct InputRow {
        Input input;
        Texture name, port;
    };
    std::vector<InputRow> input_rows_;
    bool inputs_open_ = false;
    int input_focus_ = 0;
    Spring inputs_anim_;     // 0 closed .. 1 open
    Spring input_highlight_; // row index, fractional while it slides
};

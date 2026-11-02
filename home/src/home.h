#pragma once

// The home screen: wallpaper, a status pill (clock + Inputs) at the top right,
// a glass dock of favourite apps, and the grid of all other apps below it.
// Moving down into the grid scrolls the page up and blurs the wallpaper.
//
// Long-pressing OK on a tile opens a menu to move or hide it. Moving picks the
// tile up (it wiggles) and the arrows carry it through the dock and grid.
// A built-in Home Settings tile opens the settings sheet: wallpapers and the
// day/night appearance.

#include "apps.h"
#include "gfx.h"
#include "icons.h"
#include "layout.h"
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
    // OK acts on release, so a long press can mean something else.
    void key_down(SDL_Keycode key, bool repeat);
    void key_up(SDL_Keycode key);
    // The Magic Remote pointer, in screen pixels.
    void pointer_move(float x, float y);
    void pointer_down();
    void pointer_up();
    void wheel(int steps);  // positive: up
    void pointer_hidden();  // LG hides the pointer after a while or on a key
    void options();         // Play/Pause: the focused tile's menu, like a long press
    void update(float dt);
    void render(Painter &p);

    // All of LG's launch points; the layout decides order and what is hidden.
    void set_apps(const std::vector<App> &apps);
    void set_inputs(const std::vector<Input> &inputs);
    // Where the layout is saved; loads it and re-applies it.
    void set_layout_path(const std::string &path);

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
        Spring x, y;            // drawn position, sliding to its slot
    };

    // A glass list of rows with a sliding highlight: the Inputs panel and the
    // tile menu.
    struct Menu {
        struct Row {
            Texture label, detail;  // detail: dim text at the right, may be empty
            bool dot = false;
            uint32_t dot_color = 0;
        };
        std::vector<Row> rows;
        Texture title;  // optional header
        int focus = 0;
        bool open = false;
        Spring anim, highlight;
        Rect rect{};  // where it was last drawn

        void show();
        void hide() { open = false, anim.target = 0; }
        void clear();
    };
    enum class MenuResult { kNone, kChosen, kClosed };

    // Tile order and focus.
    int dock_count() const;
    int row_count() const;        // dock + grid rows
    int row_length(int row) const;
    int tile_index(int row, int col) const;
    void index_to_slot(int index, int *row, int *col) const;
    Rect tile_rect(int row, int col) const;  // unscrolled
    // dx, dy: the direction pressed, for the lean (0, 0 when not from a key).
    // scroll: bring the focused row into view (not when the pointer moved it).
    void move_focus(int row, int col, int dx, int dy, bool scroll = true);
    void press(SDL_Keycode key);  // a key, or OK released before a long press
    void activate();              // OK on the focused item
    void long_press();            // held OK: the tile menu
    bool hit_tile(float x, float y, int *row, int *col) const;
    std::vector<float> scroll_stops() const;
    void rebuild_tiles();         // from apps_ and layout_

    // Edit mode.
    void move_key(SDL_Keycode key);
    void save_layout();

    // Menus.
    MenuResult menu_key(Menu &m, SDL_Keycode key);
    void menu_pointer(Menu &m, float x, float y);
    void draw_menu(Painter &p, Menu &m, float x, float y, float width, float alpha);

    // Settings.
    void open_settings();
    void settings_key(SDL_Keycode key);
    void refresh_settings();  // row details from layout_
    std::vector<std::string> wallpaper_names() const;
    std::string wallpaper_path(const std::string &name) const;
    void load_wallpapers();   // from layout_
    bool night_now() const;

    void draw_tile(Painter &p, int index);
    void refresh_clock();

    int w_ = 0, h_ = 0;
    std::string assets_;
    Wallpaper wallpaper_, night_wallpaper_;  // night_ only when a separate image is chosen
    Spring night_;                           // 0 day .. 1 night
    TTF_Font *tile_font_ = nullptr, *label_font_ = nullptr, *pill_font_ = nullptr, *title_font_ = nullptr;
    std::vector<App> apps_;  // everything LG lists
    std::vector<Tile> tiles_;
    Icons icons_;
    Layout layout_;
    std::string layout_path_;

    int row_ = 0, col_ = 0, dock_col_ = 0;
    Spring scroll_, grid_, pill_focus_;
    float time_ = 0;

    // OK (or the pointer button) held down, for long presses.
    bool holding_ = false, held_long_ = false, hold_pointer_ = false;
    float hold_time_ = 0;

    bool pointer_ = false;  // the pointer is in use (until a key is pressed)
    float px_ = 0, py_ = 0;
    bool rehover_ = false;  // re-pick the tile under the pointer once a wheel scroll settles
    Rect button_rect_{};    // where render() last drew the Inputs button

    Texture clock_, inputs_label_;
    int clock_minute_ = -1;

    std::vector<Input> inputs_;
    Menu inputs_menu_;
    Menu tile_menu_;  // Move, Hide
    Menu settings_;
    bool moving_ = false;
    Spring wiggle_;   // 0..1, how much the picked-up tile wiggles
};

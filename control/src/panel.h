#pragma once

// The Control Center panel: a compact glass sheet in the top-right corner, in
// the home screen's design, hanging from the status pill (common/pill.h).
//
// The first page has Brightness, a grid of quick settings and two actions;
// the others are lists (choices, More settings, its sections, Bluetooth) with
// Home's sliding highlight. Pages slide within the same panel, which springs
// to each page's height. The pill's Inputs button opens the same panel with
// the list of inputs instead, narrower, as Home's Inputs menu was.

#include "bluetooth.h"
#include "gfx.h"
#include "inputs.h"
#include "spring.h"
#include "tv.h"

#include <SDL.h>

#include <algorithm>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Panel {
public:
    enum class Root { kSettings, kInputs };

    bool init(const std::string &font_dir, const std::string &icons_dir, Tv *tv, Bluetooth *bt);
    // Where it hangs from: the pill's right edge, and its bottom plus a gap.
    void set_anchor(float right, float top);

    // Settings: the first page, Brightness focused, values read again. Inputs:
    // the inputs. Already open, it switches to that root. over_home: Home's
    // wallpaper glass rather than dark glass.
    void open(Root root, bool over_home);
    void close(bool fade = true);  // fades out
    bool is_open() const { return open_; }
    Root root() const;
    bool faded_out() const { return !open_ && anim_.x < 0.01f; }
    float shown() const { return std::clamp(anim_.x, 0.f, 1.f); }  // 0 closed .. 1 open
    // Whether focus is in the panel; when it isn't (it is on the pill), no cell is lit.
    void set_active(bool active);
    bool active() const { return active_on_; }
    Rect rect() const;  // where it is drawn

    void set_inputs(const std::vector<Input> &inputs);
    // Home's blurred wallpaper (again if Home saved a new one); none until Home has run.
    void load_backdrop();
    const Texture &backdrop() const { return backdrop_; }
    Texture icon(const std::string &name);

    void key_down(SDL_Keycode key, bool repeat);
    void key_up(SDL_Keycode key);
    void options();  // Play/Pause: Forget on a Bluetooth device
    void pointer_move(float x, float y);
    void pointer_click();
    void wheel(int steps);  // positive: up

    void update(float dt);
    void render(Painter &p);

    void setting_changed(const std::string &id);
    void devices_changed();
    void show_toast(const std::string &message);

    // The panel is done: Back on the first page, or an action that leaves it.
    std::function<void()> on_close;
    // Up from the top row: focus goes to the pill.
    std::function<void()> on_leave_top;

private:
    enum class Type { kSlider, kTile, kButton, kRow, kHeader };
    struct Cell {
        Type type = Type::kRow;
        std::string key;     // identity, to keep focus across rebuilds
        std::string id;      // the setting shown, if any
        std::string icon;
        std::string label_text, value_text;
        Texture label, value;
        std::string shown;   // the text value was rendered from
        Rect rect{};         // in page coordinates
        bool chevron = false, check = false, toggle = false, toggle_on = false, disabled = false;
        uint32_t dot = 0;    // an input's status dot, 0xRRGGBB; 0: none
        std::function<void()> activate;
        Spring focus;
    };
    struct Page {
        enum Kind { kMain, kPicker, kMore, kSection, kBluetooth, kInputs } kind = kMain;
        std::string title, arg;  // arg: the setting picked, or the section
        Texture title_tex;
        std::vector<Cell> cells;
        std::vector<std::vector<int>> grid;  // focus rows of cell indices
        int row = 0, col = 0;
        float height = 0;
        bool list = false;                   // a sliding highlight instead of per-cell fills
        Spring scroll, highlight;
        std::string shape;                   // what the cells depend on; rebuilt when it changes
    };

    void build(Page &page);
    void rebuild(Page &page);  // keeps focus and its springs
    std::string shape_of(const Page &page) const;
    void refresh(Cell &cell);
    void push(Page::Kind kind, const std::string &title, const std::string &arg = "");
    void pop();
    Cell *focused();
    void move_focus(int dr, int dc);
    void set_focus(int row, int col);
    void adjust(Cell &cell, int dir);
    void scroll_into_view(Page &page, bool snap);
    static bool has_header(const Page &page) { return page.list && page.kind != Page::kInputs; }
    static float width_of(const Page &page);
    float max_height() const;
    Texture text(TTF_Font *font, const std::string &s);
    void draw_page(Painter &p, Page &page, float ox, float oy, float alpha);
    void draw_cell(Painter &p, Page &page, Cell &cell, float ox, float oy, float alpha);

    Tv *tv_ = nullptr;
    Bluetooth *bt_ = nullptr;
    std::string icons_dir_;
    TTF_Font *label_font_ = nullptr, *small_font_ = nullptr, *value_font_ = nullptr, *title_font_ = nullptr;
    std::map<std::string, Texture> icons_;
    std::vector<std::unique_ptr<Page>> stack_;

    float right_ = 1839, top_ = 142;
    std::vector<Input> inputs_;

    bool open_ = false;
    Spring anim_;        // 0 closed .. 1 open
    Spring active_;      // 0 focus on the pill .. 1 in the panel
    bool active_on_ = true;
    Spring height_, width_;
    Spring slide_;       // page transition: 0 just changed .. 1 settled
    float slide_dir_ = 1;

    // Remote auto-repeat: the longer a slider key is held, the bigger the step.
    SDL_Keycode held_key_ = 0;
    int held_count_ = 0;
    unsigned held_last_ = 0;

    bool pointer_ = false;
    float px_ = 0, py_ = 0;

    Texture toast_;
    Texture backdrop_;  // Home's blurred wallpaper; none until Home has run
    time_t backdrop_time_ = 0;
    bool over_home_ = false;  // Home's wallpaper glass; dark glass over any other app
    float toast_time_ = 0;
};

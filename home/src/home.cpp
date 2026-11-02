#include "home.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <set>

#include <dirent.h>

namespace {

// Layout at 1920x1080, after tvOS.
constexpr int kCols = 6;
constexpr float kTileW = 240, kTileH = 145, kGapX = 50;
constexpr float kRadius = 24;
constexpr float kLeft = (1920 - (kCols * kTileW + (kCols - 1) * kGapX)) / 2;
constexpr float kDockTop = 820;      // tiles in the dock
constexpr float kDockPad = 34;       // glass around the dock tiles
constexpr float kGridTop = 1035;     // first grid row: its top shows below the dock
constexpr float kRowPitch = 240;     // grid rows, with room for the caption
constexpr float kFocusScale = 0.10f;
constexpr float kIconSize = 112;     // icon height inside a tile
constexpr float kIconRadius = 18;

// Springs: tvOS motion is quick but not bouncy.
constexpr float kFocusStiffness = 260, kFocusDamping = 26;
constexpr float kScrollStiffness = 140, kScrollDamping = 24;
constexpr float kSlideStiffness = 220, kSlideDamping = 26;  // tiles moving to a new slot
// Lean: the focused tile's small slide in the direction of travel, which also
// carries the shine. Slightly underdamped so it settles with life.
constexpr float kLeanStiffness = 200, kLeanDamping = 17;
constexpr float kLeanPixels = 7;
// Where the shine rests once the tile is still (0..1 of the tile), and how far
// a full lean moves it.
constexpr float kShineX = 0.5f, kShineY = -0.05f, kShineTravel = 0.55f;

// Holding OK this long opens the tile menu.
constexpr float kLongPress = 0.6f;
// The picked-up tile's wiggle: radians either way, and how fast.
constexpr float kWiggleAngle = 0.035f, kWiggleSpeed = 15;

// Menus (Inputs, the tile menu).
constexpr float kMenuPad = 12, kMenuRowH = 72, kMenuRadius = 30;
constexpr float kInputsW = 480, kTileMenuW = 300, kSettingsW = 820, kMenuTitleH = 92;

// The Control Center (control/): an overlay app, opened by the pill's middle button.
const char *kControlCenter = "com.lgtvtweaks.control";

// Wallpapers: bundled in assets/wallpapers, plus any the user copies here.
const char *kUserWallpapers = "/var/lib/lgtv-home/wallpapers";
const char *kDefaultWallpaper = "Beach.jpg";
// The blurred wallpaper, for the Control Center's glass (control/src/panel.cpp
// reads it). /tmp is the TV's own: both apps' jails share it.
const char *kBackdrop = "/tmp/lgtv-home-backdrop.bmp";
// Automatic appearance: night between these hours.
constexpr int kNightFrom = 19, kNightTo = 7;

Color shade(uint32_t hex, float k, float lift = 0) {
    Color c = rgb(hex);
    return {std::min(1.f, c.r * k + lift), std::min(1.f, c.g * k + lift), std::min(1.f, c.b * k + lift), 1};
}

bool inside(Rect r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

}  // namespace

void Home::Menu::show() {
    open = true;
    anim.target = 1;
    highlight.snap(float(focus));
}

void Home::Menu::clear() {
    for (Row &row : rows) {
        destroy(row.label);
        destroy(row.detail);
    }
    rows.clear();
}

bool Home::init(int w, int h, const char *font_path, const std::string &assets) {
    w_ = w;
    h_ = h;
    assets_ = assets;

    tile_font_ = TTF_OpenFont(font_path, 30);
    label_font_ = TTF_OpenFont(font_path, 26);
    pill_font_ = TTF_OpenFont(font_path, 28);
    title_font_ = TTF_OpenFont(font_path, 40);
    if (!tile_font_ || !label_font_ || !pill_font_ || !title_font_) {
        LOG("font %s: %s", font_path, TTF_GetError());
        return false;
    }

    inputs_label_ = render_text(pill_font_, "Inputs");
    for (const char *action : {"Move", "Hide"}) {
        Menu::Row row;
        row.label = render_text(pill_font_, action);
        tile_menu_.rows.push_back(row);
    }
    settings_.title = render_text(title_font_, "Home Settings");
    for (const char *name : {"Wallpaper", "Appearance", "Show hidden apps", "Reset layout"}) {
        Menu::Row row;
        row.label = render_text(pill_font_, name);
        settings_.rows.push_back(row);
    }
    refresh_clock();
    set_inputs({});
    return true;
}

// --- Settings --------------------------------------------------------------

std::vector<std::string> Home::wallpaper_names() const {
    std::set<std::string> names;
    for (const std::string &dir : {assets_ + "/wallpapers", std::string(kUserWallpapers)}) {
        DIR *d = opendir(dir.c_str());
        if (!d) continue;
        while (dirent *e = readdir(d)) {
            std::string name = e->d_name;
            std::string ext = name.size() > 4 ? name.substr(name.size() - 4) : "";
            for (char &c : ext) c = char(std::tolower(c));
            if (ext == ".jpg" || ext == "jpeg" || ext == ".png") names.insert(name);
        }
        closedir(d);
    }
    return {names.begin(), names.end()};
}

std::string Home::wallpaper_path(const std::string &name) const {
    std::string user = std::string(kUserWallpapers) + "/" + name;
    if (FILE *f = std::fopen(user.c_str(), "rb")) {
        std::fclose(f);
        return user;
    }
    return assets_ + "/wallpapers/" + name;
}

void Home::load_wallpaper_image() {
    destroy(wallpaper_.sharp);
    destroy(wallpaper_.blurred);
    wallpaper_ = load_wallpaper(wallpaper_path(layout_.wallpaper.empty() ? kDefaultWallpaper : layout_.wallpaper),
                                w_, h_, kBackdrop);
}

bool Home::night_now() const {
    if (layout_.appearance == "day") return false;
    if (layout_.appearance == "night") return true;
    std::time_t now = std::time(nullptr);
    std::tm local;
    localtime_r(&now, &local);
    return local.tm_hour >= kNightFrom || local.tm_hour < kNightTo;
}

void Home::open_settings() {
    refresh_settings();
    settings_.focus = 0;
    settings_.show();
}

void Home::refresh_settings() {
    auto stem = [](const std::string &file) { return file.substr(0, file.rfind('.')); };
    std::string details[4];
    // Arrows on the rows Left/Right change.
    auto choice = [](const std::string &value) { return "\u2039  " + value + "  \u203a"; };
    details[0] = choice(stem(layout_.wallpaper.empty() ? kDefaultWallpaper : layout_.wallpaper));
    details[1] = choice(layout_.appearance == "day"     ? "Always day"
                        : layout_.appearance == "night" ? "Always night"
                                                        : "Automatic");
    details[2] = layout_.hidden.empty() ? "None hidden" : std::to_string(layout_.hidden.size()) + " hidden";
    settings_.rows[0].thumb = wallpaper_.sharp;
    for (int i = 0; i < 4; i++) {
        destroy(settings_.rows[i].detail);
        if (!details[i].empty()) settings_.rows[i].detail = render_text(label_font_, details[i]);
    }
}

void Home::settings_key(SDL_Keycode key) {
    int row = settings_.focus;
    int step = key == SDLK_LEFT ? -1 : 1;
    bool cycle = (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_RETURN) && row <= 1;
    if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_ESCAPE || key == SDLK_AC_BACK) {
        menu_key(settings_, key);
        return;
    }
    if (cycle) {
        auto next = [step](const std::vector<std::string> &options, const std::string &current) {
            auto it = std::find(options.begin(), options.end(), current);
            int i = it == options.end() ? 0 : int(it - options.begin());
            return options[(i + step + options.size()) % options.size()];
        };
        std::vector<std::string> names = wallpaper_names();
        if (row == 0 && !names.empty()) {
            layout_.wallpaper = next(names, layout_.wallpaper.empty() ? kDefaultWallpaper : layout_.wallpaper);
            if (layout_.wallpaper == kDefaultWallpaper) layout_.wallpaper.clear();
            load_wallpaper_image();
        } else if (row == 1) {
            layout_.appearance = next({"auto", "day", "night"}, layout_.appearance);
            night_.target = night_now() ? 1 : 0;
        }
    } else if (key == SDLK_RETURN && row == 2) {
        layout_.hidden.clear();
        rebuild_tiles();
    } else if (key == SDLK_RETURN && row == 3) {
        layout_.order.clear();
        layout_.hidden.clear();
        rebuild_tiles();
    } else {
        return;
    }
    if (row >= 2) {
        if (!layout_path_.empty()) layout_.save(layout_path_);
    } else {
        save_layout();
    }
    refresh_settings();
}

// --- Apps and layout -------------------------------------------------------

void Home::set_apps(const std::vector<App> &apps) {
    apps_ = apps;
    rebuild_tiles();
}

void Home::set_layout_path(const std::string &path) {
    layout_path_ = path;
    if (!layout_.load(path)) layout_ = {};
    load_wallpaper_image();
    night_.snap(night_now() ? 1 : 0);
    rebuild_tiles();
}

void Home::load_icons_now(int timeout_ms) {
    std::vector<std::string> paths{assets_ + "/icons/settings.png", assets_ + "/icons/control.png"};
    for (const Tile &t : tiles_) paths.push_back(t.app.icon);
    icons_.load_now(paths, timeout_ms);
}

void Home::save_layout() {
    layout_.order.clear();
    for (const Tile &t : tiles_) layout_.order.push_back(t.app.id);
    if (!layout_path_.empty()) layout_.save(layout_path_);
}

void Home::rebuild_tiles() {
    std::string focused;
    if (row_ != kPill && !tiles_.empty()) focused = tiles_[tile_index(row_, col_)].app.id;

    // Keep tiles that stay, with their textures and positions, so they slide.
    std::map<std::string, Tile> old;
    for (Tile &t : tiles_) old[t.app.id] = std::move(t);
    tiles_.clear();
    for (const App &app : layout_.apply(apps_)) {
        auto it = old.find(app.id);
        if (it != old.end() && it->second.app.title == app.title) {
            tiles_.push_back(std::move(it->second));
            tiles_.back().app = app;
            old.erase(it);
            continue;
        }
        Tile t;
        t.app = app;
        t.name = render_text(tile_font_, app.title);
        t.label = render_text(label_font_, app.title);
        tiles_.push_back(std::move(t));
    }
    for (auto &entry : old) {
        destroy(entry.second.name);
        destroy(entry.second.label);
    }
    // New tiles start in their slot rather than flying in.
    for (int i = 0; i < int(tiles_.size()); i++) {
        Tile &t = tiles_[i];
        if (t.x.target == 0 && t.y.target == 0) {
            int row, col;
            index_to_slot(i, &row, &col);
            Rect r = tile_rect(row, col);
            t.x.snap(r.x);
            t.y.snap(r.y);
        }
    }

    if (tiles_.empty()) {
        moving_ = false;
        move_focus(kPill, 0, 0, 0);
        return;
    }
    int row = 0, col = 0;
    for (int i = 0; i < int(tiles_.size()); i++) {
        if (tiles_[i].app.id == focused) index_to_slot(i, &row, &col);
    }
    row_ = col_ = -2;  // force a fresh focus, not a bump
    move_focus(row, col, 0, 0);
    for (Tile &t : tiles_) t.focus.snap(t.focus.target);
}

int Home::dock_count() const { return std::min<int>(kCols, int(tiles_.size())); }

int Home::row_count() const {
    int grid = int(tiles_.size()) - dock_count();
    return 1 + (grid + kCols - 1) / kCols;
}

int Home::row_length(int row) const {
    if (row == kPill) return kPillButtons;
    if (row == 0) return dock_count();
    int first = dock_count() + (row - 1) * kCols;
    return std::clamp(int(tiles_.size()) - first, 0, kCols);
}

int Home::tile_index(int row, int col) const {
    return row == 0 ? col : dock_count() + (row - 1) * kCols + col;
}

void Home::index_to_slot(int index, int *row, int *col) const {
    if (index < kCols) {
        *row = 0;
        *col = index;
    } else {
        *row = 1 + (index - kCols) / kCols;
        *col = (index - kCols) % kCols;
    }
}

Rect Home::tile_rect(int row, int col) const {
    float y = row == 0 ? kDockTop : kGridTop + (row - 1) * kRowPitch;
    return {kLeft + col * (kTileW + kGapX), y, kTileW, kTileH};
}

// --- Inputs ----------------------------------------------------------------

void Home::set_inputs(const std::vector<Input> &inputs) {
    inputs_ = inputs;
    inputs_menu_.clear();
    for (const Input &input : inputs) {
        Menu::Row row;
        row.label = render_text(pill_font_, input.label);
        // The port only when the name doesn't already say it.
        if (input.label != input.port) row.detail = render_text(label_font_, input.port);
        row.dot = true;
        row.dot_color = input.connected ? 0x30d158 : 0x8e8e93;
        inputs_menu_.rows.push_back(row);
    }
    if (inputs.empty()) {
        // Says so rather than opening nothing; choosing it does nothing.
        Menu::Row row;
        row.label = render_text(pill_font_, "No inputs found");
        inputs_menu_.rows.push_back(row);
    }
    int n = int(inputs_menu_.rows.size());
    inputs_menu_.focus = std::clamp(inputs_menu_.focus, 0, n - 1);
}

void Home::set_status(const std::string &message) {
    destroy(status_);
    if (!message.empty()) status_ = render_text(pill_font_, message);
}

// --- Focus and keys --------------------------------------------------------

void Home::move_focus(int row, int col, int dx, int dy, bool scroll) {
    row = std::clamp(row, kPill, row_count() - 1);
    col = std::clamp(col, 0, row_length(row) - 1);
    if (row == row_ && col == col_ && (dx || dy)) {
        // Nowhere to go: bump toward the edge and back.
        if (row != kPill) {
            Tile &t = tiles_[tile_index(row, col)];
            t.lean_x.v += dx * 9;
            t.lean_y.v += dy * 9;
        }
        return;
    }
    if (row != kPill) {
        // The new tile settles in from the side focus came from.
        Tile &t = tiles_[tile_index(row, col)];
        t.lean_x.snap(-dx);
        t.lean_y.snap(-dy);
        t.lean_x.target = t.lean_y.target = 0;
    }
    row_ = row;
    col_ = col;
    if (row == 0) dock_col_ = col;

    for (Tile &t : tiles_) t.focus.target = 0;
    if (row != kPill) tiles_[tile_index(row, col)].focus.target = 1;
    for (int i = 0; i < kPillButtons; i++) pill_focus_[i].target = row == kPill && col == i ? 1 : 0;

    if (!scroll) return;
    // The first grid row rises to y 300; later rows keep the focused row in view.
    float target = 0;
    if (row >= 1) {
        float top = kGridTop + (row - 1) * kRowPitch;
        target = std::max(kGridTop - 300, top - 540);
    }
    scroll_.target = target;
    grid_.target = row >= 1 ? 1 : 0;
}

std::vector<float> Home::scroll_stops() const {
    // The same resting places the D-pad scrolls to, one per row.
    std::vector<float> stops{0};
    for (int row = 1; row < row_count(); row++) {
        float top = kGridTop + (row - 1) * kRowPitch;
        float stop = std::max(kGridTop - 300, top - 540);
        if (stop > stops.back() + 1) stops.push_back(stop);
    }
    return stops;
}

void Home::key_down(SDL_Keycode key, bool repeat) {
    if (pointer_) {
        // Back to the D-pad: scroll to wherever focus is.
        pointer_ = false;
        if (row_ >= 1 || scroll_.target > 0) move_focus(row_, col_, 0, 0);
    }
    if (key == SDLK_RETURN) {
        if (!repeat) {
            holding_ = true;
            held_long_ = hold_pointer_ = false;
            hold_time_ = 0;
        }
        return;
    }
    press(key);
}

void Home::key_up(SDL_Keycode key) {
    if (key != SDLK_RETURN || !holding_ || hold_pointer_) return;
    holding_ = false;
    if (!held_long_) press(SDLK_RETURN);
}

void Home::press(SDL_Keycode key) {
    if (settings_.open) {
        settings_key(key);
        return;
    }
    if (tile_menu_.open) {
        if (menu_key(tile_menu_, key) == MenuResult::kChosen) {
            if (tile_menu_.focus == 0) {
                moving_ = true;
                wiggle_.target = 1;
            } else if (row_ != kPill) {
                layout_.hidden.push_back(tiles_[tile_index(row_, col_)].app.id);
                tiles_.erase(tiles_.begin() + tile_index(row_, col_));
                save_layout();
                rebuild_tiles();
            }
        }
        return;
    }
    if (inputs_menu_.open) {
        if (menu_key(inputs_menu_, key) == MenuResult::kChosen && on_launch &&
            inputs_menu_.focus < int(inputs_.size())) {
            on_launch(inputs_[inputs_menu_.focus].app_id);
        }
        return;
    }
    if (moving_) {
        move_key(key);
        return;
    }
    switch (key) {
    case SDLK_LEFT: move_focus(row_, col_ - 1, -1, 0); break;
    case SDLK_RIGHT: move_focus(row_, col_ + 1, 1, 0); break;
    case SDLK_UP: move_focus(row_ - 1, row_ == 0 ? 0 : col_, 0, -1); break;
    case SDLK_DOWN: move_focus(row_ + 1, row_ == kPill ? dock_col_ : col_, 0, 1); break;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        if (row_ >= 1) move_focus(0, dock_col_, 0, -1);
        break;
    case SDLK_RETURN: activate(); break;
    default: LOG("home: key %d", int(key)); break;
    }
}

void Home::activate() {
    if (row_ == kPill && col_ == 2) {
        open_settings();
    } else if (row_ == kPill && col_ == 1) {
        if (on_launch) on_launch(kControlCenter);
    } else if (row_ == kPill) {
        inputs_menu_.show();
    } else if (on_launch) {
        on_launch(tiles_[tile_index(row_, col_)].app.id);
    }
}

void Home::long_press() {
    if (row_ == kPill || moving_ || tile_menu_.open || inputs_menu_.open || settings_.open || tiles_.empty()) return;
    tile_menu_.focus = 0;
    tile_menu_.show();
}

void Home::move_key(SDL_Keycode key) {
    int n = int(tiles_.size());
    int i = tile_index(row_, col_), j = i, dx = 0, dy = 0;
    switch (key) {
    case SDLK_LEFT: j = i - 1, dx = -1; break;
    case SDLK_RIGHT: j = i + 1, dx = 1; break;
    case SDLK_UP:
        dy = -1;
        if (row_ == 1) j = std::min(col_, kCols - 1);  // into the dock, at the same column
        else if (row_ > 1) j = i - kCols;
        break;
    case SDLK_DOWN:
        dy = 1;
        if (row_ == 0) j = std::min(n - 1, kCols + col_);
        else if (row_ < row_count() - 1) j = std::min(n - 1, i + kCols);
        break;
    case SDLK_RETURN:
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        moving_ = false;
        wiggle_.target = 0;
        save_layout();
        return;
    default: return;
    }
    if (j < 0 || j >= n || j == i) {
        Tile &t = tiles_[i];
        t.lean_x.v += dx * 9;
        t.lean_y.v += dy * 9;
        return;
    }
    Tile t = std::move(tiles_[i]);
    tiles_.erase(tiles_.begin() + i);
    tiles_.insert(tiles_.begin() + j, std::move(t));
    int row, col;
    index_to_slot(j, &row, &col);
    row_ = col_ = -2;
    move_focus(row, col, 0, 0);
}

// --- Pointer ---------------------------------------------------------------

bool Home::hit_tile(float x, float y, int *row, int *col) const {
    for (int r = 0; r < row_count(); r++) {
        for (int c = 0; c < row_length(r); c++) {
            Rect t = tile_rect(r, c);
            t.y -= scroll_.x;
            if (inside(t, x, y)) {
                *row = r;
                *col = c;
                return true;
            }
        }
    }
    return false;
}

void Home::pointer_move(float x, float y) {
    pointer_ = true;
    px_ = x;
    py_ = y;
    if (settings_.open) return menu_pointer(settings_, x, y);
    if (tile_menu_.open) return menu_pointer(tile_menu_, x, y);
    if (inputs_menu_.open) return menu_pointer(inputs_menu_, x, y);
    if (moving_) return;
    int row, col;
    int which = -1;
    for (int i = 0; i < kPillButtons; i++) {
        if (inside(button_rect_[i], x, y)) which = i;
    }
    if (grid_.x < 0.5f && which >= 0) {
        if (row_ != kPill || col_ != which) move_focus(kPill, which, 0, 0, false);
    } else if (hit_tile(x, y, &row, &col) && (row != row_ || col != col_)) {
        move_focus(row, col, 0, 0, false);
    }
}

void Home::pointer_hidden() {
    pointer_ = false;
    rehover_ = false;
}

void Home::options() { long_press(); }

void Home::pointer_down() {
    holding_ = hold_pointer_ = true;
    held_long_ = false;
    hold_time_ = 0;
}

void Home::pointer_up() {
    if (!holding_ || !hold_pointer_) return;
    holding_ = false;
    if (held_long_) return;
    // A click.
    for (Menu *m : {&settings_, &tile_menu_, &inputs_menu_}) {
        if (!m->open) continue;
        if (inside(m->rect, px_, py_)) press(SDLK_RETURN);
        else m->hide();
        return;
    }
    if (moving_) return press(SDLK_RETURN);  // drop
    int row, col;
    bool on_button = row_ == kPill && inside(button_rect_[col_], px_, py_);
    bool on_tile = hit_tile(px_, py_, &row, &col) && row == row_ && col == col_;
    if (on_button || on_tile) activate();
}

void Home::wheel(int steps) {
    if (settings_.open || tile_menu_.open || inputs_menu_.open || moving_) {
        press(steps > 0 ? SDLK_UP : SDLK_DOWN);
        return;
    }
    // One row per step, snapping to the D-pad's resting places; focus goes to
    // whatever is under the pointer once the page settles.
    std::vector<float> stops = scroll_stops();
    int nearest = 0;
    for (int i = 0; i < int(stops.size()); i++) {
        if (std::abs(stops[i] - scroll_.target) < std::abs(stops[nearest] - scroll_.target)) nearest = i;
    }
    int next = std::clamp(nearest - steps, 0, int(stops.size()) - 1);
    scroll_.target = stops[next];
    grid_.target = next > 0 ? 1 : 0;
    rehover_ = pointer_;
}

// --- Menus -----------------------------------------------------------------

Home::MenuResult Home::menu_key(Menu &m, SDL_Keycode key) {
    int n = int(m.rows.size());
    switch (key) {
    case SDLK_UP:
    case SDLK_DOWN: {
        int next = m.focus + (key == SDLK_DOWN ? 1 : -1);
        if (next >= 0 && next < n) {
            m.focus = next;
            m.highlight.target = float(next);
        } else {
            m.highlight.v += (key == SDLK_DOWN ? 4 : -4);  // bump at the ends
        }
        return MenuResult::kNone;
    }
    case SDLK_RETURN:
        m.hide();
        return MenuResult::kChosen;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
    case SDLK_LEFT:
        m.hide();
        return MenuResult::kClosed;
    default: return MenuResult::kNone;
    }
}

void Home::menu_pointer(Menu &m, float x, float y) {
    if (!inside(m.rect, x, y)) return;
    float header = m.title.id ? kMenuTitleH : 0;
    int i = int(std::floor((y - m.rect.y - header - kMenuPad) / kMenuRowH));
    if (i >= 0 && i < int(m.rows.size()) && i != m.focus) {
        m.focus = i;
        m.highlight.target = float(i);
    }
}

void Home::draw_menu(Painter &p, Menu &m, float x, float y, float width, float alpha) {
    float a = std::clamp(m.anim.x, 0.f, 1.f) * alpha;
    if (a < 0.01f) return;
    // Drops in a little as it opens, on whole pixels: text inside is rounded to
    // pixels too, and a sub-pixel offset while the spring settles made it flicker
    // between two rows.
    float header = m.title.id ? kMenuTitleH : 0;
    Rect panel{x, std::round(y - 18 * (1 - a)), width, header + 2 * kMenuPad + m.rows.size() * kMenuRowH};
    m.rect = panel;
    p.shadow(panel, kMenuRadius, 40, rgb(0x000000, 0.45f), a);
    p.glass(panel, kMenuRadius, rgb(0x0b1016, 0.45f), a);
    if (m.title.id) {
        p.text(std::round(panel.x + kMenuPad + 26), std::round(panel.y + (header - m.title.h) / 2 + 8), m.title,
               rgb(0xffffff), a);
    }
    float top = panel.y + header;

    Rect highlight{panel.x + kMenuPad, top + kMenuPad + m.highlight.x * kMenuRowH, panel.w - 2 * kMenuPad,
                   kMenuRowH};
    p.fill(highlight, kMenuRadius - kMenuPad, rgb(0xffffff), rgb(0xffffff), a);

    for (size_t i = 0; i < m.rows.size(); i++) {
        const Menu::Row &row = m.rows[i];
        float ry = top + kMenuPad + i * kMenuRowH;
        // How much the highlight covers this row: text turns dark under it.
        float lit = std::clamp(1 - std::abs(m.highlight.x - float(i)), 0.f, 1.f);
        Color text = {1 - 0.9f * lit, 1 - 0.9f * lit, 1 - 0.88f * lit, 1};
        float tx = panel.x + kMenuPad + 26;
        if (row.dot) {
            Rect dot{tx, ry + (kMenuRowH - 12) / 2, 12, 12};
            p.fill(dot, 6, rgb(row.dot_color), rgb(row.dot_color), a);
            tx += dot.w + 18;
        }
        p.text(std::round(tx), std::round(ry + (kMenuRowH - row.label.h) / 2), row.label, text, a);
        float right = panel.x + panel.w - kMenuPad - 26;
        if (row.thumb.id) {
            Rect thumb{right - 96, ry + (kMenuRowH - 54) / 2, 96, 54};
            p.image(thumb, 8, row.thumb, {1, 1, 1, 1}, a);
            right = thumb.x - 18;
        }
        if (row.detail.id) {
            Color dim = {text.r, text.g, text.b, 0.6f};
            p.text(std::round(right - row.detail.w), std::round(ry + (kMenuRowH - row.detail.h) / 2), row.detail,
                   dim, a);
        }
    }
}

// --- Frame -----------------------------------------------------------------

void Home::refresh_clock() {
    std::time_t now = std::time(nullptr);
    std::tm local;
    localtime_r(&now, &local);
    if (local.tm_min == clock_minute_) return;
    clock_minute_ = local.tm_min;
    night_.target = night_now() ? 1 : 0;
    char text[16];
    std::strftime(text, sizeof text, "%-I:%M %p", &local);
    destroy(clock_);
    clock_ = render_text(pill_font_, text);
}

void Home::update(float dt) {
    time_ += dt;
    icons_.upload_ready();

    if (holding_ && !held_long_) {
        hold_time_ += dt;
        int row, col;
        bool on_focus = !hold_pointer_ || (hit_tile(px_, py_, &row, &col) && row == row_ && col == col_);
        if (hold_time_ >= kLongPress && on_focus) {
            held_long_ = true;
            long_press();
        }
    }
    if (rehover_ && std::abs(scroll_.x - scroll_.target) < 2 && std::abs(scroll_.v) < 20) {
        rehover_ = false;
        pointer_move(px_, py_);
    }

    for (int i = 0; i < int(tiles_.size()); i++) {
        Tile &t = tiles_[i];
        int row, col;
        index_to_slot(i, &row, &col);
        Rect slot = tile_rect(row, col);
        t.x.target = slot.x;
        t.y.target = slot.y;
        t.x.step(dt, kSlideStiffness, kSlideDamping);
        t.y.step(dt, kSlideStiffness, kSlideDamping);

        // While the pointer rests on the focused tile, it leans toward it.
        float lx = 0, ly = 0;
        bool menus = tile_menu_.open || inputs_menu_.open || settings_.open;
        if (pointer_ && !menus && !moving_ && row == row_ && col == col_) {
            Rect r = slot;
            r.y -= scroll_.x;
            if (inside(r, px_, py_)) {
                lx = std::clamp((px_ - (r.x + r.w / 2)) / (r.w / 2), -1.f, 1.f) * 0.8f;
                ly = std::clamp((py_ - (r.y + r.h / 2)) / (r.h / 2), -1.f, 1.f) * 0.8f;
            }
        }
        t.lean_x.target = lx;
        t.lean_y.target = ly;
        t.focus.step(dt, kFocusStiffness, kFocusDamping);
        t.lean_x.step(dt, kLeanStiffness, kLeanDamping);
        t.lean_y.step(dt, kLeanStiffness, kLeanDamping);
    }
    for (Spring &s : pill_focus_) s.step(dt, kFocusStiffness, kFocusDamping);
    for (Menu *m : {&inputs_menu_, &tile_menu_, &settings_}) {
        m->anim.step(dt, kFocusStiffness, kFocusDamping);
        m->highlight.step(dt, kFocusStiffness, kFocusDamping);
    }
    wiggle_.step(dt, kFocusStiffness, kFocusDamping);
    night_.step(dt, kScrollStiffness * 0.3f, kScrollDamping * 0.6f);  // a slow fade
    scroll_.step(dt, kScrollStiffness, kScrollDamping);
    grid_.step(dt, kScrollStiffness, kScrollDamping);
    refresh_clock();
}

void Home::draw_tile(Painter &p, int index) {
    Tile &tile = tiles_[index];
    int row, col;
    index_to_slot(index, &row, &col);
    float f = std::max(0.f, tile.focus.x);
    Rect r{tile.x.x, tile.y.x - scroll_.x, kTileW, kTileH};
    if (r.y > h_ + 40 || r.y + r.h < -80) return;
    bool picked = moving_ && row == row_ && col == col_;
    float k = 1 + kFocusScale * f + (picked ? 0.04f * wiggle_.x : 0);
    Rect s = r.scaled(k);
    s.x += tile.lean_x.x * kLeanPixels * f;
    s.y += tile.lean_y.x * kLeanPixels * f;
    if (picked) p.set_rotation(kWiggleAngle * wiggle_.x * std::sin(time_ * kWiggleSpeed));

    if (f > 0.01f) {
        Rect shadow = s;
        shadow.y += 16 * f;
        p.shadow(shadow, kRadius, 30, rgb(0x000000, 0.55f), f);
    }
    // The shine moves with the lean and rests once the tile is still.
    float sx = kShineX + tile.lean_x.x * kShineTravel, sy = kShineY + tile.lean_y.x * kShineTravel;
    uint32_t color = tile.app.color;
    bool seamless = icons_.edge_color(tile.app.icon, &color);
    // A gentle vertical gradient, flat where it has to meet the icon's edge.
    Color top = seamless ? rgb(color) : shade(color, 1.15f, 0.06f);
    Color bottom = seamless ? rgb(color) : shade(color, 0.85f);
    float radius = kRadius * k;
    p.fill(s, radius, top, bottom);

    Texture icon = icons_.get(tile.app.icon);
    if (icon.id) {
        float ih = kIconSize * k, iw = ih * icon.w / icon.h;
        Rect r_icon{s.x + (s.w - iw) / 2, s.y + (s.h - ih) / 2, iw, ih};
        p.image(r_icon, kIconRadius * k, icon);
    } else {
        Rect name{s.x + (s.w - tile.name.w * k) / 2, s.y + (s.h - tile.name.h * k) / 2, tile.name.w * k,
                  tile.name.h * k};
        p.image(name, 0, tile.name, rgb(0xffffff));
    }
    // Over the icon too, so the whole tile catches the light like a sheet of glass.
    p.shine(s, radius, sx, sy, 0.24f * f);
    p.set_rotation(0);

    if (row >= 1) {
        p.text(std::round(r.x + (r.w - tile.label.w) / 2), std::round(s.y + s.h + 14), tile.label,
               rgb(0xffffff, 0.95f), f);
    }
}

void Home::render(Painter &p) {
    float g = std::clamp(grid_.x, 0.f, 1.f);
    Rect screen{0, 0, float(w_), float(h_)};

    float n = std::clamp(night_.x, 0.f, 1.f);
    p.set_backdrop(wallpaper_.blurred);
    // Wallpaper, crossfading to its blurred copy and dimming in the grid, and
    // at night.
    p.image(screen, 0, wallpaper_.sharp);
    p.image(screen, 0, wallpaper_.blurred, {1, 1, 1, 1}, g);
    p.fill(screen, 0, rgb(0x050a1a, 0.45f), rgb(0x050a1a, 0.55f), n);

    // Glass dock behind the first row, dimmed with the wallpaper in the grid;
    // smoky rather than milky at night.
    Rect first = tile_rect(0, 0), last = tile_rect(0, kCols - 1);
    Rect dock{first.x - kDockPad, first.y - kDockPad - scroll_.x, last.x + last.w - first.x + 2 * kDockPad,
              kTileH + 2 * kDockPad};
    p.shadow(dock, 46, 40, rgb(0x000000, 0.25f));
    Color day_glass = rgb(0xffffff, 0.12f), night_glass = rgb(0x0b1016, 0.40f);
    Color glass{day_glass.r + (night_glass.r - day_glass.r) * n, day_glass.g + (night_glass.g - day_glass.g) * n,
                day_glass.b + (night_glass.b - day_glass.b) * n, day_glass.a + (night_glass.a - day_glass.a) * n};
    p.glass(dock, 46, glass);
    p.fill(screen, 0, rgb(0x000000, 0.35f), rgb(0x000000, 0.45f), g);

    // Unfocused tiles first, so the focused one is drawn over its neighbours.
    int focused = row_ == kPill || tiles_.empty() ? -1 : tile_index(row_, col_);
    for (int i = 0; i < int(tiles_.size()); i++) {
        if (i != focused) draw_tile(p, i);
    }
    if (focused >= 0) draw_tile(p, focused);
    // No apps: why, in the middle of the dock.
    if (tiles_.empty() && status_.id) {
        p.text(std::round(dock.x + (dock.w - status_.w) / 2), std::round(dock.y + (dock.h - status_.h) / 2), status_,
               rgb(0xffffff), 1 - g);
    }

    // Status pill: clock, the Inputs button, the Control Center and the
    // settings button, fading out in the grid.
    float alpha = 1 - g;
    float pad = 26, gap = 22, height = 64, bh = height - 12;
    Rect inputs_button{0, 0, inputs_label_.w + 2 * pad, bh}, control_button{0, 0, bh, bh}, gear_button{0, 0, bh, bh};
    float width = pad + clock_.w + gap + inputs_button.w + 6 + control_button.w + 6 + gear_button.w + 6;
    Rect pill{1920 - kLeft + kDockPad - width, 64, width, height};
    gear_button.x = pill.x + pill.w - gear_button.w - 6;
    control_button.x = gear_button.x - 6 - control_button.w;
    inputs_button.x = control_button.x - 6 - inputs_button.w;
    gear_button.y = control_button.y = inputs_button.y = pill.y + 6;
    // Darker glass than the dock: the top of a photo is often bright sky or sand.
    p.glass(pill, height / 2, rgb(0x0b1016, 0.38f), alpha);
    p.text(std::round(pill.x + pad), std::round(pill.y + (height - clock_.h) / 2), clock_, rgb(0xffffff), alpha);

    Texture glyphs[kPillButtons] = {{}, icons_.get(assets_ + "/icons/control.png"),
                                    icons_.get(assets_ + "/icons/settings.png")};
    Rect buttons[kPillButtons] = {inputs_button, control_button, gear_button};
    for (int i = 0; i < kPillButtons; i++) {
        button_rect_[i] = buttons[i];
        float pf = std::max(0.f, pill_focus_[i].x);
        Rect b = buttons[i].scaled(1 + 0.08f * pf);
        if (pf > 0.01f) p.shadow(b, b.h / 2, 18, rgb(0x000000, 0.4f), pf * alpha);
        p.fill(b, b.h / 2, rgb(0xffffff, 0.10f + 0.90f * pf), rgb(0xffffff, 0.10f + 0.90f * pf), alpha);
        // White on glass, dark on the focused button.
        Color ink = {1 - 0.9f * pf, 1 - 0.9f * pf, 1 - 0.88f * pf, 1};
        if (i == 0) {
            p.text(std::round(b.x + (b.w - inputs_label_.w) / 2), std::round(b.y + (b.h - inputs_label_.h) / 2),
                   inputs_label_, ink, alpha);
        } else if (glyphs[i].id) {
            p.image(b.inset(b.w * 0.2f), 0, glyphs[i], ink, alpha);
        }
    }

    // Menus over everything, with the screen dimmed behind them.
    float dim = std::max({std::clamp(inputs_menu_.anim.x, 0.f, 1.f), std::clamp(tile_menu_.anim.x, 0.f, 1.f),
                          std::clamp(settings_.anim.x, 0.f, 1.f)});
    if (dim > 0.01f) {
        p.fill(screen, 0, rgb(0x000000, 0.30f), rgb(0x000000, 0.30f), dim);
        // The tile being edited stays bright above the dimming.
        if (tile_menu_.anim.x > 0.01f && focused >= 0) draw_tile(p, focused);
    }
    draw_menu(p, inputs_menu_, pill.x + pill.w - kInputsW, pill.y + pill.h + 14, kInputsW, 1);
    if (focused >= 0) {
        // Below the tile, or above it when there's no room.
        Rect t{tiles_[focused].x.x, tiles_[focused].y.x - scroll_.x, kTileW, kTileH};
        float mh = 2 * kMenuPad + tile_menu_.rows.size() * kMenuRowH;
        int row, col;
        index_to_slot(focused, &row, &col);
        float my = t.y + t.h + (row >= 1 ? 72 : 28);  // clear of the caption in the grid
        if (my + mh > h_ - 20) my = t.y - 28 - mh;
        draw_menu(p, tile_menu_, t.x, my, kTileMenuW, 1);
    }
    // Drops from the corner, like Inputs.
    draw_menu(p, settings_, pill.x + pill.w - kSettingsW, pill.y + pill.h + 14, kSettingsW, 1);
}

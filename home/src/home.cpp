#include "home.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <ctime>

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

// Springs: tvOS motion is quick but not bouncy.
constexpr float kFocusStiffness = 260, kFocusDamping = 26;
constexpr float kScrollStiffness = 140, kScrollDamping = 24;
// Lean: the focused tile's small slide in the direction of travel, which also
// carries the shine. Slightly underdamped so it settles with life.
constexpr float kLeanStiffness = 200, kLeanDamping = 17;
constexpr float kLeanPixels = 7;
// Where the shine rests once the tile is still (0..1 of the tile), and how far
// a full lean moves it.
constexpr float kShineX = 0.5f, kShineY = -0.05f, kShineTravel = 0.55f;

Color shade(uint32_t hex, float k, float lift = 0) {
    Color c = rgb(hex);
    return {std::min(1.f, c.r * k + lift), std::min(1.f, c.g * k + lift), std::min(1.f, c.b * k + lift), 1};
}

}  // namespace

void Spring::step(float dt, float stiffness, float damping) {
    // Small fixed substeps keep the integration stable at any frame time.
    for (float left = dt; left > 0; left -= 1.f / 240) {
        float h = std::min(left, 1.f / 240);
        v += ((target - x) * stiffness - v * damping) * h;
        x += v * h;
    }
}

bool Home::init(int w, int h, const char *font_path, const std::string &assets) {
    w_ = w;
    h_ = h;
    wallpaper_ = load_wallpaper(assets + "/wallpapers/default.jpg", w, h);

    tile_font_ = TTF_OpenFont(font_path, 30);
    label_font_ = TTF_OpenFont(font_path, 26);
    pill_font_ = TTF_OpenFont(font_path, 28);
    if (!tile_font_ || !label_font_ || !pill_font_) {
        LOG("font %s: %s", font_path, TTF_GetError());
        return false;
    }

    for (App &app : placeholder_apps()) {
        Tile t;
        t.app = app;
        t.name = render_text(tile_font_, app.title);
        t.label = render_text(label_font_, app.title);
        tiles_.push_back(std::move(t));
    }
    inputs_ = render_text(pill_font_, "Inputs");
    refresh_clock();
    move_focus(0, 0, 0, 0);
    for (Tile &t : tiles_) t.focus.snap(t.focus.target);
    return true;
}

int Home::dock_count() const { return std::min<int>(kCols, int(tiles_.size())); }

int Home::row_count() const {
    int grid = int(tiles_.size()) - dock_count();
    return 1 + (grid + kCols - 1) / kCols;
}

int Home::row_length(int row) const {
    if (row == kPill) return 1;
    if (row == 0) return dock_count();
    int first = dock_count() + (row - 1) * kCols;
    return std::clamp(int(tiles_.size()) - first, 0, kCols);
}

int Home::tile_index(int row, int col) const {
    return row == 0 ? col : dock_count() + (row - 1) * kCols + col;
}

Rect Home::tile_rect(int row, int col) const {
    float y = row == 0 ? kDockTop : kGridTop + (row - 1) * kRowPitch;
    return {kLeft + col * (kTileW + kGapX), y, kTileW, kTileH};
}

void Home::move_focus(int row, int col, int dx, int dy) {
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

    for (size_t i = 0; i < tiles_.size(); i++) tiles_[i].focus.target = 0;
    if (row != kPill) tiles_[tile_index(row, col)].focus.target = 1;
    pill_focus_.target = row == kPill ? 1 : 0;

    // The first grid row rises to y 300; later rows keep the focused row in view.
    float target = 0;
    if (row >= 1) {
        float top = kGridTop + (row - 1) * kRowPitch;
        target = std::max(kGridTop - 300, top - 540);
    }
    scroll_.target = target;
    grid_.target = row >= 1 ? 1 : 0;
}

void Home::key(SDL_Keycode key) {
    switch (key) {
    case SDLK_LEFT: move_focus(row_, col_ - 1, -1, 0); break;
    case SDLK_RIGHT: move_focus(row_, col_ + 1, 1, 0); break;
    case SDLK_UP: move_focus(row_ - 1, row_ == 0 ? 0 : col_, 0, -1); break;
    case SDLK_DOWN: move_focus(row_ + 1, row_ == kPill ? dock_col_ : col_, 0, 1); break;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        if (row_ >= 1) move_focus(0, dock_col_, 0, -1);
        break;
    case SDLK_RETURN:
        if (row_ == kPill) LOG("home: open inputs");
        else LOG("home: launch %s", tiles_[tile_index(row_, col_)].app.id.c_str());
        break;
    default: LOG("home: key %d", int(key)); break;
    }
}

void Home::refresh_clock() {
    std::time_t now = std::time(nullptr);
    std::tm local;
    localtime_r(&now, &local);
    if (local.tm_min == clock_minute_) return;
    clock_minute_ = local.tm_min;
    char text[16];
    std::strftime(text, sizeof text, "%-I:%M %p", &local);
    destroy(clock_);
    clock_ = render_text(pill_font_, text);
}

void Home::update(float dt) {
    for (Tile &t : tiles_) {
        t.focus.step(dt, kFocusStiffness, kFocusDamping);
        t.lean_x.step(dt, kLeanStiffness, kLeanDamping);
        t.lean_y.step(dt, kLeanStiffness, kLeanDamping);
    }
    pill_focus_.step(dt, kFocusStiffness, kFocusDamping);
    scroll_.step(dt, kScrollStiffness, kScrollDamping);
    grid_.step(dt, kScrollStiffness, kScrollDamping);
    refresh_clock();
}

void Home::draw_tile(Painter &p, int row, int col) {
    Tile &tile = tiles_[tile_index(row, col)];
    float f = std::max(0.f, tile.focus.x);
    Rect r = tile_rect(row, col);
    r.y -= scroll_.x;
    if (r.y > h_ + 40 || r.y + r.h < -80) return;
    Rect s = r.scaled(1 + kFocusScale * f);
    s.x += tile.lean_x.x * kLeanPixels * f;
    s.y += tile.lean_y.x * kLeanPixels * f;

    if (f > 0.01f) {
        Rect shadow = s;
        shadow.y += 16 * f;
        p.shadow(shadow, kRadius, 30, rgb(0x000000, 0.55f), f);
    }
    // The shine moves with the lean and rests once the tile is still.
    float sx = kShineX + tile.lean_x.x * kShineTravel, sy = kShineY + tile.lean_y.x * kShineTravel;
    p.fill_shine(s, kRadius * (1 + kFocusScale * f), shade(tile.app.color, 1.15f, 0.06f),
                 shade(tile.app.color, 0.85f), sx, sy, 0.22f * f);

    float k = 1 + kFocusScale * f;
    Rect name{s.x + (s.w - tile.name.w * k) / 2, s.y + (s.h - tile.name.h * k) / 2, tile.name.w * k,
              tile.name.h * k};
    p.image(name, 0, tile.name, rgb(0xffffff));

    if (row >= 1) {
        p.text(std::round(r.x + (r.w - tile.label.w) / 2), std::round(s.y + s.h + 14), tile.label,
               rgb(0xffffff, 0.95f), f);
    }
}

void Home::render(Painter &p) {
    float g = std::clamp(grid_.x, 0.f, 1.f);
    Rect screen{0, 0, float(w_), float(h_)};

    p.set_backdrop(wallpaper_.blurred);
    // Wallpaper, crossfading to its blurred copy and dimming in the grid.
    p.image(screen, 0, wallpaper_.sharp);
    p.image(screen, 0, wallpaper_.blurred, {1, 1, 1, 1}, g);
    p.fill(screen, 0, rgb(0x000000, 0.35f), rgb(0x000000, 0.45f), g);

    // Glass dock behind the first row.
    Rect first = tile_rect(0, 0), last = tile_rect(0, kCols - 1);
    Rect dock{first.x - kDockPad, first.y - kDockPad - scroll_.x, last.x + last.w - first.x + 2 * kDockPad,
              kTileH + 2 * kDockPad};
    p.shadow(dock, 46, 40, rgb(0x000000, 0.25f));
    p.glass(dock, 46, rgb(0xffffff, 0.12f));

    // Unfocused tiles first, so the focused one is drawn over its neighbours.
    for (int pass = 0; pass < 2; pass++) {
        for (int row = 0; row < row_count(); row++) {
            for (int col = 0; col < row_length(row); col++) {
                bool focused = row == row_ && col == col_;
                if (focused == (pass == 1)) draw_tile(p, row, col);
            }
        }
    }

    // Status pill: clock and the Inputs button, fading out in the grid.
    float pf = std::max(0.f, pill_focus_.x), alpha = 1 - g;
    float pad = 26, gap = 22, height = 64;
    Rect button{0, 0, inputs_.w + 2 * pad, height - 12};
    float width = pad + clock_.w + gap + button.w + 6;
    Rect pill{1920 - kLeft + kDockPad - width, 64, width, height};
    button.x = pill.x + pill.w - button.w - 6;
    button.y = pill.y + 6;
    // Darker glass than the dock: the top of a photo is often bright sky or sand.
    p.glass(pill, height / 2, rgb(0x0b1016, 0.38f), alpha);
    p.text(std::round(pill.x + pad), std::round(pill.y + (height - clock_.h) / 2), clock_, rgb(0xffffff), alpha);

    Rect b = button.scaled(1 + 0.08f * pf);
    if (pf > 0.01f) p.shadow(b, b.h / 2, 18, rgb(0x000000, 0.4f), pf * alpha);
    p.fill(b, b.h / 2, rgb(0xffffff, 0.10f + 0.90f * pf), rgb(0xffffff, 0.10f + 0.90f * pf), alpha);
    Color label = {1 - 0.9f * pf, 1 - 0.9f * pf, 1 - 0.88f * pf, 1};
    p.text(std::round(b.x + (b.w - inputs_.w) / 2), std::round(b.y + (b.h - inputs_.h) / 2), inputs_, label, alpha);
}

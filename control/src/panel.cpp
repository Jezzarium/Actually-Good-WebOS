#include "panel.h"

#include "log.h"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>

#include <sys/stat.h>

namespace {

// Drops from where Home's status pill ends (its right edge and bottom).
constexpr float kPanelW = 600, kPanelRight = 1839, kPanelTop = 142, kPanelRadius = 30;
constexpr float kMaxHeight = 1080 - kPanelTop - 48;
constexpr float kPad = 12, kGap = 12, kCellRadius = 18;
constexpr float kInner = kPanelW - 2 * kPad;
constexpr float kSliderH = 84, kTileH = 96, kButtonH = 104, kRowH = 72, kHeaderH = 72;
// Focused cells grow by this much on each side, and their corners with them,
// so they stay concentric with the panel's.
constexpr float kGrow = 4;
constexpr float kIcon = 34, kTextLeft = 26;

// The home screen's springs: quick, not bouncy.
constexpr float kFocusStiffness = 260, kFocusDamping = 26;
constexpr float kSlideStiffness = 300, kSlideDamping = 30;
constexpr float kToastTime = 3;

const std::vector<std::string> kQuickTiles = {"pictureMode", "eyeComfortMode", "soundMode",
                                              "soundOutput", "sleepTimer", "energySaving"};

struct Section {
    const char *title;
    std::vector<std::string> ids;
    const char *lg_target;  // LG settings page for "Open in LG settings", or null
};
const std::vector<Section> &sections() {
    static const std::vector<Section> all = {
        {"Picture",
         {"pictureMode", "backlight", "contrast", "brightness", "color", "sharpness", "colorTemperature",
          "peakBrightness", "hdrDynamicToneMapping", "gamma", "dynamicContrast", "superResolution", "noiseReduction",
          "truMotionMode", "realCinema", "motionEyeCare", "eyeComfortMode", "ai_Brightness", "aspectRatio"},
         "picture"},
        {"Sound", {"soundMode", "soundOutput", "autoVolume"}, "sound"},
        {"OLED care & power", {"energySaving", "screenShift", "logoLuminanceAdjust", "quickStartMode", "standByLight"},
         nullptr},
        {"General", {"sleepTimer", "screenSaverTimer"}, nullptr},
        {"Accessibility", {"highContrast", "greyscale", "menuTransparency", "audioGuidance"}, nullptr},
    };
    return all;
}
const Section *section(const std::string &title) {
    for (const Section &s : sections()) {
        if (title == s.title) return &s;
    }
    return nullptr;
}

const char *kLgSettings = "com.palm.app.settings";

// Home's blurred wallpaper (home/src/home.cpp saves it), behind the glass. An
// overlay can't see what's below it, so over video it is the wallpaper still.
const char *kBackdrop = "/tmp/lgtv-home-backdrop.bmp";

bool inside(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// White on the dark panel, near-black where the white focus fill is under it.
Color ink(float lit, float a = 1) { return {1 - 0.9f * lit, 1 - 0.9f * lit, 1 - 0.88f * lit, a}; }

// Shortens s with an ellipsis until it fits in width pixels.
std::string fit(TTF_Font *font, std::string s, float width) {
    int w = 0;
    if (TTF_SizeUTF8(font, s.c_str(), &w, nullptr) == 0 && w <= width) return s;
    while (!s.empty()) {
        // Drop one UTF-8 character, then any spaces it leaves at the end.
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xc0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
        while (!s.empty() && s.back() == ' ') s.pop_back();
        std::string t = s + "\u2026";
        if (TTF_SizeUTF8(font, t.c_str(), &w, nullptr) == 0 && w <= width) return t;
    }
    return "\u2026";
}

}  // namespace

bool Panel::init(const std::string &font_dir, const std::string &icons_dir, Tv *tv, Bluetooth *bt) {
    tv_ = tv;
    bt_ = bt;
    icons_dir_ = icons_dir;
    std::string semibold = font_dir + "/LG_Smart_UI-SemiBold.ttf", regular = font_dir + "/LG_Smart_UI-Regular.ttf";
    label_font_ = TTF_OpenFont(semibold.c_str(), 28);
    value_font_ = TTF_OpenFont(semibold.c_str(), 26);
    title_font_ = TTF_OpenFont(semibold.c_str(), 30);
    small_font_ = TTF_OpenFont(regular.c_str(), 22);
    if (!label_font_ || !value_font_ || !title_font_ || !small_font_) {
        LOG("panel: fonts in %s: %s", font_dir.c_str(), TTF_GetError());
        return false;
    }
    for (const char *name : {"sun", "picture", "eye", "sound", "speaker", "moon", "leaf", "power", "more", "gear",
                             "chevron", "back", "check", "bluetooth", "headphones", "search"}) {
        icon(name);
    }
    load_backdrop();
    stack_.push_back(std::make_unique<Page>());
    build(*stack_.back());
    height_.snap(stack_.back()->height);
    return true;
}

Texture Panel::icon(const std::string &name) {
    auto it = icons_.find(name);
    if (it != icons_.end()) return it->second;
    Texture t;
    if (SDL_Surface *s = IMG_Load((icons_dir_ + "/" + name + ".png").c_str())) {
        if (SDL_Surface *rgba = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0)) {
            if (rgba->pitch == rgba->w * 4) t = upload_rgba(rgba->pixels, rgba->w, rgba->h);
            SDL_FreeSurface(rgba);
        }
        SDL_FreeSurface(s);
    } else {
        LOG("panel: icon %s: %s", name.c_str(), IMG_GetError());
    }
    return icons_[name] = t;
}

Texture Panel::text(TTF_Font *font, const std::string &s) { return s.empty() ? Texture{} : render_text(font, s); }

// --- Pages -----------------------------------------------------------------

void Panel::build(Page &page) {
    for (Cell &c : page.cells) {
        destroy(c.label);
        destroy(c.value);
    }
    destroy(page.title_tex);
    page.cells.clear();
    page.grid.clear();
    page.list = page.kind != Page::kMain;
    page.shape = shape_of(page);

    auto add = [&page](Cell c) {
        page.cells.push_back(std::move(c));
        return int(page.cells.size()) - 1;
    };
    auto setting_cell = [this](Type type, const std::string &id) {
        const Item *item = Tv::item(id);
        Cell c;
        c.type = type;
        c.key = id;
        c.id = id;
        c.icon = item->icon;
        c.label_text = item->label;
        c.toggle = item->kind == Kind::kToggle && type == Type::kRow;
        c.chevron = item->kind == Kind::kChoice && type == Type::kRow;
        c.activate = [this, id]() {
            const Item *it = Tv::item(id);
            if (tv_->get(id).disabled) return;
            if (it->kind == Kind::kToggle) tv_->set(id, tv_->get(id).value == "on" ? "off" : "on");
            else if (it->kind == Kind::kChoice) push(Page::kPicker, it->label, id);
        };
        return c;
    };

    if (page.kind == Page::kMain) {
        float y = kPad;
        Cell slider = setting_cell(Type::kSlider, "backlight");
        slider.rect = {kPad, y, kInner, kSliderH};
        page.grid.push_back({add(slider)});
        y += kSliderH + kGap;
        float tile_w = (kInner - kGap) / 2;
        for (size_t i = 0; i < kQuickTiles.size(); i += 2) {
            std::vector<int> row;
            for (size_t j = 0; j < 2; j++) {
                Cell tile = setting_cell(Type::kTile, kQuickTiles[i + j]);
                tile.rect = {kPad + j * (tile_w + kGap), y, tile_w, kTileH};
                row.push_back(add(tile));
            }
            page.grid.push_back(row);
            y += kTileH + kGap;
        }
        struct Action {
            const char *key, *label, *icon;
            std::function<void()> run;
        };
        std::vector<Action> actions = {
            {"screenOff", "Screen off", "power",
             [this]() { tv_->screen_off([this](bool ok) { if (ok && on_close) on_close(); }); }},
            {"more", "More", "more", [this]() { push(Page::kMore, "More settings"); }},
            {"lg", "All settings", "gear",
             [this]() { tv_->launch(kLgSettings, "", [this](bool ok) { if (ok && on_close) on_close(); }); }},
        };
        float button_w = (kInner - 2 * kGap) / 3;
        std::vector<int> row;
        for (size_t i = 0; i < actions.size(); i++) {
            Cell b;
            b.type = Type::kButton;
            b.key = actions[i].key;
            b.icon = actions[i].icon;
            b.label_text = actions[i].label;
            b.activate = actions[i].run;
            b.rect = {kPad + i * (button_w + kGap), y, button_w, kButtonH};
            row.push_back(add(b));
        }
        page.grid.push_back(row);
        page.height = y + kButtonH + kPad;
    } else {
        Cell header;
        header.type = Type::kHeader;
        header.key = "header";
        header.icon = "back";
        header.label_text = page.title;
        header.activate = [this]() { pop(); };
        header.rect = {kPad, kPad, kInner, kHeaderH};
        page.grid.push_back({add(header)});
        float y = kPad + kHeaderH;
        auto row = [&](Cell c) {
            c.rect = {kPad, y, kInner, kRowH};
            y += kRowH;
            page.grid.push_back({add(std::move(c))});
        };
        auto action_row = [](const std::string &key, const std::string &label, const std::string &icon_name,
                             std::function<void()> run, bool chevron) {
            Cell c;
            c.key = key;
            c.label_text = label;
            c.icon = icon_name;
            c.activate = std::move(run);
            c.chevron = chevron;
            return c;
        };

        if (page.kind == Page::kPicker) {
            const std::string id = page.arg;
            for (const std::string &option : tv_->options(id)) {
                Cell c;
                c.key = "option:" + option;
                c.label_text = tv_->option_text(id, option);
                c.check = option == tv_->get(id).value;
                c.activate = [this, id, option]() {
                    tv_->set(id, option);
                    pop();
                };
                row(c);
            }
            if (id == "soundOutput") {
                row(action_row("bluetooth", "Bluetooth devices", "bluetooth",
                               [this]() { push(Page::kBluetooth, "Bluetooth"); }, true));
            }
        } else if (page.kind == Page::kMore) {
            for (const Section &s : sections()) {
                std::string title = s.title;
                row(action_row("section:" + title, title, "", [this, title]() { push(Page::kSection, title, title); },
                               true));
            }
        } else if (page.kind == Page::kSection) {
            const Section *s = section(page.arg);
            for (const std::string &id : s ? s->ids : std::vector<std::string>{}) {
                if (!tv_->visible(id)) continue;
                Cell c = setting_cell(Tv::item(id)->kind == Kind::kRange ? Type::kSlider : Type::kRow, id);
                c.icon.clear();
                row(c);
            }
            if (s && s->lg_target) {
                std::string params = std::string("{\"target\":\"") + s->lg_target + "\"}";
                row(action_row("lg", "Open in LG settings", "gear", [this, params]() {
                    tv_->launch(kLgSettings, params, [this](bool ok) { if (ok && on_close) on_close(); });
                }, true));
            }
        } else if (page.kind == Page::kBluetooth) {
            for (const BtDevice &d : bt_->devices()) {
                BtDevice device = d;
                Cell c;
                c.key = "bt:" + d.address;
                c.icon = "headphones";
                c.label_text = d.name;
                c.value_text = bt_->busy(d.address) ? (d.paired ? "Connecting…" : "Pairing…")
                               : d.connected          ? "Connected"
                               : d.paired             ? "Not connected"
                                                      : "Pair";
                c.activate = [this, device]() {
                    if (device.connected) bt_->disconnect(device);
                    else bt_->connect(device);
                };
                row(c);
            }
            bool searching = bt_->searching();
            row(action_row("search", searching ? "Searching…" : "Search for devices", "search",
                           [this]() { bt_->search(!bt_->searching()); }, false));
        }
        page.height = y + kPad;
    }

    for (Cell &c : page.cells) refresh(c);
    page.row = std::clamp(page.row, 0, std::max(0, int(page.grid.size()) - 1));
    if (!page.grid.empty()) page.col = std::clamp(page.col, 0, int(page.grid[page.row].size()) - 1);
}

std::string Panel::shape_of(const Page &page) const {
    std::string shape;
    switch (page.kind) {
    case Page::kMain:
    case Page::kMore: break;
    case Page::kPicker:
        shape = tv_->get(page.arg).value;
        for (const std::string &o : tv_->options(page.arg)) shape += "|" + o;
        break;
    case Page::kSection:
        if (const Section *s = section(page.arg)) {
            for (const std::string &id : s->ids) shape += tv_->visible(id) ? "1" : "0";
        }
        break;
    case Page::kBluetooth:
        shape = bt_->searching() ? "s" : "";
        for (const BtDevice &d : bt_->devices()) {
            shape += "|" + d.address + d.name + (d.paired ? "p" : "") + (d.connected ? "c" : "") +
                     (bt_->busy(d.address) ? "b" : "");
        }
        break;
    }
    return shape;
}

void Panel::rebuild(Page &page) {
    std::string focus_key;
    std::map<std::string, Spring> springs;
    if (!page.grid.empty()) focus_key = page.cells[page.grid[page.row][page.col]].key;
    for (Cell &c : page.cells) springs[c.key] = c.focus;
    build(page);
    for (size_t r = 0; r < page.grid.size(); r++) {
        for (size_t c = 0; c < page.grid[r].size(); c++) {
            if (page.cells[page.grid[r][c]].key == focus_key) page.row = int(r), page.col = int(c);
        }
    }
    for (Cell &c : page.cells) {
        auto it = springs.find(c.key);
        if (it != springs.end()) c.focus = it->second;
    }
    set_focus(page.row, page.col);
    scroll_into_view(page, false);
}

void Panel::refresh(Cell &cell) {
    if (!cell.label.id && !cell.label_text.empty()) {
        float room = cell.rect.w - 2 * kTextLeft;
        if (cell.type == Type::kButton) room = cell.rect.w - 20;
        if (cell.type == Type::kTile) room = cell.rect.w - kTextLeft - kIcon - 18 - 16;
        if (!cell.icon.empty() && cell.type == Type::kRow) room -= kIcon + 18;
        if (cell.type == Type::kRow && (cell.chevron || !cell.value_text.empty() || !cell.id.empty())) room *= 0.6f;
        TTF_Font *font = cell.type == Type::kTile     ? small_font_
                         : cell.type == Type::kHeader ? title_font_
                         : cell.type == Type::kButton ? value_font_
                                                      : label_font_;
        cell.label = text(font, fit(font, cell.label_text, room));
    }
    std::string value = cell.value_text;
    if (!cell.id.empty()) {
        const Setting &s = tv_->get(cell.id);
        cell.disabled = s.disabled;
        cell.toggle_on = s.value == "on";
        if (!cell.toggle) value = tv_->text(cell.id);
    }
    if (cell.type == Type::kButton || cell.type == Type::kHeader || cell.toggle || cell.check) value.clear();
    // Re-render only when the text changed (sliders change it on every step).
    if (cell.value.id && cell.shown == value) return;
    destroy(cell.value);
    cell.shown = value;
    if (value.empty()) return;
    float room = cell.type == Type::kTile ? cell.rect.w - kTextLeft - kIcon - 18 - 16 : cell.rect.w * 0.45f;
    cell.value = text(value_font_, fit(value_font_, value, room));
}

void Panel::push(Page::Kind kind, const std::string &title, const std::string &arg) {
    auto page = std::make_unique<Page>();
    page->kind = kind;
    page->title = title;
    page->arg = arg;
    if (kind == Page::kSection) {
        if (const Section *s = section(arg)) tv_->load(s->ids);
    } else if (kind == Page::kPicker) {
        tv_->load({arg});
    } else if (kind == Page::kBluetooth) {
        bt_->set_active(true);
    }
    build(*page);
    // On the first row under the header; a picker on the current value.
    if (page->grid.size() > 1) page->row = 1;
    if (kind == Page::kPicker) {
        for (size_t r = 0; r < page->grid.size(); r++) {
            if (page->cells[page->grid[r][0]].check) page->row = int(r);
        }
    }
    stack_.push_back(std::move(page));
    set_focus(stack_.back()->row, stack_.back()->col);
    scroll_into_view(*stack_.back(), true);
    stack_.back()->highlight.snap(stack_.back()->highlight.target);
    slide_.snap(0);
    slide_.target = 1;
    slide_dir_ = 1;
}

void Panel::pop() {
    if (stack_.size() <= 1) {
        if (on_close) on_close();
        return;
    }
    if (stack_.back()->kind == Page::kBluetooth) bt_->set_active(false);
    stack_.pop_back();
    Page &page = *stack_.back();
    if (shape_of(page) != page.shape) rebuild(page);
    else for (Cell &c : page.cells) refresh(c);
    slide_.snap(0);
    slide_.target = 1;
    slide_dir_ = -1;
}

void Panel::load_backdrop() {
    struct stat st;
    if (stat(kBackdrop, &st) != 0 || st.st_mtime == backdrop_time_) return;
    backdrop_time_ = st.st_mtime;
    SDL_Surface *s = IMG_Load(kBackdrop);
    if (!s) {
        LOG("panel: %s: %s", kBackdrop, IMG_GetError());
        return;
    }
    if (SDL_Surface *rgba = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0)) {
        if (rgba->pitch == rgba->w * 4) {
            destroy(backdrop_);
            backdrop_ = upload_rgba(rgba->pixels, rgba->w, rgba->h);
        }
        SDL_FreeSurface(rgba);
    }
    SDL_FreeSurface(s);
}

void Panel::open() {
    load_backdrop();
    while (stack_.size() > 1) stack_.pop_back();
    bt_->set_active(false);
    Page &main = *stack_.back();
    main.row = main.col = 0;
    for (Cell &c : main.cells) c.focus.snap(0);
    set_focus(0, 0);
    std::vector<std::string> ids = {"backlight"};
    ids.insert(ids.end(), kQuickTiles.begin(), kQuickTiles.end());
    tv_->load(ids);
    open_ = true;
    anim_.snap(0);
    anim_.target = 1;
    height_.snap(main.height);
    slide_.snap(1);
}

void Panel::close() {
    open_ = false;
    anim_.target = 0;
    if (bt_->searching()) bt_->search(false);
    bt_->set_active(false);
}

// --- Focus and keys --------------------------------------------------------

Panel::Cell *Panel::focused() {
    Page &page = *stack_.back();
    if (page.grid.empty()) return nullptr;
    return &page.cells[page.grid[page.row][page.col]];
}

void Panel::set_focus(int row, int col) {
    Page &page = *stack_.back();
    if (page.grid.empty()) return;
    page.row = std::clamp(row, 0, int(page.grid.size()) - 1);
    page.col = std::clamp(col, 0, int(page.grid[page.row].size()) - 1);
    for (Cell &c : page.cells) c.focus.target = 0;
    Cell &f = page.cells[page.grid[page.row][page.col]];
    f.focus.target = 1;
    page.highlight.target = f.rect.y;
}

void Panel::move_focus(int dr, int dc) {
    Page &page = *stack_.back();
    if (page.grid.empty()) return;
    if (dc) {
        int col = page.col + dc;
        if (col >= 0 && col < int(page.grid[page.row].size())) set_focus(page.row, col);
        return;
    }
    int row = page.row + dr;
    if (row < 0 || row >= int(page.grid.size())) {
        page.highlight.v += dr * 300;  // bump at the ends, like Home's menus
        return;
    }
    // The column nearest the focused cell's centre.
    Rect from = page.cells[page.grid[page.row][page.col]].rect;
    float cx = from.x + from.w / 2;
    int best = 0;
    float best_d = 1e9f;
    for (size_t c = 0; c < page.grid[row].size(); c++) {
        Rect r = page.cells[page.grid[row][c]].rect;
        float d = std::abs(r.x + r.w / 2 - cx);
        if (d < best_d) best_d = d, best = int(c);
    }
    set_focus(row, best);
    scroll_into_view(page, false);
}

void Panel::scroll_into_view(Page &page, bool snap) {
    float view_h = std::min(page.height, kMaxHeight);
    float top = page.list ? kPad + kHeaderH : kPad;
    float max_scroll = std::max(0.f, page.height - view_h);
    float target = 0;
    if (max_scroll > 0 && !page.grid.empty()) {
        Rect r = page.cells[page.grid[page.row][page.col]].rect;
        // Keep the focused row in the middle of what's visible below the header.
        float visible = view_h - top - kPad;
        target = std::clamp(r.y + r.h / 2 - (top + visible / 2), 0.f, max_scroll);
    }
    page.scroll.target = target;
    if (snap) page.scroll.snap(target);
}

void Panel::adjust(Cell &cell, int dir) {
    const Setting &s = tv_->get(cell.id);
    if (s.disabled || s.value.empty()) return;
    unsigned now = SDL_GetTicks();
    SDL_Keycode key = dir > 0 ? SDLK_RIGHT : SDLK_LEFT;
    held_count_ = key == held_key_ && now - held_last_ < 250 ? held_count_ + 1 : 0;
    held_key_ = key;
    held_last_ = now;
    // About two seconds from one end to the other when held.
    int accel = held_count_ < 6 ? 1 : held_count_ < 16 ? 2 : 5;
    tv_->set_number(cell.id, std::atof(s.value.c_str()) + dir * s.step * accel);
}

void Panel::key_down(SDL_Keycode key, bool repeat) {
    if (!open_) return;
    pointer_ = false;
    Page &page = *stack_.back();
    Cell *cell = focused();
    switch (key) {
    case SDLK_UP: move_focus(-1, 0); break;
    case SDLK_DOWN: move_focus(1, 0); break;
    case SDLK_LEFT:
    case SDLK_RIGHT: {
        int dir = key == SDLK_RIGHT ? 1 : -1;
        if (cell && cell->type == Type::kSlider) adjust(*cell, dir);
        else if (page.list && dir < 0 && !repeat) pop();
        else if (page.list && cell && cell->chevron && !repeat) cell->activate();
        else move_focus(0, dir);
        break;
    }
    case SDLK_RETURN:
        if (!repeat && cell && cell->activate && !cell->disabled) cell->activate();
        break;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        if (!repeat) pop();
        break;
    default: break;
    }
}

void Panel::key_up(SDL_Keycode) { held_key_ = 0; }

void Panel::options() {
    // Play/Pause on a paired Bluetooth device forgets it.
    Page &page = *stack_.back();
    Cell *cell = focused();
    if (page.kind != Page::kBluetooth || !cell || cell->key.compare(0, 3, "bt:") != 0) return;
    for (const BtDevice &d : bt_->devices()) {
        if ("bt:" + d.address == cell->key && d.paired) {
            bt_->forget(d);
            show_toast("Forgetting " + d.name);
        }
    }
}

// --- Pointer ---------------------------------------------------------------

Rect Panel::panel_rect() const {
    float a = std::clamp(anim_.x, 0.f, 1.f);
    // Whole pixels while it drops in: text rounded to pixels inside a panel at
    // a sub-pixel offset flickers between two rows.
    return {kPanelRight - kPanelW, std::round(kPanelTop - 18 * (1 - a)), kPanelW, std::round(height_.x)};
}

void Panel::pointer_move(float x, float y) {
    pointer_ = true;
    px_ = x;
    py_ = y;
    if (!open_) return;
    Page &page = *stack_.back();
    Rect pr = panel_rect();
    for (size_t r = 0; r < page.grid.size(); r++) {
        for (size_t c = 0; c < page.grid[r].size(); c++) {
            const Cell &cell = page.cells[page.grid[r][c]];
            Rect cr = cell.rect;
            cr.x += pr.x;
            cr.y += pr.y;
            // Rows scroll under the fixed header.
            if (cell.type != Type::kHeader) {
                if (page.list && y < pr.y + kPad + kHeaderH) continue;
                cr.y -= page.scroll.x;
            }
            if (inside(cr, x, y) && (int(r) != page.row || int(c) != page.col)) set_focus(int(r), int(c));
        }
    }
}

void Panel::pointer_click() {
    if (!open_) return;
    Page &page = *stack_.back();
    Rect pr = panel_rect();
    if (!inside(pr, px_, py_)) {
        if (on_close) on_close();
        return;
    }
    if (page.list && inside({pr.x + kPad, pr.y + kPad, kInner, kHeaderH}, px_, py_)) {
        pop();
        return;
    }
    Cell *cell = focused();
    if (!cell) return;
    Rect cr = cell->rect;
    cr.x += pr.x;
    cr.y += pr.y - page.scroll.x;
    if (!inside(cr, px_, py_)) return;
    if (cell->type == Type::kSlider) {
        const Setting &s = tv_->get(cell->id);
        float fraction = std::clamp((px_ - cr.x) / cr.w, 0.f, 1.f);
        if (!s.disabled && !s.value.empty()) tv_->set_number(cell->id, std::round(s.min + fraction * (s.max - s.min)));
    } else if (cell->activate && !cell->disabled) {
        cell->activate();
    }
}

void Panel::wheel(int steps) {
    if (open_) move_focus(steps > 0 ? -1 : 1, 0);
}

// --- Updates from the TV ---------------------------------------------------

void Panel::setting_changed(const std::string &id) {
    for (auto &page : stack_) {
        if (shape_of(*page) != page->shape) {
            if (page.get() == stack_.back().get()) rebuild(*page);
            else build(*page);
            continue;
        }
        for (Cell &c : page->cells) {
            if (c.id == id) refresh(c);
        }
    }
}

void Panel::devices_changed() {
    Page &page = *stack_.back();
    if (page.kind == Page::kBluetooth) rebuild(page);
}

void Panel::show_toast(const std::string &message) {
    destroy(toast_);
    toast_ = text(small_font_, fit(small_font_, message, kPanelW - 56));
    toast_time_ = kToastTime;
}

// --- Frame -----------------------------------------------------------------

void Panel::update(float dt) {
    anim_.step(dt, kFocusStiffness, kFocusDamping);
    slide_.step(dt, kSlideStiffness, kSlideDamping);
    Page &page = *stack_.back();
    height_.target = std::min(page.height, kMaxHeight);
    height_.step(dt, kSlideStiffness, kSlideDamping);
    page.scroll.step(dt, kFocusStiffness, kFocusDamping);
    page.highlight.step(dt, kFocusStiffness, kFocusDamping);
    for (Cell &c : page.cells) c.focus.step(dt, kFocusStiffness, kFocusDamping);
    if (toast_time_ > 0) toast_time_ -= dt;
}

void Panel::draw_cell(Painter &p, Page &page, Cell &cell, float ox, float oy, float alpha) {
    float f = std::clamp(cell.focus.x, 0.f, 1.f);
    Rect r{ox + cell.rect.x, oy + cell.rect.y, cell.rect.w, cell.rect.h};
    float a = alpha * (cell.disabled ? 0.4f : 1);
    // In lists the sliding highlight is the focus; elsewhere each cell lights up.
    float lit = f;
    if (page.list) {
        // The header doesn't scroll; the highlight is drawn scrolled.
        float row_y = cell.type == Type::kHeader ? cell.rect.y + page.scroll.x : cell.rect.y;
        lit = std::clamp(1 - std::abs(page.highlight.x - row_y) / kRowH, 0.f, 1.f);
    }

    float radius = kCellRadius;
    if (!page.list && cell.type != Type::kHeader) {
        // Grows by the same few pixels whatever its width, so wide cells stay inside the panel.
        r = r.inset(-kGrow * f);
        radius += kGrow * f;
        if (f > 0.01f) p.shadow(r, radius, 18, rgb(0x000000, 0.35f), f * a);
        p.fill(r, radius, mix(rgb(0xffffff, 0.10f), rgb(0xffffff), f), mix(rgb(0xffffff, 0.08f), rgb(0xffffff), f), a);
    }
    if (cell.type == Type::kSlider) {
        const Setting &s = tv_->get(cell.id);
        double n = std::atof(s.value.c_str());
        float fraction = s.value.empty() || s.max <= s.min ? 0 : float((n - s.min) / (s.max - s.min));
        // Never narrower than it is tall, so the end stays round.
        Rect level{r.x, r.y, std::max(r.h, r.w * std::clamp(fraction, 0.f, 1.f)), r.h};
        Color tint = mix(rgb(0xffffff, 0.16f), rgb(0x000000, 0.10f), lit);
        if (!s.value.empty()) p.fill(level, radius, tint, tint, a);
    }

    Color c = ink(lit);
    float tx = r.x + kTextLeft;
    float cy = r.y + r.h / 2;
    Texture ic = cell.icon.empty() ? Texture{} : icon(cell.icon);
    if (cell.type == Type::kButton) {
        // Icon over the label, centred.
        float total = kIcon + 6 + cell.label.h;
        float top = std::round(cy - total / 2);
        if (ic.id) p.image({r.x + (r.w - kIcon) / 2, top, kIcon, kIcon}, 0, ic, c, a);
        p.text(std::round(r.x + (r.w - cell.label.w) / 2), top + kIcon + 6, cell.label, c, a);
        return;
    }
    if (cell.type == Type::kHeader) {
        if (ic.id) p.image({r.x + 14, std::round(cy - kIcon / 2), kIcon, kIcon}, 0, ic, ink(lit, 0.7f + 0.3f * lit), a);
        p.text(std::round(r.x + 14 + kIcon + 12), std::round(cy - cell.label.h / 2), cell.label, c, a);
        return;
    }
    if (ic.id) {
        p.image({tx - 4, std::round(cy - kIcon / 2), kIcon, kIcon}, 0, ic, c, a);
        tx += kIcon + 14;
    }
    if (cell.type == Type::kTile) {
        // Label over value.
        float gap = 2, total = cell.label.h + gap + cell.value.h;
        float top = std::round(cy - total / 2);
        p.text(std::round(tx), top, cell.label, {c.r, c.g, c.b, 0.65f}, a);
        p.text(std::round(tx), top + cell.label.h + gap, cell.value, c, a);
        return;
    }
    p.text(std::round(tx), std::round(cy - cell.label.h / 2), cell.label, c, a);

    float right = r.x + r.w - kTextLeft;
    if (cell.check) {
        Texture ck = icon("check");
        p.image({right - 30, std::round(cy - 15), 30, 30}, 0, ck, c, a);
        return;
    }
    if (cell.toggle) {
        // On/off switch.
        Rect track{right - 60, std::round(cy - 17), 60, 34};
        Color off = mix(rgb(0xffffff, 0.24f), rgb(0x000000, 0.16f), lit);
        Color on = rgb(0x30d158);
        p.fill(track, 17, cell.toggle_on ? on : off, cell.toggle_on ? on : off, a);
        Rect knob{track.x + (cell.toggle_on ? 29 : 3), track.y + 3, 28, 28};
        p.fill(knob, 14, rgb(0xffffff), rgb(0xffffff), a);
        return;
    }
    if (cell.chevron) {
        Texture ch = icon("chevron");
        p.image({right - 22, std::round(cy - 13), 26, 26}, 0, ch, {c.r, c.g, c.b, 0.6f}, a);
        right -= 22 + 12;
    }
    if (cell.value.id) {
        bool slider = cell.type == Type::kSlider;
        Color vc = slider ? c : Color{c.r, c.g, c.b, 0.65f};
        p.text(std::round(right - cell.value.w), std::round(cy - cell.value.h / 2), cell.value, vc, a);
    }
}

void Panel::draw_page(Painter &p, Page &page, float ox, float oy, float alpha) {
    for (Cell &c : page.cells) {
        if (c.type == Type::kHeader) continue;
        draw_cell(p, page, c, ox, oy - page.scroll.x, alpha);
    }
}

void Panel::render(Painter &p) {
    float a = std::clamp(anim_.x, 0.f, 1.f);
    if (a < 0.01f) return;
    Page &page = *stack_.back();
    Rect pr = panel_rect();

    // The same glass as Home's menus.
    p.shadow(pr, kPanelRadius, 40, rgb(0x000000, 0.45f), a);
    if (backdrop_.id) {
        p.set_backdrop(backdrop_);
        p.glass(pr, kPanelRadius, rgb(0x0b1016, 0.45f), a);
    } else {
        // Home hasn't run since the TV started: a rim of light around dark smoked glass.
        p.fill(pr, kPanelRadius, rgb(0xffffff, 0.16f), rgb(0xffffff, 0.06f), a);
        p.fill(pr.inset(1.5f), kPanelRadius - 1.5f, rgb(0x1b2029, 0.90f), rgb(0x0f1217, 0.92f), a);
    }

    float s = std::clamp(slide_.x, 0.f, 1.f);
    float ox = pr.x + slide_dir_ * (1 - s) * 40, oy = pr.y;
    float pa = a * (0.3f + 0.7f * s);

    glEnable(GL_SCISSOR_TEST);
    glScissor(int(pr.x), int(1080 - (pr.y + pr.h)), int(pr.w), int(pr.h));
    // The list's highlight, over the header too (rows only pass under it).
    if (page.list && !page.grid.empty()) {
        Rect h{ox + kPad, oy + page.highlight.x - page.scroll.x, kInner, kRowH};
        p.fill(h, kCellRadius, rgb(0xffffff), rgb(0xffffff), pa);
    }
    // Lists scroll under a fixed header; clip to the panel.
    float top = page.list ? kPad + kHeaderH : 0;
    glScissor(int(pr.x), int(1080 - (pr.y + pr.h)), int(pr.w), int(pr.h - top));
    draw_page(p, page, ox, oy, pa);
    glDisable(GL_SCISSOR_TEST);
    for (Cell &c : page.cells) {
        if (c.type == Type::kHeader) draw_cell(p, page, c, ox, oy, pa);
    }

    if (toast_time_ > 0 && toast_.id) {
        float ta = std::min(1.f, toast_time_ / 0.3f) * a;
        Rect t{pr.x + 8, pr.y + pr.h + 14, pr.w - 16, float(toast_.h) + 28};
        p.fill(t, t.h / 2, rgb(0x3a1d1d, 0.95f), rgb(0x3a1d1d, 0.95f), ta);
        p.text(std::round(t.x + (t.w - toast_.w) / 2), std::round(t.y + 14), toast_, rgb(0xffd7d7), ta);
    }
}

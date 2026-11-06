// The Control Center: a transparent overlay window over whatever is playing.
// Over Home it draws the status pill (common/pill.h) as Home publishes it in
// the shared state (common/shell_state.h). Under the pill it opens the panel
// or the Inputs menu: from the pill (Home's focus or the pointer), or from
// the remote's Settings key (tv/settingskey.js) over any app, with a pill of
// its own above the dimmed screen.
//
// It stays loaded. Over Home it stays on screen without the remote, taking
// the pointer only over the pill (its input region); over other apps it moves
// its window off screen when closed, so opening it again is instant. It asks
// to stay loaded when another app comes to the front (keepWindowOnAppSwitch);
// should it be closed anyway, Home and tv/settings-key reload it hidden
// ({"hidden": true}): it then draws one transparent frame off screen, so its
// surface is already mapped when it is next shown.

#include "bluetooth.h"
#include "fonts.h"
#include "gfx.h"
#include "inputs.h"
#include "lg_sdl.h"
#include "log.h"
#include "luna.h"
#include "overlay.h"
#include "panel.h"
#include "pill.h"
#include "shell_state.h"
#include "spring.h"
#include "tv.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_webOS.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>

#include <sys/stat.h>

namespace {

const char *kAppId = "com.lgtvtweaks.control";
const char *kHome = "com.lgtvtweaks.home";
const int kScancodePlay = 450;  // LG remote's Play: not in SDL_webOS.h
// tv/settings-key touches this just before it reloads the app hidden (Home
// says so in the shared state instead). A relaunch that soon after is that
// reload reaching a copy still running, not a request to open.
const char *kPreloadMarker = "/tmp/lgtv-control.preload";
constexpr uint64_t kPreloadAge = 3000;
// The foreground app's id: tv/settings-key writes it on every change.
const char *kForeground = "/tmp/lgtv-control.foreground";
// Home publishes every frame, in front or not; older than this, it has
// stopped (crashed or closed), and its pill goes. (A screen capture holds up
// every app's frames for a second or two.)
constexpr uint64_t kHomeAlive = 3000;
// A request from Home this recent when the app starts is what started it.
constexpr uint64_t kRequestAge = 3000;
// A relaunch this soon after a request was handled is Home's launch for that
// same request (it launches when it thinks the app isn't running).
constexpr uint64_t kRequestRelaunch = 1500;
// Over Home, how often to look for a new wallpaper backdrop.
constexpr uint64_t kBackdropCheck = 250;
// The pill's focus while the panel is open: the home screen's springs.
constexpr float kFocusStiffness = 260, kFocusDamping = 26;

std::string assets_dir() {
    if (const char *env = std::getenv("CONTROL_ASSETS")) return env;
    char *base = SDL_GetBasePath();  // .../bin/
    std::string dir = std::string(base ? base : "./") + "../assets";
    SDL_free(base);
    return dir;
}

bool preload_relaunch(const shell::HomeState &home) {
    struct stat st;
    uint64_t now = shell::now_ms();
    if (stat(kPreloadMarker, &st) == 0 && uint64_t(std::time(nullptr) - st.st_mtime) * 1000 <= kPreloadAge) return true;
    return home.preload_ms && now - home.preload_ms <= kPreloadAge;
}

// The foreground app, from kForeground.
struct Foreground {
    long long written = -1;
    std::string id;

    // Reads the file again if it was written; true if the id changed.
    bool update() {
        struct stat st;
        long long w = stat(kForeground, &st) == 0 ? st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec : 0;
        if (w == written) return false;
        written = w;
        std::ifstream f(kForeground);
        std::string next;
        std::getline(f, next);
        if (next == id) return false;
        id = next;
        return true;
    }
    // No file yet (tv/settings-key starts a while after Home at boot): Home is the first app.
    bool home() const { return !written || id == kHome; }
};

// What a frame of the pill shows, to skip drawing the same one again.
struct PillFrame {
    float alpha = -1, focus[Pill::kButtons] = {};
    bool gear = false, glass = false;
    int minute = -1;
    unsigned backdrop = 0;  // Panel::backdrop_version(): a new wallpaper redraws the glass

    bool same(const PillFrame &o) const {
        auto near = [](float a, float b) { return std::abs(a - b) < 0.002f; };
        for (int i = 0; i < Pill::kButtons; i++) {
            if (!near(focus[i], o.focus[i])) return false;
        }
        return near(alpha, o.alpha) && gear == o.gear && glass == o.glass && minute == o.minute &&
               backdrop == o.backdrop;
    }
};

struct Region {
    int x = 0, y = 0, w = -1, h = -1;  // w -1: never set
    bool operator==(const Region &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

bool inside(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

}  // namespace

int main(int argc, char **argv) {
    // Launched by the app manager (argv[1] is its JSON), stderr is /dev/null.
    if (argc > 1 && argv[1][0] == '{' && std::freopen("/tmp/lgtv-control.log", "w", stderr)) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    bool start_hidden = argc > 1 && std::strstr(argv[1], "\"hidden\":true");

    // Back and Exit come to us instead of LG's "exit the app?" dialog.
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0 || TTF_Init() != 0 || !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        LOG("init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    // An alpha channel: what isn't drawn shows the app (and video) below.
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_Window *window = SDL_CreateWindow("Control Center", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920,
                                          1080, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!window) {
        LOG("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    // Before the first frame: the compositor reads the type when it maps the surface.
    bool overlay = lg_sdl::set_property(window, "_WEBOS_WINDOW_TYPE", "_WEBOS_WINDOW_TYPE_OVERLAY");
    bool relaunch = overlay && overlay::hook_relaunch();
    // Without them, an ordinary full-screen window that closes when done.
    bool resident = overlay && relaunch;
    if (!resident) LOG("control: not this SDL build's layout; running as a plain window");
    if (start_hidden && !resident) return 0;
    // An overlay is closed when another app comes to the front, unless it asks
    // to stay: then surface-manager leaves it to the app (StarfishOverlaySurface.qml).
    if (resident) lg_sdl::set_property(window, "keepWindowOnAppSwitch", "true");
    if (start_hidden) lg_sdl::set_property(window, "windowPosX", "1920");

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        LOG("SDL_GL_CreateContext: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);

    Painter painter;
    if (!painter.init(w, h)) return 1;
    // The window is composited as premultiplied: colour blended by its alpha,
    // alpha accumulated.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    Luna luna;
    if (!luna.connect(kAppId)) LOG("control: no Luna bus");
    Tv tv(luna);
    Bluetooth bt(tv);
    Panel panel;
    if (!panel.init(assets_dir(), assets_dir() + "/icons", &tv, &bt)) return 1;
    tv.changed = [&panel](const std::string &id) { panel.setting_changed(id); };
    tv.error = [&panel](const std::string &message) {
        LOG("control: %s", message.c_str());
        panel.show_toast(message);
    };
    bt.changed = [&panel]() { panel.devices_changed(); };
    luna.subscribe("luna://com.webos.service.eim/getAllInputStatus", R"({"subscribe":true})",
                   [&panel](json_object *reply) {
                       std::vector<Input> inputs = inputs_from_status(reply);
                       if (!inputs.empty()) panel.set_inputs(inputs);
                   });

    // The pill, in Home's font so the layouts match.
    TTF_Font *pill_font = TTF_OpenFont(fonts::ui(assets_dir(), fonts::Weight::Semibold).c_str(), 28);
    if (!pill_font) {
        LOG("control: pill font: %s", TTF_GetError());
        return 1;
    }
    Pill pill;
    pill.init(pill_font);
    Rect pr = pill.rect();
    panel.set_anchor(pr.x + pr.w, pr.y + pr.h + 14);
    const Texture glyphs[Pill::kButtons] = {panel.icon("pill-inputs"), panel.icon("pill-control"),
                                            panel.icon("pill-settings")};

    shell::State shared;
    if (!shared.open()) LOG("control: no shared state; no pill over Home");
    shell::HomeState home = shared.home();
    shell::ControlState me = shared.control();  // its request count carries on
    me.pointer_button = -1;
    uint32_t handled_request = home.request_seq;
    uint64_t handled_at = 0, backdrop_checked = 0;
    Foreground fg;
    fg.update();

    // While open, the pill's focus is the app's own: lit only while focus is
    // on the pill, not while it is in the panel or menu under it.
    Spring pill_focus[Pill::kButtons];
    int pill_button = Pill::kControl;
    bool over_home = false;   // opened over Home: its glass and its gear
    std::string opened_over;  // the foreground app when it opened

    auto home_alive = [&]() { return shell::now_ms() - home.alive_ms < kHomeAlive; };
    // Home in front and drawing: its pill is shown.
    auto home_front = [&]() { return fg.home() && home.front && home_alive(); };
    auto root_button = [&]() { return panel.root() == Panel::Root::kInputs ? Pill::kInputs : Pill::kControl; };
    auto ask_home = [&](shell::HomeRequest request, int arg = 0) {
        me.request = request;
        me.request_arg = int8_t(arg);
        me.request_seq++;
    };

    bool running = true;
    bool on_screen = !start_hidden;
    auto open = [&](Panel::Root root) {
        bool was_open = panel.is_open();
        if (!was_open) {
            over_home = fg.home();
            opened_over = fg.id;
            // From where Home's pill focus was, so it doesn't jump.
            for (int i = 0; i < Pill::kButtons; i++) pill_focus[i].snap(home_front() ? home.pill_focus[i] : 0);
        }
        panel.open(root, over_home);
        pill_button = root_button();
        if (resident) {
            if (!on_screen) lg_sdl::set_property(window, "windowPosX", "0");
            lg_sdl::set_property(window, "needFocus", "true");
        }
        on_screen = true;
    };
    // The remote goes back to the app at once; the panel fades out.
    auto close = [&](bool fade) {
        if (!panel.is_open()) return;
        if (resident) lg_sdl::set_property(window, "needFocus", "false");
        // Focus on the pill stays where it is: Home moves its own there. So
        // does the button of the menu that was open, if Home's focus is in the
        // pill (it opened the other one from there).
        bool home_on_pill = *std::max_element(home.pill_focus, home.pill_focus + Pill::kButtons) > 0.5f;
        if (over_home && home_front() && (!panel.active() || home_on_pill)) {
            ask_home(shell::kFocusPill, panel.active() ? root_button() : pill_button);
        }
        panel.close(fade);
    };
    auto press_pill = [&](int button) {
        if (button == Pill::kSettings) {
            // Home Settings is Home's own sheet.
            close(true);
            ask_home(shell::kOpenHomeSettings);
            return;
        }
        Panel::Root root = button == Pill::kInputs ? Panel::Root::kInputs : Panel::Root::kSettings;
        if (panel.is_open() && panel.root() == root) close(true);
        else open(root);
    };
    auto open_requested = [&](uint8_t request) {
        if (request == shell::kOpenPanel) open(Panel::Root::kSettings);
        else if (request == shell::kOpenInputs) open(Panel::Root::kInputs);
    };
    panel.on_close = [&]() { close(true); };
    panel.on_leave_top = [&]() {
        panel.set_active(false);
        pill_button = root_button();
    };

    // Shown at start: Home's request that started it, or the Settings key.
    if (!start_hidden) {
        bool requested = home.request != shell::kNoRequest && shell::now_ms() - home.request_ms < kRequestAge;
        if (requested) open_requested(home.request);
        else open(Panel::Root::kSettings);
        handled_at = shell::now_ms();
    }

    // A hidden start still draws one (empty) frame, off screen.
    bool premap = start_hidden;
    bool drew = true;
    Region region;
    PillFrame last_pill;
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    while (running) {
        luna.pump();
        SDL_Event e;
        // Off screen: wake for events every 50 ms. Over Home with nothing
        // changing: every 16 ms, to see what Home publishes.
        bool got = drew || premap ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, on_screen ? 16 : 50);
        for (; got; got = SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && !panel.is_open() &&
                resident) {
                // The compositor gives a new overlay the remote even when it asks
                // not to ("false" reads as true then); it lets go on a change.
                lg_sdl::set_property(window, "needFocus", "true");
                lg_sdl::set_property(window, "needFocus", "false");
            }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_LEAVE) me.pointer_button = -1;
            bool pill_visible = panel.is_open() || (home_front() && home.pill_alpha > 0.5f);
            if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONUP) {
                float x = e.type == SDL_MOUSEMOTION ? float(e.motion.x) : float(e.button.x);
                float y = e.type == SDL_MOUSEMOTION ? float(e.motion.y) : float(e.button.y);
                int button = pill_visible ? pill.hit(x, y) : -1;
                if (e.type == SDL_MOUSEBUTTONUP) {
                    if (e.button.button != SDL_BUTTON_LEFT) continue;
                    if (button >= 0) press_pill(button);
                    else if (panel.is_open()) {
                        panel.pointer_move(x, y);
                        panel.pointer_click();
                    }
                } else if (!panel.is_open()) {
                    // Over Home: Home moves its focus to the button.
                    if (button != me.pointer_button) {
                        me.pointer_button = int8_t(button);
                        if (button >= 0) me.pointer_seq++;
                    }
                } else if (button >= 0) {
                    panel.set_active(false);
                    pill_button = button;
                } else {
                    if (inside(panel.rect(), x, y)) panel.set_active(true);
                    panel.pointer_move(x, y);
                }
                continue;
            }
            if (!panel.is_open()) continue;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                SDL_Keycode key = e.key.keysym.sym;
                int scancode = e.key.keysym.scancode;
                // LG's remote keys have scancodes but no keycode.
                if (scancode == SDL_WEBOS_SCANCODE_BACK || scancode == SDL_WEBOS_SCANCODE_EXIT) key = SDLK_AC_BACK;
                if (scancode == SDL_WEBOS_SCANCODE_EXIT && e.type == SDL_KEYDOWN) {
                    close(true);
                    continue;
                }
                if (!panel.active()) {
                    // Focus on the pill: Left/Right along it, Down back to the panel.
                    if (e.type != SDL_KEYDOWN) continue;
                    int buttons = pill.gear() ? Pill::kButtons : Pill::kSettings;
                    if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                        int next = pill_button + (key == SDLK_RIGHT ? 1 : -1);
                        if (next >= 0 && next < buttons) pill_button = next;
                        else pill_focus[pill_button].v += (key == SDLK_RIGHT ? 4 : -4);  // bump at the ends
                    } else if (key == SDLK_DOWN) {
                        panel.set_active(true);
                    } else if (key == SDLK_RETURN && !e.key.repeat) {
                        press_pill(pill_button);
                    } else if ((key == SDLK_AC_BACK || key == SDLK_ESCAPE) && !e.key.repeat) {
                        close(true);
                    }
                    continue;
                }
                if (scancode == kScancodePlay || scancode == SDL_SCANCODE_PAUSE) {
                    if (e.type == SDL_KEYDOWN && !e.key.repeat) panel.options();
                    continue;
                }
                if (e.type == SDL_KEYDOWN) panel.key_down(key, e.key.repeat);
                else panel.key_up(key);
            }
            if (e.type == SDL_MOUSEWHEEL && e.wheel.y) panel.wheel(e.wheel.y);
        }

        uint64_t now = shell::now_ms();
        home = shared.home();
        // Another app came to the front (the Home key, an input...). The panel
        // outlives the switch, but not over the new app: it goes at once, as
        // the compositor no longer draws it promptly.
        if (fg.update() && panel.is_open() && fg.id != opened_over) close(false);
        if (home.request_seq != handled_request) {
            handled_request = home.request_seq;
            handled_at = now;
            if (home.request_ms && now - home.request_ms < kRequestAge) open_requested(home.request);
        }
        me.alive_ms = now;
        shared.set_control(me);
        if (overlay::take_relaunch()) {
            // The Settings key launches the app: open it, or close it when it
            // is already open.
            if (preload_relaunch(home)) LOG("control: relaunch from a preload, ignored");
            else if (now - handled_at < kRequestRelaunch) LOG("control: relaunch for Home's request, ignored");
            else if (panel.is_open()) close(true);
            else open(Panel::Root::kSettings);
        }
        tv.update();
        bt.update();

        Uint64 counter = SDL_GetPerformanceCounter();
        double elapsed = double(counter - last) / freq;
        last = counter;
        if (elapsed > 0.05 && panel.is_open() && drew) LOG("control: slow frame %.0f ms", elapsed * 1000);
        // Clamp long gaps (just shown, debugger) so springs don't jump.
        float dt = float(std::min(elapsed, 0.1));
        panel.update(dt);
        for (int i = 0; i < Pill::kButtons; i++) {
            pill_focus[i].target = !panel.active() && pill_button == i ? 1 : 0;
            pill_focus[i].step(dt, kFocusStiffness, kFocusDamping);
        }
        if (!resident && panel.faded_out()) break;

        // Where the window is and what it takes. Over Home the pill is always
        // there (faded out in the grid); elsewhere only while the panel is.
        bool pill_over_home = home_front();
        bool want_on_screen = !panel.faded_out() || (pill_over_home && resident);
        pill.set_gear(pill_over_home);
        if (!pill.gear()) pill_button = std::min<int>(pill_button, Pill::kControl);
        pill.refresh_clock();
        pr = pill.rect();
        Region want;
        if (panel.is_open()) want = {0, 0, w, h};
        else if (pill_over_home && home.pill_alpha > 0.5f) want = {int(pr.x), int(pr.y), int(pr.w), int(pr.h)};
        else want = {0, 0, 0, 0};
        if (!want_on_screen) want = {0, 0, 0, 0};

        // The pill as it should look now. While the panel is open over Home
        // it is the app's own; closing, it blends back into Home's.
        float k = panel.shown();
        PillFrame frame;
        frame.alpha = pill_over_home ? std::max(home.pill_alpha, k) : k;
        for (int i = 0; i < Pill::kButtons; i++) {
            float home_focus = pill_over_home ? home.pill_focus[i] : 0;
            frame.focus[i] = home_focus + (pill_focus[i].x - home_focus) * k;
        }
        frame.gear = pill.gear();
        if (fg.home() && now - backdrop_checked > kBackdropCheck) {
            panel.load_backdrop();
            backdrop_checked = now;
        }
        frame.glass = fg.home() && panel.backdrop().id;
        frame.backdrop = panel.backdrop_version();
        std::time_t t = std::time(nullptr);
        std::tm local;
        localtime_r(&t, &local);
        frame.minute = local.tm_min;

        if (want_on_screen && !on_screen && resident) {
            lg_sdl::set_property(window, "windowPosX", "0");
            on_screen = true;
            last_pill = PillFrame{};  // draw it
        }
        if (!(want == region) && resident) {
            if (!overlay::set_input_region(window, want.x, want.y, want.w, want.h)) {
                LOG("control: can't set the input region");
            }
            region = want;
        }

        bool panel_drawn = !panel.faded_out();
        bool leaving = on_screen && !want_on_screen;
        drew = premap || panel_drawn || leaving || (on_screen && !frame.same(last_pill));
        if (!drew) continue;
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (want_on_screen) {
            // The screen dims behind the panel; the pill stays above it, as
            // the bar the menus hang from.
            Rect screen{0, 0, float(w), float(h)};
            if (k > 0.01f) painter.fill(screen, 0, rgb(0x000000, 0.30f), rgb(0x000000, 0.30f), k);
            if (frame.glass) painter.set_backdrop(panel.backdrop());
            pill.draw(painter, frame.alpha, frame.focus, glyphs, frame.glass);
            if (panel_drawn) panel.render(painter);
        }
        SDL_GL_SwapWindow(window);
        last_pill = frame;
        if (!want_on_screen && resident && (on_screen || premap)) {
            // The empty frame is up: out of the way, so it costs the compositor nothing.
            lg_sdl::set_property(window, "windowPosX", "1920");
            on_screen = false;
            premap = false;
            drew = false;
        }
        premap = false;
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

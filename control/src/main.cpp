// The Control Center: a transparent overlay window over whatever is playing,
// opened by the remote's Settings key (tv/settingskey.js) and Home's control
// center button.
//
// It stays loaded and hides by moving its window off screen and giving the
// remote back, so opening it again is instant. It asks to stay loaded when
// another app comes to the front (keepWindowOnAppSwitch); should it be closed
// anyway, tv/settings-key reloads it hidden ({"hidden": true}): it then draws
// one transparent frame off screen, so its surface is already mapped when it
// is next opened.

#include "bluetooth.h"
#include "gfx.h"
#include "log.h"
#include "luna.h"
#include "overlay.h"
#include "panel.h"
#include "tv.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_webOS.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <sys/stat.h>

namespace {

const char *kAppId = "com.lgtvtweaks.control";
const char *kFontDir = "/usr/share/fonts";
const int kScancodePlay = 450;  // LG remote's Play: not in SDL_webOS.h
// tv/settings-key touches this just before it reloads the app hidden. A
// relaunch that soon after is that reload reaching a copy still running, not
// a request to open.
const char *kPreloadMarker = "/tmp/lgtv-control.preload";
const time_t kPreloadMarkerAge = 3;

std::string assets_dir() {
    if (const char *env = std::getenv("CONTROL_ASSETS")) return env;
    char *base = SDL_GetBasePath();  // .../bin/
    std::string dir = std::string(base ? base : "./") + "../assets";
    SDL_free(base);
    return dir;
}

bool preload_relaunch() {
    struct stat st;
    return stat(kPreloadMarker, &st) == 0 && std::time(nullptr) - st.st_mtime <= kPreloadMarkerAge;
}

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
    bool overlay = overlay::set_property(window, "_WEBOS_WINDOW_TYPE", "_WEBOS_WINDOW_TYPE_OVERLAY");
    bool relaunch = overlay && overlay::hook_relaunch();
    // Without them, an ordinary full-screen window that closes when done.
    bool resident = overlay && relaunch;
    if (!resident) LOG("control: not this SDL build's layout; running as a plain window");
    if (start_hidden && !resident) return 0;
    // An overlay is closed when another app comes to the front, unless it asks
    // to stay: then surface-manager leaves it to the app (StarfishOverlaySurface.qml).
    if (resident) overlay::set_property(window, "keepWindowOnAppSwitch", "true");
    if (start_hidden) overlay::set_property(window, "windowPosX", "1920");

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
    if (!panel.init(kFontDir, assets_dir() + "/icons", &tv, &bt)) return 1;
    tv.changed = [&panel](const std::string &id) { panel.setting_changed(id); };
    tv.error = [&panel](const std::string &message) {
        LOG("control: %s", message.c_str());
        panel.show_toast(message);
    };
    bt.changed = [&panel]() { panel.devices_changed(); };

    enum class State { kShown, kHiding, kHidden } state = State::kHidden;
    bool running = true;
    auto show = [&]() {
        if (resident) {
            overlay::set_property(window, "windowPosX", "0");
            overlay::set_property(window, "needFocus", "true");
        }
        panel.open();
        state = State::kShown;
    };
    // The remote goes back to the app at once; the window leaves once faded out.
    auto hide = [&](bool fade = true) {
        if (state != State::kShown) return;
        if (resident) overlay::set_property(window, "needFocus", "false");
        panel.close(fade);
        state = State::kHiding;
    };
    panel.on_close = hide;
    // A hidden start still draws one (empty) frame, off screen.
    bool premap = start_hidden;
    if (!start_hidden) show();

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    while (running) {
        luna.pump();
        SDL_Event e;
        bool got = state == State::kHidden && !premap ? SDL_WaitEventTimeout(&e, 50) : SDL_PollEvent(&e);
        for (; got; got = SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && state != State::kShown &&
                resident) {
                // The compositor gives a new overlay the remote even when it asks
                // not to ("false" reads as true then); it lets go on a change.
                overlay::set_property(window, "needFocus", "true");
                overlay::set_property(window, "needFocus", "false");
            }
            if (state != State::kShown) continue;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                SDL_Keycode key = e.key.keysym.sym;
                int scancode = e.key.keysym.scancode;
                // LG's remote keys have scancodes but no keycode.
                if (scancode == SDL_WEBOS_SCANCODE_BACK || scancode == SDL_WEBOS_SCANCODE_EXIT) key = SDLK_AC_BACK;
                if (scancode == SDL_WEBOS_SCANCODE_EXIT && e.type == SDL_KEYDOWN) {
                    hide();
                    continue;
                }
                if (scancode == kScancodePlay || scancode == SDL_SCANCODE_PAUSE) {
                    if (e.type == SDL_KEYDOWN && !e.key.repeat) panel.options();
                    continue;
                }
                if (e.type == SDL_KEYDOWN) panel.key_down(key, e.key.repeat);
                else panel.key_up(key);
            }
            if (e.type == SDL_MOUSEMOTION) panel.pointer_move(float(e.motion.x), float(e.motion.y));
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                panel.pointer_move(float(e.button.x), float(e.button.y));
                panel.pointer_click();
            }
            if (e.type == SDL_MOUSEWHEEL && e.wheel.y) panel.wheel(e.wheel.y);
        }
        if (overlay::take_relaunch()) {
            // The Settings key and Home's button both launch the app: open it,
            // or close it when it is already open.
            if (preload_relaunch()) LOG("control: relaunch from the preload, ignored");
            else if (state == State::kShown) hide();
            else show();
        }
        // Another app came to the front (the Home key, an input...). The panel
        // outlives the switch, but not over the new app: it goes at once, as
        // the compositor no longer draws it promptly.
        if (state == State::kShown && panel.app_switched()) hide(false);
        tv.update();
        bt.update();

        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = double(now - last) / freq;
        last = now;
        if (state == State::kHidden && !premap) continue;
        if (elapsed > 0.05 && state == State::kShown) LOG("control: slow frame %.0f ms", elapsed * 1000);
        // Clamp long gaps (just shown, debugger) so springs don't jump.
        float dt = float(std::min(elapsed, 0.1));
        panel.update(dt);
        if (state == State::kHiding && panel.faded_out()) {
            if (!resident) break;
            state = State::kHidden;
        }

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (state != State::kHidden) panel.render(painter);
        SDL_GL_SwapWindow(window);
        if (state == State::kHidden && resident) {
            // The empty frame is up: out of the way, so it costs the compositor nothing.
            overlay::set_property(window, "windowPosX", "1920");
            premap = false;
        }
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

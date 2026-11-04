// The home screen app: a full-screen GLES2 window on the TV (LG's SDL port,
// Wayland underneath), or a 1920x1080 window on a desktop for development.

#include "gfx.h"
#include "home.h"
#include "lg_sdl.h"
#include "log.h"
#include "luna.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#ifdef LGTV_WEBOS
#include <SDL_webOS.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

#include <sys/stat.h>

namespace {

const char *kAppId = "com.lgtvtweaks.home";
const char *kAppManager = "luna://com.webos.applicationManager/";
const int kScancodePlay = 450;  // LG remote's Play: not in SDL_webOS.h
// How long the first frame may wait for the jail mounts (after a reboot) and
// for icons to decode.
const Uint32 kMountTimeoutMs = 3000;
const int kIconsTimeoutMs = 1000;

// The app manager runs developer apps chrooted in /var/palm/jail/<id>, where
// other apps' folders (and so their icons) don't exist, and nothing outside
// /tmp is writable. The jail's mounts live in the TV's main mount table, so
// ask the Homebrew Channel's root exec service to bind the app folders in
// read-only, and kDataDir (owned by the jail user) read-write. Idempotent;
// gone at reboot. Each bind is made a slave: other apps' mounts (an overlay
// per store app) still appear inside it, but unmounting it (tv/uninstall.sh)
// can't propagate back and unmount the real ones.
const char *kDataDir = "/var/lib/lgtv-home";
const char *kJailMounts =
    "J=/var/palm/jail/com.lgtvtweaks.home; D=/var/lib/lgtv-home; "
    "for d in /media/cryptofs/apps/usr/palm/applications /mnt/otncabi/usr/palm/applications "
    "/usr/palm/applications; do "
    "mkdir -p $J$d && { grep -q \" $J$d \" /proc/mounts || "
    "{ mount --bind $d $J$d && mount -o remount,bind,ro $J$d && mount --make-rslave $J$d; }; }; done; "
    "mkdir -p $D/wallpapers $J$D && chown -R 6067:5000 $D && "  // the jail's prisoner:jailer
    "{ grep -q \" $J$D \" /proc/mounts || { mount --bind $D $J$D && mount --make-rslave $J$D; }; }";

#ifdef LGTV_WEBOS
const char *kFont = "/usr/share/fonts/LG_Smart_UI-SemiBold.ttf";
const Uint32 kWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
#else
const char *kFont = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf";
const Uint32 kWindowFlags = SDL_WINDOW_OPENGL;
#endif

// Wallpapers and other assets: next to bin/ in the installed app, the source
// tree in a desktop build. HOME_ASSETS overrides both.
std::string assets_dir() {
    if (const char *env = std::getenv("HOME_ASSETS")) return env;
#ifdef LGTV_WEBOS
    char *base = SDL_GetBasePath();  // .../bin/
    std::string dir = std::string(base ? base : "./") + "../assets";
    SDL_free(base);
    return dir;
#else
    return HOME_SOURCE_DIR "/assets";
#endif
}

}  // namespace

int main(int argc, char **argv) {
    // Launched by the app manager (argv[1] is its JSON), stderr is /dev/null.
    if (argc > 1 && argv[1][0] == '{' && std::freopen("/tmp/lgtv-home.log", "w", stderr)) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);  // freopen makes it fully buffered
    }

    // Back and Exit come to us instead of LG's "exit the app?" dialog: a home
    // screen never exits.
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0 || TTF_Init() != 0 ||
        !(IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & IMG_INIT_JPG)) {
        LOG("init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_Window *window = SDL_CreateWindow("Home", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          1920, 1080, kWindowFlags);
    if (!window) {
        LOG("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        LOG("SDL_GL_CreateContext: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    LOG("home: drawable %dx%d, %s", w, h, reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

    const char *font = std::getenv("HOME_FONT") ? std::getenv("HOME_FONT") : kFont;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 t0 = SDL_GetPerformanceCounter();
    Painter painter;
    Home home;
    if (!painter.init(w, h) || !home.init(w, h, font, assets_dir())) return 1;
    LOG("home: ready in %.0f ms", double(SDL_GetPerformanceCounter() - t0) * 1000 / freq);

    // Everything is in place before the first frame, so the screen appears
    // finished: the saved layout and wallpaper, the app list from the last run
    // and every icon. The live list replaces it once the app manager answers.
    Luna luna;
    std::vector<App> shown;
    if (luna.connect(kAppId)) {
        const std::string data = kDataDir;
        // The jail mounts last until reboot, so usually the last run made
        // them; the folder's contents only show through the mount.
        struct stat st;
        // After a reboot, wait for them (briefly) so the layout can be read.
        auto mounts = std::make_shared<int>(0);  // 0 waiting, 1 done, 2 gave up waiting
        if (stat((data + "/wallpapers").c_str(), &st) == 0) {
            *mounts = 1;
        } else {
            json_object *exec = json_object_new_object();
            json_object_object_add(exec, "command", json_object_new_string(kJailMounts));
            luna.call("luna://org.webosbrew.hbchannel.service/exec", json_object_to_json_string(exec),
                      [mounts, &home, data](json_object *reply) {
                          if (!get_bool(reply, "returnValue")) {
                              LOG("home: jail mounts: %s", json_object_to_json_string(reply));
                          }
                          if (*mounts == 2) home.set_layout_path(data + "/layout.json");
                          *mounts = 1;
                      });
            json_object_put(exec);
            for (Uint32 start = SDL_GetTicks(); !*mounts && SDL_GetTicks() - start < kMountTimeoutMs;) {
                luna.pump();
                SDL_Delay(5);
            }
            if (!*mounts) *mounts = 2;
        }
        // Until they're done, a default layout with nothing saved.
        home.set_layout_path(*mounts == 1 ? data + "/layout.json" : "");
        shown = load_apps(data + "/apps.json");
        home.set_apps(shown);
        home.set_status("Loading apps\u2026");  // first run only: no saved list yet
        home.load_icons_now(kIconsTimeoutMs);

        auto show = [&home, &shown, data](json_object *reply) {
            json_object *list;
            if (!json_object_object_get_ex(reply, "launchPoints", &list)) {
                LOG("home: listLaunchPoints: %s", json_object_to_json_string(reply));
                home.set_status("Couldn't get the app list: " + get_string(reply, "errorText", "no answer"));
                return;
            }
            std::vector<App> apps = apps_from_launch_points(reply, kAppId);
            LOG("home: %zu apps", apps.size());
            home.set_status("No apps found");
            if (apps == shown) return;
            shown = apps;
            save_apps(data + "/apps.json", apps);
            home.set_apps(apps);
        };
        // The first reply has the list; later ones describe one change, so
        // fetch the whole list again rather than patching it.
        luna.subscribe(std::string(kAppManager) + "listLaunchPoints", R"({"subscribe":true})",
                       [&luna, show](json_object *reply) {
                           json_object *change;
                           if (json_object_object_get_ex(reply, "change", &change)) {
                               luna.call(std::string(kAppManager) + "listLaunchPoints", "{}", show);
                           } else {
                               show(reply);
                           }
                       });

        home.on_launch = [&luna](const std::string &id, const std::string &app_params) {
            LOG("home: launch %s %s", id.c_str(), app_params.c_str());
            json_object *params = json_object_new_object();
            json_object_object_add(params, "id", json_object_new_string(id.c_str()));
            if (!app_params.empty()) json_object_object_add(params, "params", json_tokener_parse(app_params.c_str()));
            luna.call(std::string(kAppManager) + "launch", json_object_to_json_string(params),
                      [id](json_object *reply) {
                          if (!get_bool(reply, "returnValue")) {
                              LOG("home: launch %s: %s", id.c_str(), json_object_to_json_string(reply));
                          }
                      });
            json_object_put(params);
        };
        home.on_tile = [window](const std::string &json) {
            if (!lg_sdl::set_property(window, "lgtvLaunch", json.c_str())) LOG("home: can't set window properties");
        };
        home.on_tile_back = [window](const std::string &count) {
            lg_sdl::set_property(window, "lgtvTileBack", count.c_str());
        };
    } else {
        LOG("home: no Luna bus");
        home.set_layout_path("/tmp/lgtv-home-layout.json");
        home.set_status("No apps: can't reach the TV's app manager");
        home.load_icons_now(kIconsTimeoutMs);
    }
    LOG("home: first frame ready at %.0f ms", double(SDL_GetPerformanceCounter() - t0) * 1000 / freq);

    Uint64 last = SDL_GetPerformanceCounter(), stats_start = last;
    int frames = 0;
    double worst = 0;
    bool first_frame = true;
    for (bool running = true; running;) {
        luna.pump();
        for (SDL_Event e; SDL_PollEvent(&e);) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                SDL_Keycode key = e.key.keysym.sym;
                int scancode = e.key.keysym.scancode;
#ifdef LGTV_WEBOS
                // LG's remote keys have scancodes but no keycode.
                if (scancode == SDL_WEBOS_SCANCODE_BACK || scancode == SDL_WEBOS_SCANCODE_EXIT) key = SDLK_AC_BACK;
                if (scancode == SDL_WEBOS_SCANCODE_CURSOR_HIDE) {
                    if (e.type == SDL_KEYDOWN) home.pointer_hidden();
                    continue;
                }
                if (scancode == SDL_WEBOS_SCANCODE_CURSOR_SHOW) continue;
#endif
                // Play/Pause on a tile opens its menu, as on tvOS.
                if (scancode == kScancodePlay || scancode == SDL_SCANCODE_PAUSE) {
                    if (e.type == SDL_KEYDOWN && !e.key.repeat) home.options();
                    continue;
                }
                if (e.type == SDL_KEYDOWN) home.key_down(key, e.key.repeat);
                else home.key_up(key);
            }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) home.focus_gained();
            if (e.type == SDL_MOUSEMOTION) home.pointer_move(float(e.motion.x), float(e.motion.y));
            if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.button == SDL_BUTTON_LEFT) {
                home.pointer_move(float(e.button.x), float(e.button.y));
                if (e.type == SDL_MOUSEBUTTONDOWN) home.pointer_down();
                else home.pointer_up();
            }
            if (e.type == SDL_MOUSEWHEEL && e.wheel.y) home.wheel(e.wheel.y);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = double(now - last) / freq;
        last = now;
        if (dt > 0.1) LOG("home: slow frame %.0f ms", dt * 1000);
        worst = std::max(worst, dt);
        if (++frames, now - stats_start >= 5 * freq) {
            LOG("home: %.1f fps, worst frame %.1f ms", frames / (double(now - stats_start) / freq),
                worst * 1000);
            stats_start = now;
            frames = 0;
            worst = 0;
        }

        // Clamp long gaps (window hidden, debugger) so springs don't jump.
        home.update(float(std::min(dt, 0.1)));
        // A full clear lets Mali's tiler skip loading the previous frame.
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        home.render(painter);
        SDL_GL_SwapWindow(window);
        if (first_frame) {
            // The TV takes a while to show a new window; the first swap waits for it.
            LOG("home: first frame shown at %.0f ms", double(SDL_GetPerformanceCounter() - t0) * 1000 / freq);
            first_frame = false;
        }
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

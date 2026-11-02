// The home screen app: a full-screen GLES2 window on the TV (LG's SDL port,
// Wayland underneath), or a 1920x1080 window on a desktop for development.

#include "gfx.h"
#include "home.h"
#include "log.h"
#include "luna.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#ifdef HOME_WEBOS
#include <SDL_webOS.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <string>

namespace {

const char *kAppId = "com.lgtvtweaks.home";
const char *kAppManager = "luna://com.webos.applicationManager/";

// The app manager runs developer apps chrooted in /var/palm/jail/<id>, where
// other apps' folders (and so their icons) don't exist. The jail's mounts live
// in the TV's main mount table, so ask the Homebrew Channel's root exec
// service to bind them in read-only. Idempotent; gone at reboot.
const char *kJailMounts =
    "J=/var/palm/jail/com.lgtvtweaks.home; "
    "for d in /media/cryptofs/apps/usr/palm/applications /mnt/otncabi/usr/palm/applications "
    "/usr/palm/applications; do "
    "mkdir -p $J$d && { grep -q \" $J$d \" /proc/mounts || "
    "{ mount --bind $d $J$d && mount -o remount,bind,ro $J$d; }; }; done";

#ifdef HOME_WEBOS
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
#ifdef HOME_WEBOS
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

    Luna luna;
    if (luna.connect(kAppId)) {
        auto show = [&home](json_object *reply) {
            std::vector<App> apps = apps_from_launch_points(reply, kAppId);
            LOG("home: %zu apps", apps.size());
            if (!apps.empty()) home.set_apps(apps);
        };
        // The first reply has the list; later ones describe one change, so
        // fetch the whole list again rather than patching it.
        auto list_apps = [&luna, show] {
            luna.subscribe(std::string(kAppManager) + "listLaunchPoints", R"({"subscribe":true})",
                           [&luna, show](json_object *reply) {
                               json_object *list;
                               if (json_object_object_get_ex(reply, "launchPoints", &list)) {
                                   show(reply);
                               } else if (json_object_object_get_ex(reply, "change", &list)) {
                                   luna.call(std::string(kAppManager) + "listLaunchPoints", "{}", show);
                               } else {
                                   LOG("home: listLaunchPoints: %s", json_object_to_json_string(reply));
                               }
                           });
        };
        // Icons are read as soon as the list arrives, so mount first.
        json_object *exec = json_object_new_object();
        json_object_object_add(exec, "command", json_object_new_string(kJailMounts));
        luna.call("luna://org.webosbrew.hbchannel.service/exec", json_object_to_json_string(exec),
                  [list_apps](json_object *reply) {
                      if (!get_bool(reply, "returnValue")) {
                          LOG("home: jail mounts: %s", json_object_to_json_string(reply));
                      }
                      list_apps();
                  });
        json_object_put(exec);

        luna.subscribe("luna://com.webos.service.eim/getAllInputStatus", R"({"subscribe":true})",
                       [&home](json_object *reply) {
                           std::vector<Input> inputs = inputs_from_status(reply);
                           if (!inputs.empty()) home.set_inputs(inputs);
                       });
        home.on_launch = [&luna](const std::string &id) {
            LOG("home: launch %s", id.c_str());
            json_object *params = json_object_new_object();
            json_object_object_add(params, "id", json_object_new_string(id.c_str()));
            luna.call(std::string(kAppManager) + "launch", json_object_to_json_string(params),
                      [id](json_object *reply) {
                          if (!get_bool(reply, "returnValue")) {
                              LOG("home: launch %s: %s", id.c_str(), json_object_to_json_string(reply));
                          }
                      });
            json_object_put(params);
        };
    } else {
        LOG("home: no Luna bus, showing placeholder apps");
    }

    Uint64 last = SDL_GetPerformanceCounter(), stats_start = last;
    int frames = 0;
    double worst = 0;
    for (bool running = true; running;) {
        luna.pump();
        for (SDL_Event e; SDL_PollEvent(&e);) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
#ifdef HOME_WEBOS
                // LG's remote keys have scancodes but no keycode.
                int scancode = e.key.keysym.scancode;
                if (scancode == SDL_WEBOS_SCANCODE_BACK || scancode == SDL_WEBOS_SCANCODE_EXIT) key = SDLK_AC_BACK;
#endif
                home.key(key);
            }
            if (e.type == SDL_MOUSEMOTION) home.pointer_move(float(e.motion.x), float(e.motion.y));
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                home.pointer_move(float(e.button.x), float(e.button.y));
                home.pointer_click();
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
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

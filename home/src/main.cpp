// The home screen app: a full-screen GLES2 window on the TV (LG's SDL port,
// Wayland underneath), or a 1920x1080 window on a desktop for development.

#include "gfx.h"
#include "home.h"
#include "log.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace {

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

int main(int, char **) {
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

    Uint64 last = SDL_GetPerformanceCounter(), stats_start = last;
    int frames = 0;
    double worst = 0;
    for (bool running = true; running;) {
        for (SDL_Event e; SDL_PollEvent(&e);) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) home.key(e.key.keysym.sym);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = double(now - last) / freq;
        last = now;
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

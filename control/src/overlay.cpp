// The sysroot's SDL config leaves Wayland out of SDL_SysWMinfo; LG's build has it.
#define SDL_VIDEO_DRIVER_WAYLAND 1
#include "overlay.h"

#include "lg_sdl.h"
#include "log.h"

#include <SDL_syswm.h>
#include <wayland-client.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <dlfcn.h>

namespace overlay {

namespace {

// Offset in LG's libSDL2-2.0.so.0.14.0 on webOS 10.3.1 (OLED65C5PUA):
// callback_RegistNativeApp answers {"event":"relaunch"} by calling this
// SDL_VideoDevice function with the first window.
constexpr size_t kDeviceRelaunchFunction = 92;

// The instruction that uses it, as it is in that build (Thumb-2, little-endian).
const lg_sdl::Instruction kRelaunchLoad = {"callback_RegistNativeApp", 0xbe, {0xfb, 0x6d}, 2};  // ldr r3, [r7, #92]

using WindowFunction = void (*)(void *device, SDL_Window *window);

WindowFunction relaunch_original;
std::atomic<bool> relaunched{false};

void on_relaunch(void *device, SDL_Window *window) {
    relaunched = true;
    relaunch_original(device, window);
}

// Binds wl_compositor on a queue of our own, so SDL's events stay where SDL
// dispatches them.
wl_compositor *bind_compositor(wl_display *display) {
    wl_event_queue *queue = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    if (!queue || !wrapper) return nullptr;
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_compositor *compositor = nullptr;
    static const wl_registry_listener listener = {
        [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
            auto **out = static_cast<wl_compositor **>(data);
            if (!*out && std::strcmp(interface, wl_compositor_interface.name) == 0) {
                *out = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
            }
        },
        [](void *, wl_registry *, uint32_t) {},
    };
    wl_registry_add_listener(registry, &listener, &compositor);
    wl_display_roundtrip_queue(display, queue);
    wl_registry_destroy(registry);
    // It sends no events; off our queue before the queue goes.
    if (compositor) wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(compositor), nullptr);
    wl_event_queue_destroy(queue);
    return compositor;
}

}  // namespace

bool hook_relaunch() {
    if (!lg_sdl::matches(kRelaunchLoad)) {
        LOG("overlay: this SDL build isn't the one the relaunch hook was written for");
        return false;
    }
    auto get_device = reinterpret_cast<void *(*)()>(dlsym(RTLD_DEFAULT, "SDL_GetVideoDevice"));
    char *device = get_device ? static_cast<char *>(get_device()) : nullptr;
    if (!device) return false;
    auto *slot = reinterpret_cast<WindowFunction *>(device + kDeviceRelaunchFunction);
    if (*slot == on_relaunch) return true;
    Dl_info info{};
    if (!*slot || !dladdr(reinterpret_cast<void *>(*slot), &info) || !info.dli_fname ||
        !std::strstr(info.dli_fname, "libSDL2")) {
        LOG("overlay: the relaunch slot doesn't point into LG's SDL");
        return false;
    }
    relaunch_original = *slot;
    *slot = on_relaunch;
    return true;
}

bool take_relaunch() { return relaunched.exchange(false); }

bool set_input_region(SDL_Window *window, int x, int y, int w, int h) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WAYLAND || !info.info.wl.display ||
        !info.info.wl.surface) {
        return false;
    }
    static wl_compositor *compositor = bind_compositor(info.info.wl.display);
    if (!compositor) return false;
    wl_region *region = wl_compositor_create_region(compositor);
    if (w > 0 && h > 0) wl_region_add(region, x, y, w, h);
    wl_surface_set_input_region(info.info.wl.surface, region);
    wl_region_destroy(region);
    // Applied on commit; nothing new is attached, so the picture stays.
    wl_surface_commit(info.info.wl.surface);
    wl_display_flush(info.info.wl.display);
    return true;
}

}  // namespace overlay

// The sysroot's SDL config leaves Wayland out of SDL_SysWMinfo; LG's build has it.
#define SDL_VIDEO_DRIVER_WAYLAND 1
#include "overlay.h"

#include "log.h"

#include <SDL_syswm.h>
#include <wayland-client.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <dlfcn.h>

namespace overlay {

namespace {

// Offsets in LG's libSDL2-2.0.so.0.14.0 on webOS 10.3.1 (OLED65C5PUA).
// Wayland_CreateWindow stores its SDL_WindowData in SDL_Window::driverdata
// and the wl_webos_shell_surface in SDL_WindowData.
constexpr size_t kWindowDriverData = 144;
constexpr size_t kWindowDataShellSurface = 56;
// callback_RegistNativeApp answers {"event":"relaunch"} by calling this
// SDL_VideoDevice function with the first window.
constexpr size_t kDeviceRelaunchFunction = 92;
// wl_webos_shell_surface requests: set_property is the third.
constexpr uint32_t kSetProperty = 2;

// The instructions that use those offsets, as they are in that build (Thumb-2,
// little-endian): where they are, relative to the exported function.
struct Instruction {
    const char *function;
    size_t offset;
    unsigned char bytes[4];
    size_t size;
};
const Instruction kWindowDataStore = {"Wayland_CreateWindow", 0x30, {0xc5, 0xf8, 0x90, 0x00}, 4};  // str.w r0, [r5, #144]
const Instruction kShellSurfaceStore = {"Wayland_CreateWindow", 0x224, {0xa0, 0x63}, 2};           // str r0, [r4, #56]
const Instruction kRelaunchLoad = {"callback_RegistNativeApp", 0xbe, {0xfb, 0x6d}, 2};             // ldr r3, [r7, #92]

using GetClass = const char *(*)(void *proxy);
using Marshal = void (*)(void *proxy, uint32_t opcode, ...);
using WindowFunction = void (*)(void *device, SDL_Window *window);

GetClass get_class;
Marshal marshal;
WindowFunction relaunch_original;
std::atomic<bool> relaunched{false};

// Whether this copy of SDL is the build the offsets were taken from, judged
// by the instructions that use them. Reading the pointers in any other build
// could crash, so nothing is read unless these match.
bool matches(const Instruction &ins) {
    void *fn = dlsym(RTLD_DEFAULT, ins.function);
    if (!fn) return false;
    // Thumb function addresses have the low bit set.
    auto *code = reinterpret_cast<const unsigned char *>(reinterpret_cast<uintptr_t>(fn) & ~uintptr_t(1));
    return std::memcmp(code + ins.offset, ins.bytes, ins.size) == 0;
}

bool load_wayland() {
    if (marshal) return true;
    void *lib = dlopen("libwayland-client.so.0", RTLD_NOW);
    if (!lib) return false;
    get_class = reinterpret_cast<GetClass>(dlsym(lib, "wl_proxy_get_class"));
    marshal = reinterpret_cast<Marshal>(dlsym(lib, "wl_proxy_marshal"));
    return get_class && marshal;
}

void *shell_surface(SDL_Window *window) {
    static const bool layout = matches(kWindowDataStore) && matches(kShellSurfaceStore);
    if (!window || !layout || !load_wayland()) return nullptr;
    void *data = *reinterpret_cast<void **>(reinterpret_cast<char *>(window) + kWindowDriverData);
    if (!data) return nullptr;
    void *shell = *reinterpret_cast<void **>(static_cast<char *>(data) + kWindowDataShellSurface);
    if (!shell) return nullptr;
    const char *cls = get_class(shell);
    return cls && std::strcmp(cls, "wl_webos_shell_surface") == 0 ? shell : nullptr;
}

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

bool set_property(SDL_Window *window, const char *name, const char *value) {
    void *shell = shell_surface(window);
    if (!shell) return false;
    marshal(shell, kSetProperty, name, value);
    return true;
}

bool hook_relaunch() {
    if (!matches(kRelaunchLoad)) {
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

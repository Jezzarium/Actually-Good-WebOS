#include "overlay.h"

#include "log.h"

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

}  // namespace overlay

#include "lg_sdl.h"

#include <cstdint>
#include <cstring>

#include <dlfcn.h>

namespace lg_sdl {

namespace {

// Offsets in LG's libSDL2-2.0.so.0.14.0 on webOS 10.3.1 (OLED65C5PUA).
// Wayland_CreateWindow stores its SDL_WindowData in SDL_Window::driverdata
// and the wl_webos_shell_surface in SDL_WindowData.
constexpr size_t kWindowDriverData = 144;
constexpr size_t kWindowDataShellSurface = 56;
// wl_webos_shell_surface requests: set_property is the third.
constexpr uint32_t kSetProperty = 2;

// The instructions that use those offsets, as they are in that build (Thumb-2,
// little-endian).
const Instruction kWindowDataStore = {"Wayland_CreateWindow", 0x30, {0xc5, 0xf8, 0x90, 0x00}, 4};  // str.w r0, [r5, #144]
const Instruction kShellSurfaceStore = {"Wayland_CreateWindow", 0x224, {0xa0, 0x63}, 2};           // str r0, [r4, #56]

using GetClass = const char *(*)(void *proxy);
using Marshal = void (*)(void *proxy, uint32_t opcode, ...);

GetClass get_class;
Marshal marshal;

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

}  // namespace

bool matches(const Instruction &ins) {
    void *fn = dlsym(RTLD_DEFAULT, ins.function);
    if (!fn) return false;
    // Thumb function addresses have the low bit set.
    auto *code = reinterpret_cast<const unsigned char *>(reinterpret_cast<uintptr_t>(fn) & ~uintptr_t(1));
    return std::memcmp(code + ins.offset, ins.bytes, ins.size) == 0;
}

bool set_property(SDL_Window *window, const char *name, const char *value) {
    void *shell = shell_surface(window);
    if (!shell) return false;
    marshal(shell, kSetProperty, name, value);
    return true;
}

}  // namespace lg_sdl

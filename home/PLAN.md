# Home screen: design and status

A tvOS-style home screen for the rooted LG C5 (webOS 10.3.1). It is an app launcher and nothing
more, written from scratch. [Lumen](https://github.com/ammar027/lumen-webos) was used only as a
reference for which Luna calls work; it has since been replaced and uninstalled.

## Look

Modelled on the tvOS home screen, with one change: the background is a wallpaper you choose, not
artwork that changes with the focused app.

```
┌──────────────────────────────────────────────────────────────┐
│                                  ┌─────────────────────────┐ │
│                                  │ 9:41    [⇥]  [◐]  [⚙]   │ │  status pill
│                                  └─────────────────────────┘ │
│                         wallpaper                            │
│                                                              │
│ ╭──────────────────────────────────────────────────────────╮ │
│ │ [ APP ] [ app ] [ app ] [ app ] [ app ] [ app ]          │ │  dock: frosted glass, 6 favourites
│ ╰──────────────────────────────────────────────────────────╯ │
│   [ app ] [ app ] [ app ] [ app ] [ app ] [ app ]            │  grid: top of it shows below the dock
└──────────────────────────────────────────────────────────────┘
```

- **Wallpaper:** full screen and still, chosen in Home Settings. At night (automatic from 7 PM to
  7 AM, or always) it dims with a cool tint and the dock glass turns smoky.
- **Status pill:** the clock, an **Inputs** button (an arrow into a screen: a menu of HDMI inputs with their CEC names;
  choosing one switches to it), a pair of switches for the **Control Center** (the overlay app in
  `control/`, which drops from under the pill) and a gear for **Home Settings**. Up from the dock
  reaches it. The Control Center draws it (`common/pill.cpp`), the only one of our apps on screen
  everywhere, so it looks the same over apps (there without the gear). Home still lays it out,
  moves focus through it and fades it out in the grid: every frame it publishes the pill's fade and
  focus springs in `/tmp/lgtv-shell.state` (`common/shell_state.h`), and OK on Inputs or the
  Control Center becomes a request there, which the Control Center picks up within a frame or two
  (Home launches it if it isn't running). The other way, the Control Center asks Home to open Home
  Settings (the gear clicked or chosen in its pill) or to move focus to the button it was on, and
  says when the pointer is over a button. Home reloads it, hidden, whenever it hasn't said it is
  alive for a second. Launching an app hides the pill at once, before LG switches.
- **Dock:** six favourite apps on frosted glass. Tiles are the app's colour with its icon
  centred; full-square store icons get a tile of their own edge colour so they look full-bleed.
- **Focus:** the focused tile grows 10% on a spring and lifts with a shadow. A small lean carries
  it in from the direction of travel (and bumps at the edges), and a highlight over tile and icon
  moves with that lean, resting once the tile is still. With the pointer, the tile leans toward it.
- **Grid:** all other apps, 6 per row. Moving down scrolls the page up while the wallpaper blurs
  and dims.
- **Edit mode:** hold OK (or press Play/Pause, or hold the pointer) on a tile for **Move** and
  **Hide**. A moving tile wiggles and the arrows carry it while the others slide aside.
- **Home Settings:** wallpaper (Left/Right, previewed live with a thumbnail), appearance, show
  hidden apps, reset layout.

### Why a still wallpaper is cheap

The frosted glass and the blurred background never need a live blur. When a wallpaper is loaded,
the app blurs a quarter-size copy once on the CPU. Glass panels show the matching part of that copy
and scrolling crossfades between the sharp and blurred versions, so the GPU only draws textures.

## How it works on the TV

A native app (`"type": "native"`, `bin/home`) in C++, built like Moonlight: it links against the
TV's own LG-built `libSDL2` (webOS port of 2.0.14, Wayland underneath) for the window and remote,
and `libGLESv2` for drawing. FreeType (through SDL_ttf), SDL_image, json-c and `libluna-service2`
also come from the TV. Everything on screen is a rounded rectangle drawn by one shader
(`common/gfx.cpp`, shared with `control/`), filled with a gradient, a texture, frosted glass, a soft shadow or a highlight.

- **Toolchain:** the homebrew webOS cross-compiler, buildroot-nc4
  (`arm-webos-linux-gnueabi_sdk-buildroot`, in `~/webos-toolchain`). The TV's userspace is
  **32-bit ARM** (armhf) although its kernel is 64-bit. Only SDL 2.0.14-era API may be used.
  libstdc++ is linked statically: the toolchain's GCC is newer than the TV's.
- **Sandbox:** the app manager runs developer apps as user `prisoner` (uid 6067), chrooted in
  `/var/palm/jail/<id>`, with only the `public` Luna permission group. `jailer_disabled` does not
  apply to developer apps. The jail's mounts live in the TV's main mount table, so at startup the
  app asks the Homebrew Channel's root `exec` service (callable from `public`) to bind the other
  apps' folders in read-only (for their icons) and `/var/lib/lgtv-home` read-write (layout,
  settings, user wallpapers). Nothing persists across reboots except that folder.
- **Luna:** directly through `libluna-service2`, registered anonymously (LG's SDL already holds the
  app's own name in-process), pumped once per frame on the GLib main context.
  `applicationManager/listLaunchPoints` (subscribed) and `applicationManager/launch` work from
  `public`, as does `eim/getAllInputStatus` (subscribed; the Control Center's Inputs menu uses it).
- **Remote:** Back and Exit reach the app through SDL's webOS access-policy hints instead of LG's
  "exit the app?" dialog; they arrive as webOS scancodes, as do Play (450) and the pointer's
  show/hide events. The Magic Remote pointer arrives as ordinary mouse events.
- **Being Home:** `appinfo.json` declares the `home` category and the app is the default home
  (`setDefaultApp`), so the Home button opens it and `tv/default-home` opens it at power-on. It is
  on the app manager's `keepAliveApps` list (a persistent config setting), so Home brings the
  running window back in about 50 ms instead of a two-second cold start.
- **Why not Flutter:** tried first. The TV's Flutter (3.22.2, used by LG Home) is LG-patched: an app
  compiled with upstream tools crashes on its first draw call. LG's public flutter-webos SDK
  (Flutter 3.38, bundles its own engine) only supports webOS 26 and later.

## Measured

- **Frame rate:** the UI layer is a 1920×1080 surface scaled to the 4K panel, which runs at
  60 Hz; 120 Hz only applies to 120 Hz video and game input. The home screen holds 60 fps, with
  the worst frame around 20 ms.
- **Memory:** about 50 MB resident (wallpaper and its blurred copy, icons, fonts).
- **Start:** the first frame is ready about 300 ms after the window opens, already complete:
  the saved layout and wallpaper, the app list from the last run (`/var/lib/lgtv-home/apps.json`)
  and every icon are loaded before it is drawn, and the live list from the app manager replaces it
  only if something changed. The TV then takes about 1 to 2 s to show a new window. Keep-alive
  avoids all of this while the TV is on, but the TV closes every app when it is switched off, so
  each power-on is a cold start.

## Not done yet

- **Tile art overrides:** hand-made 16:9 art for the most used apps; some icons with transparent
  corners (YouTube AdFree) still show a faint box.
- **Sunrise and sunset** for automatic appearance, instead of fixed hours.
- **Desktop build:** `./build.py --host` compiles with the system SDL2, but needs `sdl2_ttf` and
  `sdl2_image`, and has no TV services: the dock says it can't reach the app manager.
- **Input status:** the TV reports every HDMI port as connected, so the Inputs menu's dots don't
  yet show which ports have a signal.

## Folders

```
home/
  src/          C++ sources (main, home screen, wallpaper, icons, layout, apps); drawing, Luna,
                the pill's layout and the shared state are in ../common, shared with the Control Center
  assets/       wallpapers/ (bundled, packaged into the app)
  tv/           appinfo.json and app icons (packaged as-is)
  tools/        key.sh: press remote keys and capture the screen over ssh; install.sh
  build.py      cross build with CMake and .ipk packaging (../common/native_build.py; --host for a desktop build)
```

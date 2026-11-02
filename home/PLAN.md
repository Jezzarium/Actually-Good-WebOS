# Home screen plan

A tvOS-style home screen for the rooted LG C5 (webOS 10.3.1). It is an app launcher and nothing
more, written from scratch. [Lumen](https://github.com/ammar027/lumen-webos) is used only as a
reference for which Luna calls work.

## Look

Modelled on the tvOS home screen, with one change: the background is a custom wallpaper, not
artwork that changes with the focused app.

```
┌──────────────────────────────────────────────────────────────┐
│                                         ┌──────────────────┐ │
│                                         │ 9:41  [⎆ Inputs] │ │  status pill: clock + inputs button
│                                         └──────────────────┘ │
│                    custom wallpaper                          │
│                    (full screen, day/night variants)         │
│                                                              │
│ ╭──────────────────────────────────────────────────────────╮ │
│ │ [ APP ] [ app ] [ app ] [ app ] [ app ] [ app ]          │ │  dock: frosted glass, 6 favourites
│ ╰──────────────────────────────────────────────────────────╯ │
│   [ app ] [ app ] [ app ] [ app ] [ app ] [ app ]            │  grid: top of it shows below the dock
└──────────────────────────────────────────────────────────────┘
```

- **Wallpaper:** full screen and still. Day and night variants, switched automatically.
- **Status pill (top right):** the clock and an **Inputs** button. Pressing Up from the dock
  focuses the pill. The button opens a small panel listing HDMI inputs (live name, connected or
  not); choosing one switches to it.
- **Dock:** a rounded frosted-glass panel holding 6 favourite apps. Tiles are rounded,
  about 1.6:1. The focused tile grows and lifts, with a shadow and a moving shine.
- **Grid:** all other apps, 6 per row. The top edge of the first row shows below the dock. Moving
  down scrolls the whole page up: the wallpaper dims and blurs, and the dock scrolls away with
  the page, as on tvOS.

### Why a still wallpaper is cheap

The frosted glass and the blurred background when scrolling never need a live blur. When a
wallpaper is chosen, the app blurs it once and caches the result. The glass panel then shows the
matching part of the blurred copy, and scrolling crossfades between the sharp and blurred
versions. The GPU work is a texture draw, not a blur on every frame.

## Technology: native C++ with SDL2 and OpenGL ES 2, run as root

A native app (`"type": "native"`, `bin/home`), built like Moonlight: it links against the TV's
own LG-built `libSDL2` (webOS port of 2.0.14, Wayland underneath) for the window and remote
input, and `libGLESv2` for drawing. Everything else it needs is on the TV too: FreeType for text,
libpng/libjpeg/libwebp for images, `libluna-service2` for TV services. A launcher is a wallpaper,
a glass panel, about 40 image tiles and a little text, so it is textured quads and a few small
shaders; no UI framework is needed. The same code builds as a desktop window on the dev machine.

- **Toolchain:** the homebrew webOS cross-compiler, buildroot-nc4
  (`arm-webos-linux-gnueabi_sdk-buildroot`), in `~/webos-toolchain`. The TV is **32-bit ARM**
  (armhf). Only SDL 2.0.14-era API may be used, since that is what the TV ships.
- **Why not Flutter:** tried first. The TV's Flutter (3.22.2, used by LG Home) is LG-patched; an
  app compiled with upstream tools crashes on the first draw call. LG's public flutter-webos SDK
  (Flutter 3.38, bundles its own engine) only supports webOS 26 and later.
- **Sandbox:** the app manager runs developer apps as user `prisoner` chrooted in
  `/var/palm/jail/<id>`, with only the `public` Luna group (`jailer_disabled` does not apply to
  developer apps). The jail's mounts live in the TV's main mount table, so at startup the app asks
  the Homebrew Channel's root `exec` service (callable from `public`) to bind the other apps'
  folders in read-only, for their icons.
- **Luna:** directly through `libluna-service2`, registered anonymously (LG's SDL already holds
  the app's own name in-process). `listLaunchPoints` and `launch` work from `public`.
- **Home button:** a surface-manager key filter, like the one in `tv/settings-key`, sends the
  Home button to us directly. Also set as the default home app with `setDefaultApp`, so
  power-on works through `tv/default-home`.
- **Instant open:** started hidden at boot from `init.d` and kept running. Home only brings
  the window forward.

## Frame rate

The UI layer is a fixed 1920×1080 surface composited by surface-manager. It probably refreshes at
60 Hz even though the panel can do 120. The target is a steady 60 fps with no frame over 16 ms.
120 Hz is a bonus if Step 0 finds it is available.

## Steps

### 0. Tests (before writing the real app)
1. Set up the buildroot-nc4 toolchain.
2. Get a GLES2 test window running on the TV as a native app; measure the real refresh rate and
   frame times (`src/main.cpp` is this probe).
3. Test screen: wallpaper, glass dock, 30 tiles, focus animation, shine shader, scrolling grid.
4. Test Luna as root: list apps, launch an app, list and switch inputs. Test the Home key filter.
5. Measure cold start, time to bring the window forward, and memory use.

### 1. Core
- Layout: wallpaper, status pill, dock, grid, page scroll.
- App list from `listLaunchPoints`, kept up to date as apps are installed or removed. Launch
  through `applicationManager/launch`.
- D-pad focus on an explicit grid. The Magic Remote pointer focuses whatever it points at.
- Inputs panel from `com.webos.service.eim/getAllInputStatus`.
- One small module handles Luna, calling `libluna-service2` directly.

### 2. tvOS polish
- Focus: spring-animated scale (about 1.1×), shadow, a shine that follows the pointer, and a
  slight tilt (drifting gently when using the D-pad).
- Tile art: LG gives square icons and an `iconColor`. Default tiles put the large icon centred
  on that colour. An override folder holds hand-made art for the apps used most.
- Rendering rules: no live blur, images cached at their display size, animations limited to
  transform and opacity.

### 3. Edit mode
- Long-press OK: tiles wiggle; move apps, move them in or out of the dock, hide them.
- Saved as JSON in the app's data folder.

### 4. Settings (small)
- Wallpaper: built-in set or your own images, with a day and a night choice.
- Theme: light, dark or automatic (by sunrise/sunset for the TV's timezone, or fixed hours).
- Show hidden apps, reset layout.

### 5. Install on the TV
- Boot script in `tv/`, Home key filter, default home, kept running.
- `tv/revert.sh` updated to undo it.

## Folders

```
home/
  src/        C++ sources
  tv/         appinfo.json, icons (packaged as-is)
  build.py    cross build with CMake and .ipk packaging (--host for a desktop build)
  fake/       fake TV services for desktop runs
```

## Targets
- Steady 60 fps scrolling and moving focus.
- Home button to usable screen: under 100 ms when running, under 1 s from cold.
- Under 60 MB of memory.

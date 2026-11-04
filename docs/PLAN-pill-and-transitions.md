# Plan: one pill everywhere, and tvOS-style app transitions

Status: Part 1 implemented (branch `control-center`, 2026-11-03), except the pointer check with
a real Magic Remote (step 1.1); see "Part 1 as built". Part 2 implemented (branch `transitions`,
2026-11-03), except the Back-out shrink, which LG never lets happen; see "Part 2 as built".

Two pieces of work that share one idea: our apps (Home, the Control Center, and a script inside
LG's compositor) tell each other what they are doing, so each thing on screen is drawn by exactly
one of them.

1. **The pill** (clock, Inputs, Control Center, Home Settings) moves out of Home into the Control
   Center, which is the only one of our apps that is on screen everywhere. Home keeps driving it
   (fading it out when the page scrolls, focusing its buttons) through shared state. Inside apps,
   the Settings key opens the pill and the panel together, and the screen dims behind them as it
   does for Home's menus.
2. **Transitions:** a script loaded into surface-manager (LG's compositor) zooms the tile into the
   app when it opens and shrinks the app back into its tile when you go Home, as tvOS does.

---

## What we know (tested on the TV, 2026-11-03)

- **The key-filter hook runs our code inside the compositor with full access.** A script listed in
  the `com.webos.surfacemanager.keyFilters` config (how `tv/settingskey.js` already gets the
  Settings key) can reach the globals `compositor`, `views`, `viewState` and `Qt.createQmlObject`.
  A probe saw `views.fullscreen` (`StarfishFullscreenView`), `views.starfishOverlay`,
  `compositor.surfaceModel`, `compositor.activeSurface` and every view in `views`.
- **It is loaded live.** surface-manager subscribes to that config. A `setConfigs` that adds a file
  creates its loader at once and calls its `init()`. No restart is needed and no system file is
  patched. configd forgets the change at reboot, and `tv/settings-key` re-applies it at boot.
- **Animating an app's surface works.** A test script connected to `views.fullscreen.surfaceOpened(item)`
  and ran a `ParallelAnimation` (opacity 0→1, scale 0.85→1) on `item`. It fired for both the
  Homebrew Channel and Home coming back, and screen captures showed Home caught mid-animation. The
  test was removed and the TV rebooted.
- **LG's app switch is a hard cut.** `StarfishFullscreenView.__removeCurrentFullscreenItem` moves
  the outgoing surface into an invisible `hidden` FocusScope (`item.parent = hidden`) as the new one
  arrives. So an animation on the incoming surface plays over black, and anything that shows the
  outgoing app has to be captured *before* the switch.
- **Connections outlive the script.** Removing the file from the config destroys its loader object.
  Signal connections the script made to compositor objects may survive until surface-manager
  restarts, so the script must own its objects so that it can tear them down (below).
- **Overlay window properties** read by `StarfishOverlaySurface.qml` include `needFocus`,
  `windowPosX`, `windowPosY`, `moveAreaRegion`, `punchThroughRegion` and `windowPosition`.
- **surface-manager logs don't reach `/var/log/messages`.** To see what a compositor script does,
  have it make a harmless Luna call carrying the data, and watch with `ls-monitor` (the probe sent
  `applicationmanager/getAppInfo {"id":"lgtv.probe", "probe":{...}}`).

---

## Part 1: the pill lives in the Control Center

### Behaviour

| Where | Panel closed | Panel open |
|---|---|---|
| Home, top of the page | Pill drawn by the Control Center, driven by Home (focus, fade) | Pill + dim + panel |
| Home, scrolled into the grid | Pill faded out (Home sends alpha 0) | Pill fades back in + dim + panel |
| Any other app | Nothing (window off screen, as today) | Pill (no gear) + dim + panel |

- **Inside apps the pill has no Home Settings gear.** It shows the clock, Inputs and Control Center
  and is narrower. The pill stays right-aligned, so the panel under it doesn't move.
- **Inputs moves to the Control Center** so it works inside apps too. On Home, Inputs opens the
  same menu as today, now drawn by the overlay. Home Settings and the tile menu stay in Home.
- **The dim:** 30% black over the whole screen behind the panel (`home.cpp:906` uses
  `rgb(0x000000, 0.30f)`), fading with the panel. The pill is drawn above the dim: it is the bar
  the menus hang from and can hold focus. Home's own menus dim Home's window, which is below the
  overlay, so the pill isn't dimmed for those either: the same rule everywhere.
- **Focus inside apps:** the Settings key opens the panel with Brightness focused (as now). Up from
  the panel's top row moves to the pill, Left/Right moves between Inputs and Control Center, and
  Down returns to the panel. Back on the pill closes everything.
- **Focus on Home:** unchanged for the user. Home still owns the remote while the panel is closed,
  so Up from the dock focuses the pill. Home just publishes the focus for the overlay to draw.

### Shared state between Home and the Control Center

A small fixed-layout struct in a file both apps `mmap`: `/tmp/lgtv-shell.state`. Both jails share
the TV's `/tmp`, which is how `/tmp/lgtv-home-backdrop.bmp` already works. New header
`common/shell_state.h` (plus a `.cpp` for open/map/read/write):

```cpp
struct ShellState {
    uint32_t magic, version;
    uint32_t seq;               // seqlock: odd while a writer is mid-update

    // Written by Home.
    uint64_t home_alive_ms;     // CLOCK_MONOTONIC, every frame while Home is in front
    float    pill_alpha;        // what Home would have drawn: 1 - grid
    float    pill_focus[3];     // the focus springs' current values: Inputs, Control Center, gear
    uint32_t request_seq;       // bumped with each request, then Home launches the Control Center
    uint8_t  request;           // kOpenPanel, kOpenInputs

    // Written by the Control Center.
    uint64_t control_alive_ms;  // every frame, and every 500 ms while idle
    uint8_t  panel_open, inputs_open;
};
```

- **Home sends the springs' current values, not targets,** so the overlay draws exactly the frame
  Home would have drawn and the two can't drift apart. The lag is at most one frame (~16 ms),
  invisible on a fade.
- **Requests go through the state, then a launch.** The relaunch hook can't read launch params
  (`overlay.cpp`), so Home writes the request, bumps `request_seq`, then launches
  `com.lgtvtweaks.control` as today. On relaunch the overlay reads which thing to open. A relaunch
  with no new request (the Settings key) keeps today's meaning: toggle the panel.
- **Two writers, two halves.** Each app only writes its own fields, and the seqlock keeps readers
  from seeing a half-written frame. Home and the Control Center are both 32-bit ARM with the same
  struct, so plain `memcpy` under the seqlock is enough.

### Drawing the pill once

- **Shared drawing code:** new `common/pill.{h,cpp}`. The layout comes from the clock and
  "Inputs" text widths plus whether the gear is shown (the pill rect and button rects). Drawing
  takes the glass, alpha, focus values and glyphs. The clock string (`%-I:%M %p`) moves here too.
  Moved out of `Home::render` (`home/src/home.cpp:870-903`) and `Home::refresh_clock`.
- **Glass:** over Home, the Control Center uses Home's blurred wallpaper
  (`/tmp/lgtv-home-backdrop.bmp`, already loaded for the panel) with the pill's tint
  `rgb(0x0b1016, 0.38f)`, identical to Home's. Inside apps, the panel's solid dark glass
  (`0x15191f` inside a faint rim).
- **Panel position from the pill:** replace `kPanelRight`/`kPanelTop` (`control/src/panel.cpp:16`)
  with the pill's right edge and its bottom + 14, so the panel always hangs from the pill.
- **Icons:** copy `control.png` and `settings.png` from `home/assets/icons` into
  `control/assets/icons`, or render them from `home/tools/control-icon.svg` with
  `control/tools/make_icons.sh`.
- **Home stops drawing the pill.** It keeps the pill's layout (for focus order and pointer
  hit-testing) and publishes its state instead.

### Control Center states

Today it is `kShown` / `kHiding` / `kHidden` (`control/src/main.cpp`). New:

- **kOffscreen:** today's hidden. `windowPosX` 1920, no focus, waiting on events (50 ms timeout).
- **kPill:** Home is in front and the panel is closed. Window at `windowPosX` 0 with `needFocus`
  "false". It draws only the pill, and only when the shared state's `seq` or the clock changes;
  otherwise it doesn't swap, so the compositor has nothing new to composite. It polls the state
  each vsync while it changes and every 16 ms when idle (one memory read).
- **kOpen:** pill + dim + panel (or Inputs), `needFocus` "true". Same as today's `kShown`.

Moving between them:
- **Foreground changes**, from `/tmp/lgtv-control.foreground`, which `tv/settings-key` already
  writes. Home in front → kPill. Anything else → kOffscreen (after closing the panel at once, as
  `Panel::app_switched` does now).
- **Relaunch / request** → kOpen. Closing → kPill over Home, kOffscreen elsewhere.
- **Home not publishing:** `home_alive_ms` older than about 1 s while Home is said to be in front
  (Home crashed or is starting) → draw nothing until it publishes again.

### The pointer over Home

A full-screen transparent overlay over Home probably takes Magic Remote clicks meant for Home. The
fix is a Wayland input region: in kPill, input only over the pill rect; in kOpen, the whole screen
(so clicking outside the panel closes it, as now); in kOffscreen, empty.

- Get the `wl_surface` and `wl_display` from `SDL_GetWindowWMInfo` (`SDL_syswm.h`, Wayland
  member). Bind `wl_compositor` with our own `wl_display_get_registry` plus a roundtrip on a
  private event queue, so SDL's queue isn't touched. Then use `wl_compositor_create_region`,
  `wl_region_add`, `wl_surface_set_input_region` and `wl_surface_commit` (or let the next swap
  commit it).
- Put this in `control/src/overlay.cpp`, beside `set_property`, with the same "check, else run as a
  plain window" policy.
- **Test this first** (step 1.1 below). If LG's compositor ignores input regions, the fallback is
  for Home to forward pointer events: the overlay can't get them, so Home sees the pointer
  (overlay input region empty) and publishes "pointer over pill button N / clicked" in the shared
  state.

With the pointer over the pill in kPill, the overlay handles hover and clicks itself: hovering
lights a button, and clicking Control Center or Inputs opens it locally (kOpen). The gear writes a
request for Home (`kOpenHomeSettings`, a Home-side field), since Home Settings stays in Home.

### Keeping the Control Center loaded (dropping the 30 s wait)

- **Why there was a wait:** `tv/settings-key` sleeps 30 s at boot before preloading, "to let the
  boot-time app launches settle". That was written when the panel was a web app. The native app
  cold-starts in 0.5–0.7 s and uses about 21 MB.
- **Home starts it:** at startup Home touches `/tmp/lgtv-control.preload` (the existing "this
  relaunch is a preload" marker) and launches `com.lgtvtweaks.control` with `{"hidden": true}`.
  If the pill matters and `control_alive_ms` is older than 2 s while Home is in front, Home checks
  `applicationManager/running` and relaunches it hidden the same way, at most once every 10 s.
- **`tv/settings-key`:** remove the `sleep 30` (and `NO_BOOT_WAIT`), but keep the preload. The TV
  can boot into an input instead of Home ("after a reboot by fault bootd opens the last input"),
  and the Settings key must still be instant there. Keep the reload after foreground changes.
- **No fallback pill in Home.** If the Control Center has crashed, the pill is missing for the
  second it takes to come back.

### Steps

1. **Pointer spike.** Add the input region to `overlay.cpp`, keep the overlay on screen over Home
   with a test rectangle, and check with the real remote that Home's tiles still click and the
   rectangle catches clicks. Also check that an on-screen unfocused overlay over Home changes
   nothing else: the Home key, Home's keep-alive, LG's `closeOverlaidItems`.
2. `common/shell_state.{h,cpp}` and `common/pill.{h,cpp}`. Home publishes state and draws its pill
   through `common/pill` (unchanged on screen). Screenshot to compare with the current pill.
3. Control Center: kPill state, drawing the pill from Home's state, pill-derived panel position,
   the dim, no gear outside Home. Home stops drawing its pill.
4. Inputs menu in the Control Center (`eim/getAllInputStatus` is in the `public` group, already
   used by Home), requests from Home, focus between pill and panel.
5. Home starts and watches the Control Center; drop the 30 s sleep in `tv/settings-key`.
6. Docs: `control/README.md` (states, shared state, input region, glass), `home/PLAN.md` (the pill
   section), the root `README.md` table for `settings-key`.

### Part 1 as built

Where it differs from the above (`control/README.md` describes what is there):

- **Requests don't go through a launch.** Home writes the request; the Control Center polls the
  state (every 16 ms over Home) and opens it at once. Home launches it only if it hasn't said it
  is alive for a second, and it then reads the request as it starts. A relaunch within 1.5 s of a
  handled request is that launch, and is ignored.
- **Each half has its own sequence count,** since each has its own writer. Home also publishes
  `front` (false from the moment it launches an app, so the pill goes before LG's switch) and
  `preload_ms`: Home can't touch `/tmp/lgtv-control.preload` (root's file), so a relaunch within
  3 s of either marks a preload. The Control Center publishes `pointer_seq` (Home moves focus
  when the pointer comes onto a button, even the same one again) and requests to move Home's
  focus to a pill button as it closes.
- **No explicit kOffscreen/kPill/kOpen enum:** `control/src/main.cpp` derives the window's place,
  focus and input region from "Home in front" and "panel open/fading" each pass.
- **The Inputs menu is a root page of the panel** (480 px wide, no header), so it shares the
  panel's glass, highlight, pointer and keys.
- **Home's liveness threshold is 3 s,** not 1 s: a screen capture holds up every app's frames for
  1–2 s. Home renders (and publishes) while hidden too, so this only catches Home stopping.
- **No foreground file yet** (the first ~20 s after boot, before `tv/settings-key` runs) counts as
  Home in front, in both apps.
- **Exit closes the overlay** even over Home (LG closes every overlay on Exit); Home reloads it
  about 1.5 s later.

### Checks

- `control/tools/shot.sh` and `home/tools/key.sh -c`: pill pixel-identical to today's over Home;
  scrolling into the grid fades it with no visible lag; Up from the dock focuses it.
- Settings key in an app (YouTube, an HDMI input): pill without gear, dim, panel under the pill.
  Up reaches the pill, Inputs switches input, Back closes.
- `control/tools/latency/vis.sh`: opening is still 0.1–0.25 s. Home still holds 60 fps while
  scrolling with the overlay on screen (Home logs slow frames and fps).
- Kill the Control Center while on Home: the pill comes back within ~2 s. Power cycle: the pill
  is there as soon as Home is.

---

## Part 2: tvOS-style transitions in the compositor

### Behaviour

- **Opening from a tile:** the tile (its colour and icon, rounded) grows from its spot on Home to
  full screen in about 350 ms, then holds there, like a splash, until the app has drawn, then fades
  away to reveal it.
- **Going Home with the Home key:** the app's last frame shrinks into its tile's current spot on
  Home and crossfades into the tile as it lands.
- **Going Home with Back out of the app** (the app quits itself, so its picture is gone): the same
  shrink using the tile's art instead of the app's picture. If a snapshot from that Back press is
  available (see below), use that.
- **Video playing** (Netflix, YouTube, inputs): video is on a separate hardware plane that a
  snapshot can't see. Use the tile's art for the shrink.
- **Apps not opened from a tile** (inputs from the pill, LG's settings, launches from other apps)
  and going to anything but Home: LG's normal cut. Maybe later a short fade.

### Pieces

**1. Home publishes its tiles.** For each visible tile: app id, on-screen rect (with the focus
scale, as drawn), corner radius, colour (or edge colour for full-bleed icons), icon path. Two ways
to get it to the compositor:
- **A (preferred): a window property on Home's own surface.** Move `set_property` and
  `shell_surface` from `control/src/overlay.cpp` to `common/` so Home can set `lgtvTiles` (JSON,
  only the tiles on screen) when the page settles, and `lgtvLaunch` (one tile) just before
  launching. The script reads them from Home's surface item's `windowProperties`, which notifies
  on change (as `StarfishOverlaySurface.qml` does for `windowPosX`). No files, no extra permissions.
- **B (fallback): `/tmp/lgtv-home-tiles.json`**, read by the script with `XMLHttpRequest` on a
  `file://` URL (Qt 5 allows local reads; untested here). surface-manager runs outside the jails,
  so it sees the real `/tmp`.

Icon paths must be the real paths (`/media/developer/apps/...`, `/usr/palm/applications/...`),
since surface-manager loads them as root outside the jail.

**2. The compositor script.** Two files:
- `tv/Transitions.qml`: the real work, as a QML component. A full-screen `Item` layer with
  `Connections` to `views.fullscreen` (`surfaceOpened`) and to Home's surface item, the tile proxy
  (`Rectangle` + `Image`, rounded with `OpacityMask` from QtGraphicalEffects, which the TV has),
  the snapshot (`ShaderEffectSource`), and the animations.
- `tv/transitions.js`: the key-filter entry. `init()` looks for an existing child with
  `objectName: "lgtvTransitions"` and destroys it, so reloads don't stack. It then creates
  `Transitions.qml` with `Qt.createComponent("file:///var/lib/lgtv-tweaks/Transitions.qml")` under
  the views root, in the z-order just above the fullscreen view and below overlays (so the pill,
  volume OSD, notifications and LG's popups stay on top). The handler `handleTransitionKeys`
  observes Home and Back presses, tells the component, and always returns `KeyPolicy.NextPolicy`.
  Everything is wrapped in `try/catch`.

Because the component owns its `Connections`, destroying it disconnects everything. That makes
hot reloads safe and gives uninstall a clean off switch.

**3. Opening, step by step.**
1. Home sets `lgtvLaunch` and immediately launches the app (`home/src/main.cpp` `on_launch`).
2. The script sees the property change, puts the tile proxy over Home's tile and animates it to
   1920x1080: rect, radius → 0, icon scaling up and fading slightly. Easing like Home's springs
   (OutBack-ish, or a `SpringAnimation`).
3. LG switches to the app, hiding Home and maybe showing its splash (`StarfishImageSplash`). The
   proxy is above both.
4. When the app's surface has drawn, the proxy fades out over about 200 ms. Ideally that's a signal
   from the surface item; if there is none, `surfaceOpened(item)` plus a delay, or a
   `ShaderEffectSource` sampling to detect a non-black frame. A minimum hold of the expand time and
   a 5 s timeout apply.

**4. Closing, step by step (Home key).**
1. `handleTransitionKeys` sees the Home key go down while the foreground app has a tile. The script
   points a `ShaderEffectSource { live: false }` at `views.fullscreen.currentItem` and calls
   `scheduleUpdate()`. It's captured on the GPU at the next frame, while the app is still visible.
   No readback.
2. LG handles the key and brings Home back (about 50 ms, since Home stays loaded).
3. The script shows the snapshot full screen above Home and animates it into the tile's rect from
   `lgtvTiles` (Home hasn't scrolled while hidden). Radius grows to the tile's, and the snapshot
   crossfades into the tile proxy in the last third.
4. The proxy lands exactly on Home's tile and fades out. No need for Home to hide its tile if the
   proxy matches. If it doesn't quite, Home can hide that tile itself for the length of the
   animation when it comes back to the front (it knows which app it launched last). The script has
   no easy way to write to Home, since it can't write files.

**Closing with Back:** on every Back press while an app with a tile is in front, the script takes
the same cheap GPU snapshot. If Home comes to the front within about 1 s with that app gone, it
shrinks that snapshot; otherwise the snapshot is dropped. With no snapshot, it uses tile art.

**Video:** when `views.fullscreen.currentItem.windowProperties.playVideo` is set, or the foreground
video window manager reports video, skip the snapshot and use tile art.

### Install and uninstall

- `tv/install.sh`: copy `tv/transitions.js` and `tv/Transitions.qml` to `/var/lib/lgtv-tweaks/`.
- `tv/settings-key`: `insert_handler` also inserts
  `{"handler":"handleTransitionKeys","file":"/var/lib/lgtv-tweaks/transitions.js"}` at the
  **front** of the key-filter list. It has to see Home and Back before LG's `handleSystemKeys` /
  `handleHotkeys` act on them, and it never consumes them. It's idempotent like the Settings
  entry, re-applied at boot.
- `tv/uninstall.sh`: remove the entry (same `without ... handler == "handleTransitionKeys"` pattern
  as the Settings key), delete both files, and reboot (or tell the user to), since a loaded
  component lives until surface-manager restarts.
- Off switch without uninstalling: remove the entry with `setConfigs` and reboot. For a hot reload
  while developing, rename the file (the loader keys objects by file name) and re-insert it.
- Recovery if a bad script ever got stuck in boot: `ssh lgtv rm /var/lib/lgtv-tweaks/transitions.js`
  and reboot. A missing file means no loader, and settings-key still inserts the Settings key.

### Steps

1. **Snapshot spike.** A throwaway script that, on the Home key, snapshots the current app with
   `ShaderEffectSource` and shows it shrinking to a fixed rect over Home. Capture with a fast loop
   of `captureCompositorOutput`. Also check: what the snapshot shows for a web app, a native app
   and video; whether the capture really lands before LG hides the surface; and z-order against
   LG's splash and our overlay.
2. **First-frame spike:** find a signal or property on the surface item that marks its first drawn
   frame (inspect a `WebOSSurfaceItem` from a probe), else settle on the delay approach.
3. Home publishes tiles and launches (window properties, A). Move `set_property` into `common/`.
4. `Transitions.qml` + `transitions.js`: opening zoom.
5. Closing zoom with snapshot (Home key), tile-art fallback (Back, video), Back pre-snapshot.
6. Install/uninstall wiring; docs (root `README.md` TV scripts table, a new section in
   `home/PLAN.md`).

### Part 2 as built

Where it differs from the above (the README's "App transitions" describes what is there):

- **No `lgtvTiles`.** Home publishes only `lgtvLaunch`, the tile of the app last opened from it,
  again whenever that tile moves. That covers going Home from it (Home doesn't scroll while
  hidden), and a list of every tile could pass Wayland's 4 KB message limit.
- **Springs, not easing curves.** Size (with corner radius and icon size) and position each follow
  a spring (damping ratio and response, as in Apple's "Designing Fluid Interfaces"), so the card travels on a curve; responses 0.3–0.42 s, settling in about half a second. See the top of
  `Transitions.qml`. Closing lands with a slight bounce. Opacity fades stay short eases.
- **Home leaves the tile out** while the card is drawn over it, so there aren't two: on a launch,
  for one frame before it publishes `lgtvLaunch` (so the snapshot of Home under the zoom doesn't
  have it) and launches, and for 0.5 s when it gets the remote back (`SDL_WINDOWEVENT_FOCUS_GAINED`)
  with the foreground file still naming that app, until the shrinking card is on the tile.
- **Home behind the zoom is a snapshot.** LG hides Home as soon as the app's window opens, often
  within 100 ms, so the tile grew over black; Home is captured as the tile is chosen and drawn
  under it until it fills the screen.
- **First frame:** the app's surface `redraw` signal (QWaylandSurface) after `surfaceOpened`,
  or 100 ms, whichever comes first.
- **Snapshots on OK too:** apps ask "exit the app?" and quit on OK. But an app that quits opens
  LG Home, not ours (README, Known issues), so the Back path can't be seen yet.
- **Stacking:** `stackAfter` isn't callable from QML, so LG's fullscreen view and the views it
  stacks just above it (splash, spinners, HCAP, floating, boot popup) go to `z: -1` and the layer
  sits at `-0.5`; they go back to 0 when it stops.
- **Lifetime:** the layer keeps a guarded reference to the key filter's loader object and stops
  when surface-manager drops it (any change to the list, or uninstall). Calling `destroy()` from
  that moment never completes, so it stays, empty, until the next load replaces it or a reboot.
- **Snapshots are of LG's app view** (`views.fullscreen`), not of a surface item. Holding Home's
  surface in a ShaderEffectSource while LG took it off screen and put it back (Home pressed during
  the opening zoom) left it never drawn again: Home black, at 200 fps, until it restarted. The view
  never leaves the scene. The app snapshot's ShaderEffectSource is a visible pixel just off screen:
  at opacity 0 it is only drawn when something shows it, by which time LG has switched to Home.
- **Video:** `foregroundVideoWindowMgr.getForegroundVideoWindow()` on the fullscreen container
  lists the app while it plays video (tested with YouTube).
- **Debugging:** surface-manager's console doesn't reach any log. A throwaway key filter that
  `eval`s a root-owned file on a timer and sends results as a Luna call watched with
  `ls-monitor -f com.webos.starfish.keyfilters` worked as a REPL; don't ship it.

### Checks

- Captures through the whole open and close for YouTube, Netflix, the Homebrew Channel (web), a
  native app (Moonlight), and an app launched not from a tile (no zoom, nothing broken).
- Timing: from OK on a tile to the first visible change ≤ 1 frame. The open adds no delay to the
  app being usable (the proxy fades as soon as it has drawn). Home key → Home is not slower than
  today.
- Spam keys: Home during an opening zoom, OK on another tile during a closing one. Animations
  are cancelled and replaced, and the layer never stays on screen. Add a watchdog that clears it
  after 3 s whatever happens.
- Remote keys keep working with the script loaded. After uninstall plus reboot, LG behaves as
  stock.

---

## Order and risks

Part 1 first (steps 1.1–1.6), then Part 2 (2.1–2.6). Each part starts with a spike on its biggest
unknown:

| Risk | Spike | If it fails |
|---|---|---|
| The overlay steals the pointer from Home | 1.1 input region | Home forwards pointer state through the shared state |
| An on-screen overlay over Home changes LG's behaviour (Home key, `closeOverlaidItems`) | 1.1 | Pill only when the panel is open (back to drawing it in Home) |
| Snapshot is taken after LG hides the app | 2.1 | Tile art for every close |
| No first-frame signal | 2.2 | Fixed hold after `surfaceOpened` |
| A firmware update changes surface-manager's QML | — | Script checks the objects it uses exist (`views.fullscreen.surfaceOpened`, `currentItem`) and does nothing otherwise |

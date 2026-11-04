# Control Center (`control/`)

A native replacement for the web Quick Settings that used to be in `settings/`: picture, sound and power settings
in a compact panel over whatever is playing, in the home screen's design. It also draws Home's
status pill and its Inputs menu. C++ with SDL2 and OpenGL ES 2 like `home/`, sharing its drawing
and Luna code, the pill and the state the two apps share (`common/`).

```
src/
  main.cpp       window, the pill, showing and hiding, the event loop
  overlay.cpp    what LG's SDL can't do (below)
  tv.cpp         the settings: catalogue, reads, throttled writes, the exec route, the cache
  panel.cpp      the panel and the Inputs menu: pages, focus, drawing
  bluetooth.cpp  Bluetooth audio devices
icons/           24px stroke glyphs (SVG); tools/make_icons.sh renders them to assets/icons
assets/icons/    those, and the pill's three glyphs (pill-*.png, 192 px; tools/pill-*.svg)
tools/           install.sh (quick install), shot.sh (keys + screenshot), latency/ (open timing)
```

## Hooks into LG's SDL

LG's SDL (the webOS port of 2.0.14 that ships on the TV) has no API for an overlay window, for
hiding one, or for noticing a relaunch. `src/overlay.cpp` and `common/lg_sdl.cpp` (window
properties, which Home also uses) reach into the library's private data for these, at offsets found by disassembling the copy on an LG OLED65C5PUA running
webOS 10.3.1:

| What | Where | Why |
|---|---|---|
| The window's `wl_webos_shell_surface` | `SDL_Window` + 144 → driver data, + 56 | To set window properties: `_WEBOS_WINDOW_TYPE` (overlay), `windowPosX` (on or off screen), `needFocus` (take or give back the remote). SDL's `SDL_HINT_WEBOS_WINDOW_GROUP_*` hints don't work: without a real surface group the window is closed. |
| SAM's relaunch | `SDL_VideoDevice` + 92 (a window function pointer) | LG's SDL answers a relaunch by calling this with the first window and sends no event. The app wraps it to learn that the Settings key was pressed again. |
| The input region | `SDL_GetWindowWMInfo` (public API) | Over Home the window covers the screen but should only take the Magic Remote's pointer over the pill. The app binds `wl_compositor` on a Wayland event queue of its own (SDL's stays untouched) and sets the surface's input region: the pill, the whole screen while the panel is open (a click outside closes it), or nothing. The sysroot's SDL config leaves Wayland out of `SDL_SysWMinfo`, so `overlay.cpp` defines `SDL_VIDEO_DRIVER_WAYLAND` itself; LG's build fills in the display and surface. |

Another build of the library (a firmware update, another webOS version, another model) may lay
these out differently. So before reading any of it the app compares the instructions that use
those offsets (in the exported `Wayland_CreateWindow` and `callback_RegistNativeApp`) with
the bytes they have in this build, checks that the shell surface really is a
`wl_webos_shell_surface` (`wl_proxy_get_class`), and that the relaunch slot points into
`libSDL2`. If anything differs it logs why and runs as an ordinary full-screen window that
quits when closed: the settings still work, but not over the picture. Updating it for a new
build means disassembling those two functions in that copy of `/usr/lib/libSDL2-2.0.so.0`
and changing the offsets and bytes in `overlay.cpp` and `common/lg_sdl.cpp`.

## The pill and Home

Our apps tell each other what they are doing so that each thing on screen is drawn by one of
them: the pill is drawn here, the only one of our apps on screen everywhere, and Home drives it.
They share `/tmp/lgtv-shell.state` (`common/shell_state.h`), a small struct both map; each app
writes only its own half, under a sequence count so a reader never sees half an update.

- **Home writes** every frame: that it is alive and in front (it says it isn't as soon as it
  launches an app, so the pill goes before LG switches), the pill's fade (0 once scrolled into the
  grid) and its three focus springs' current values, so the pill here is exactly the frame Home
  would have drawn, at most a frame later. OK on Inputs or the Control Center becomes a request
  (a count and what to open); this app polls it and opens within a frame or two, with no launch
  through the app manager. If this app hasn't said it is alive for a second, Home launches it
  instead (with the request in the state for it to read as it starts), and it reloads it hidden
  whenever it has been gone that long, at most every 10 s.
- **This app writes** that it is alive (every pass of its loop: at least every 50 ms), which pill
  button the pointer came onto (Home moves its focus there), and requests for Home: open Home
  Settings (the gear is Home's own sheet), or move focus to a pill button (where focus was when
  the panel closed).

What the window does:

| | Panel closed | Panel open |
|---|---|---|
| Home in front | On screen, without the remote (`needFocus` false). Draws only the pill, as Home publishes it, and only when that changes (or the minute does); otherwise it doesn't swap, so the compositor has nothing new to composite. Polls the state every 16 ms. Input region: the pill (none in the grid). | Pill (with the gear), the screen dimmed 30% behind it, the panel or Inputs menu under it. Takes the remote. Input region: the whole screen. |
| Any other app | Off screen (`windowPosX` 1920) and idle, waking every 50 ms. | The same, but the pill has no gear and dark glass. |

With the panel open, Up from its top row moves focus to the pill (the panel's highlight goes;
a pill button is lit only while focus is on the pill), Left/Right moves along it, Down goes back, OK on a button opens its menu (or closes it if it is
the open one), Back closes everything. The pill's focus starts from Home's and blends back into
Home's as the panel fades out. Requests and relaunches: a relaunch (the Settings key) toggles the
panel, unless it is a preload (`/tmp/lgtv-control.preload` from `tv/settings-key`, or Home's
`preload_ms`) or comes within 1.5 s of a request it already handled (Home launching it for that
request).

Not yet tried with the real Magic Remote: whether LG's compositor honours the input region (Home's
tiles still clickable with the overlay over them, the pill catching clicks). If it doesn't, the
fallback is an empty region and Home forwarding the pointer through the shared state.

The Exit key closes every overlay, this one included, even over Home; Home loads it again about
1.5 s later. The pill is missing until then.

## Staying resident

- **Hiding:** `needFocus` false gives the remote back at once; after the fade-out the window
  draws one empty frame and moves off screen (`windowPosX` 1920), so it costs the compositor
  nothing, unless Home is in front (above). Showing moves it back and takes the remote; that is
  the whole cost of opening.
- **App switches:** when another app comes to the front (Home included), surface-manager
  closes the overlay on screen (`views.starfishOverlay.close` in
  `StarfishViewStateController.qml`), and a drawn overlay counts as on screen even off it and
  unfocused. An overlay with the window property `keepWindowOnAppSwitch` "true" is left to
  handle that itself (`StarfishOverlaySurface.qml`, `StarfishViewContainer.qml`), so the app
  sets it and stays loaded. It is still closed at power-off. As the panel would otherwise stay
  open over the new app (and the compositor then draws it only every second or so), it
  closes at once when the foreground app changes: `tv/settings-key` writes the foreground app's
  id to `/tmp/lgtv-control.foreground`, and the open panel watches it.
  (Minimizing closes it at once; unmapping the surface, or swapping it for a new undrawn window,
  doesn't take it out of the overlay view without breaking the relaunch.)
- **`tv/settings-key` loads it** with `{"hidden": true}` at boot, and again after a foreground
  change if it isn't running (it was closed some other way, or crashed); Home does too while it is
  in front (above). A hidden start draws one empty frame off screen, so the surface is already
  mapped when it is next opened (over Home it draws the pill instead). The compositor gives a new
  overlay the remote even when `needFocus` is "false" (any string reads as true at creation)
  and only lets go on a change, so on that first focus the app sets it to "true" and back to
  "false" (about 30 ms).
- **Toggling:** the Settings key launches the app; a relaunch opens it, or closes it if it is
  open. A reload touches `/tmp/lgtv-control.preload` first (Home, which can't touch root's file,
  sets `preload_ms` in the shared state), and a relaunch within 3 s of that is ignored, so a
  reload reaching a copy still running can't open it.

## Glass

An overlay can't read what is drawn below it, so the panel and pill only show glass over Home:
Home saves its blurred wallpaper to `/tmp/lgtv-home-backdrop.bmp` (both apps' jails share the
TV's `/tmp`) whenever it loads one, and this app reloads it if it changed (on opening, and every
quarter second while Home is in front), so it matches Home's own menus and the pill matches what
Home drew (pixel for pixel, compared in screen captures). Over any other app it is one solid
dark colour inside a faint rim; the app tells by the foreground app's id in
`/tmp/lgtv-control.foreground` (above). Until Home has saved a wallpaper, it is dark over Home too.

(LG's `com.webos.service.capture/executeOneShot` can capture the video and UI planes, which
would allow a real blur, but only as root and in ~0.2 s, too slow to do before every opening.)

## Measured

`tools/latency/vis.sh com.lgtvtweaks.control` launches the app and captures the screen back to
back (about one capture every 110 ms). Over Home or an HDMI input, the panel is in the first or
second capture: about 0.1 to 0.25 s, fading in (unchanged with the pill: the dim starts in the
second capture, the panel is settled by 0.3 s). The web Quick Settings took 0.35 to 0.47 s
measured the same way. Cold (not loaded) it takes 0.5 to 0.7 s. The panel holds 60 fps; the web
panel ran at about 48 fps with ~200 ms hitches. Home still holds 60 fps scrolling with the pill's
window over it. About 21 MB resident, against about 40 MB for the web panel's renderer.

## Bluetooth

`com.webos.service.bluetooth2` isn't in the `public` group, so every call goes through exec, and
the device list (`device/getStatus`) is polled every 1.5 s while the page is open. Audio devices
are those with an `A2DP-sink` service or the audio/video device class. Connecting is
`a2dp/connect`, then sound output `bt_soundbar` (what LG's menu calls a Bluetooth device);
pairing a new one is `adapter/pair` with `subscribe`, listened to for up to 30 s.

Not yet tried with real headphones: discovery and the paired list work, but connecting and
pairing haven't been tested, nor whether `bt_soundbar` is the right output for headphones.

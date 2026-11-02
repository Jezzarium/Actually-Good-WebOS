# Control Center (`control/`)

A native replacement for the web Quick Settings that used to be in `settings/`: picture, sound and power settings
in a compact panel over whatever is playing, in the home screen's design. C++ with SDL2 and
OpenGL ES 2 like `home/`, sharing its drawing and Luna code (`common/`).

```
src/
  main.cpp       window, showing and hiding, the event loop
  overlay.cpp    what LG's SDL can't do (below)
  tv.cpp         the settings: catalogue, reads, throttled writes, the exec route, the cache
  panel.cpp      the panel: pages, focus, drawing
  bluetooth.cpp  Bluetooth audio devices
icons/           24px stroke glyphs (SVG); tools/make_icons.sh renders them to assets/icons
tools/           install.sh (quick install), shot.sh (keys + screenshot), latency/ (open timing)
```

## Hooks into LG's SDL

LG's SDL (the webOS port of 2.0.14 that ships on the TV) has no API for an overlay window, for
hiding one, or for noticing a relaunch. `src/overlay.cpp` reaches into the library's private
data for these, at offsets found by disassembling the copy on an LG OLED65C5PUA running
webOS 10.3.1:

| What | Where | Why |
|---|---|---|
| The window's `wl_webos_shell_surface` | `SDL_Window` + 144 → driver data, + 56 | To set window properties: `_WEBOS_WINDOW_TYPE` (overlay), `windowPosX` (on or off screen), `needFocus` (take or give back the remote). SDL's `SDL_HINT_WEBOS_WINDOW_GROUP_*` hints don't work: without a real surface group the window is closed. |
| SAM's relaunch | `SDL_VideoDevice` + 92 (a window function pointer) | LG's SDL answers a relaunch by calling this with the first window and sends no event. The app wraps it to learn that the Settings key was pressed again. |

Another build of the library (a firmware update, another webOS version, another model) may lay
these out differently. So before reading any of it the app compares the instructions that use
those offsets (in the exported `Wayland_CreateWindow` and `callback_RegistNativeApp`) with
the bytes they have in this build, checks that the shell surface really is a
`wl_webos_shell_surface` (`wl_proxy_get_class`), and that the relaunch slot points into
`libSDL2`. If anything differs it logs why and runs as an ordinary full-screen window that
quits when closed: the settings still work, but not over the picture. Updating it for a new
build means disassembling those two functions in that copy of `/usr/lib/libSDL2-2.0.so.0`
and changing the offsets and bytes in `overlay.cpp`.

## Staying resident

- **Hiding:** `needFocus` false gives the remote back at once; after the fade-out the window
  draws one empty frame and moves off screen (`windowPosX` 1920), so it costs the compositor
  nothing. Showing moves it back and takes the remote; that is the whole cost of opening.
- **SAM closes it** whenever another app is launched (Home included), even one on the
  keep-alive list, as long as its window has ever been drawn: a drawn overlay stays in
  `getForegroundAppInfo` (`extraInfo`), which the hidden web panel didn't. Minimizing closes
  it at once; unmapping the surface doesn't help (the compositor shows it again a second later).
- **So `tv/settings-key` reloads it** with `{"hidden": true}` a few seconds after every
  foreground change, if it isn't running. A hidden start draws one empty frame off screen, so
  the surface is already mapped when it is next opened. The compositor gives a new overlay the
  remote even when `needFocus` is "false" (any string reads as true at creation) and only lets go
  on a change, so on that first focus the app sets it to "true" and back to "false" (about 30 ms).
- **Toggling:** the Settings key and Home's button both just launch the app; a relaunch opens it,
  or closes it if it is open. The reload touches `/tmp/lgtv-control.preload` first, and a
  relaunch within 3 s of that is ignored, so a reload reaching a copy still running can't open it.

## Glass

An overlay can't read what is drawn below it, so the panel's glass shows Home's wallpaper:
Home saves its blurred copy to `/tmp/lgtv-home-backdrop.bmp` (both apps' jails share the TV's
`/tmp`) whenever it loads one, and the panel reloads it on opening if it changed. Over Home it
matches Home's own menus; over video it is still the wallpaper. Until Home has run since the TV
started, the panel is plain smoked glass.

## Measured

`tools/latency/vis.sh com.lgtvtweaks.control` launches the app and captures the screen back to
back (about one capture every 110 ms). Over Home or an HDMI input, the panel is in the first or
second capture: about 0.1 to 0.25 s, fading in. The web Quick Settings took 0.35 to 0.47 s
measured the same way. Cold (not loaded) it takes 0.5 to 0.7 s. The panel holds 60 fps; the web
panel ran at about 48 fps with ~200 ms hitches. About 21 MB resident, against about 40 MB for the
web panel's renderer.

## Bluetooth

`com.webos.service.bluetooth2` isn't in the `public` group, so every call goes through exec, and
the device list (`device/getStatus`) is polled every 1.5 s while the page is open. Audio devices
are those with an `A2DP-sink` service or the audio/video device class. Connecting is
`a2dp/connect`, then sound output `bt_soundbar` (what LG's menu calls a Bluetooth device);
pairing a new one is `adapter/pair` with `subscribe`, listened to for up to 30 s.

Not yet tried with real headphones: discovery and the paired list work, but connecting and
pairing haven't been tested, nor whether `bt_soundbar` is the right output for headphones.

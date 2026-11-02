# lgtv-shell

A faster, cleaner interface for a rooted LG webOS TV: our own settings menu and our own home screen.

Built and tested on an LG OLED65C5PUA (webOS 10.3.1, Chromium 120 web engine), rooted, with the
[Homebrew Channel](https://www.webosbrew.org/) and its SSH server.

| Folder | What it is |
|---|---|
| [`settings/`](settings) | **Quick Settings**: a replacement for LG's settings panel, opened by the remote's Settings button |
| [`tv/`](tv) | Scripts that run on the TV (boot hooks, the Settings-button handler) and `revert.sh` to undo everything |
| [`home/`](home) | **Home**: a tvOS-style home screen and the TV's default Home app, native C++ with SDL2 and OpenGL ES |

## Quick Settings (`settings/`)

An overlay panel on the right of the screen, drawn over whatever is playing.

- **Brightness first:** it opens with Brightness focused (LG's "OLED Pixel Brightness", the `backlight` key), so Left/Right changes it immediately.
- **Quick panel:** picture mode, eye comfort, sound mode, sound output, sleep timer, energy saving, screen off.
- **More settings:** grouped pages for Picture, Sound, OLED care & power, General and Accessibility, plus **All LG settings** as a fallback.
- **Fast:** plain JavaScript, no framework, about 40 KB of page code. It is preloaded at boot and hides instead of closing, so it opens in about 45 ms (a cold start of a web app takes about 0.8 s).
- **Light on the GPU:** no blur, no large shadows; only `transform` and `opacity` animate.

Settings are read and written through `luna://com.webos.settingsservice` exactly as LG's own app does. Allowed values come from `getSystemSettingDesc` on the TV, so the lists match the model and the current signal. Calls a developer app isn't allowed to make (sleep timer, screen off) go through the Homebrew Channel's root `exec` service.

```sh
cd settings
./build.py            # build/com.lgtvtweaks.settings_<version>_all.ipk
./build.py --card     # full-screen variant, if the overlay window is ever refused
```

Preview in a desktop browser with fake TV services: open `settings/preview/index.html` in Chromium.

Install on the TV (from this machine, with `ssh lgtv` set up as root):

```sh
ssh lgtv 'cat > /tmp/qs.ipk && luna-send -n 1 luna://com.webos.appInstallService/dev/install \
  "{\"id\":\"com.lgtvtweaks.settings\",\"ipkUrl\":\"/tmp/qs.ipk\"}" </dev/null' \
  < settings/build/com.lgtvtweaks.settings_*_all.ipk
```

## Home screen (`home/`)

A tvOS-style home screen and the TV's default Home app: wallpaper, a glass dock of six favourites,
all other apps in a grid below, and a status pill with the clock, an **Inputs** button and a
settings button. Native C++ with SDL2 and OpenGL ES 2, linked against the TV's own libraries: a
steady 60 fps and about 50 MB of memory. It is on the app manager's keep-alive list, so the Home
button brings it back in about 50 ms.

- **Remote:** arrows move focus, OK opens. Hold OK (or press Play/Pause) on a tile to **Move** or
  **Hide** it; while moving, the arrows carry it and OK or Back drops it. Back in the grid returns
  to the dock.
- **Magic Remote:** pointing focuses, clicking opens, the wheel scrolls a row at a time.
- **Home Settings** (the gear in the corner): wallpaper (Left/Right to switch, previewed live),
  appearance (night dims the wallpaper; automatic from 7 PM to 7 AM, always day, always night), show
  hidden apps, reset layout.
- **Inputs:** the panel lists the HDMI inputs with the names devices give over CEC; OK switches.
- **Wallpapers:** Beach and Big Sur are bundled (`home/assets/wallpapers/`). To add your own, copy
  JPEGs (ideally 1920x1080) to `/var/lib/lgtv-home/wallpapers/` on the TV and pick them in Home
  Settings. The layout and settings live in `/var/lib/lgtv-home/layout.json`.

Building needs the homebrew webOS toolchain,
[buildroot-nc4](https://github.com/openlgtv/buildroot-nc4) (`arm-webos-linux-gnueabi_sdk-buildroot`,
unpacked to `~/webos-toolchain` and relocated with its `relocate-sdk.sh`), plus `cmake` and `ninja`.

```sh
cd home
./build.py            # build/com.lgtvtweaks.home_<version>_all.ipk
tools/key.sh -c /tmp/cap.png DOWN    # press remote keys over ssh and capture the screen
```

Install it, then make it the default home and keep it resident:

```sh
ssh lgtv 'cat > /tmp/home.ipk && luna-send -n 1 luna://com.webos.appInstallService/dev/install \
  "{\"id\":\"com.lgtvtweaks.home\",\"ipkUrl\":\"/tmp/home.ipk\"}" </dev/null' \
  < home/build/com.lgtvtweaks.home_*_all.ipk
ssh lgtv 'luna-send -n 1 luna://com.webos.service.applicationmanager/setDefaultApp \
  "{\"category\":\"home\",\"appId\":\"com.lgtvtweaks.home\"}" </dev/null'
```

For keep-alive, add `com.lgtvtweaks.home` to `com.webos.applicationManager.keepAliveApps` with
`com.webos.service.config/setConfigs` (the original list is in `tv/keepAlive_before.json`).

See [`home/PLAN.md`](home/PLAN.md) for how it works on the TV (the app sandbox, Luna, why not
Flutter) and what is left to do.

## TV scripts (`tv/`)

Copied to `/var/lib/webosbrew/init.d/` on the TV, where the Homebrew Channel runs them at boot. Several LG settings only last until the next reboot or wake from standby, so these re-apply them.

| Script | What it does |
|---|---|
| `settings-key` + `settingskey.js` | Adds a key handler to surface-manager (`com.webos.surfacemanager.keyFilters`) so the Settings button opens Quick Settings, and preloads the app hidden. No system files are patched. |
| `default-home` | Opens the default Home app at power-on and keeps LG's home out of memory while something else is the default. |
| `trim-preloads` | Stops LG's preload manager keeping unused apps (browser, voice, Prime Video, Netflix, Live TV, LG Channels) in memory, at boot and after every wake. |
| `oyg-allow` + `dns-allow.txt` | An allowlist for [own-your-glass](https://github.com/JulioFerrero/own-your-glass)'s DNS blocking (keeps the LG Content Store working). |

`settingskey.js` and `dns-allow.txt` live in `/var/lib/lgtv-tweaks/` on the TV.

`tv/revert.sh` undoes everything: Settings button, Home and power-on, the keep-alive list, preloads, the allowlist, own-your-glass, and the privacy settings changed along the way. It uses the saved originals in `tv/*_before.json`. The apps themselves stay installed; remove them from the TV's app list (and `/var/lib/lgtv-home` for the home screen's layout and wallpapers).

## Notes for working on the TV

- `luna-send` prints nothing over SSH unless stdin is redirected: `luna-send -n 1 <uri> '<json>' </dev/null`.
- `/usr` and `/etc` are read-only. Changes go in `/var`, through LG's config service (`com.webos.service.config/setConfigs`), or as bind mounts.
- LG's settings schema is `/etc/palm/description.json` (about 1,950 settings with types, allowed values and dimensions).
- `ls-monitor` on the TV records live Luna traffic, useful for seeing exactly what LG's own menus call.

## Credits

- [Lumen](https://github.com/ammar027/lumen-webos) (MIT): reference for the home screen and for proven Luna calls. No code copied.
- [own-your-glass](https://github.com/JulioFerrero/own-your-glass) (MIT): installed alongside for privacy hardening.
- [buildroot-nc4](https://github.com/openlgtv/buildroot-nc4): the webOS cross-compiler the home screen is built with.
- Wallpapers: Big Sur is Apple's macOS Big Sur wallpaper; Beach is an aerial beach photo.

## License

[MIT](LICENSE)

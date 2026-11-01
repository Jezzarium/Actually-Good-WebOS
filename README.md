# lgtv-shell

A faster, cleaner interface for a rooted LG webOS TV: our own settings menu now, our own home screen next.

Built and tested on an LG OLED65C5PUA (webOS 10.3.1, Chromium 120 web engine), rooted, with the
[Homebrew Channel](https://www.webosbrew.org/) and its SSH server.

| Folder | What it is |
|---|---|
| [`settings/`](settings) | **Quick Settings**: a replacement for LG's settings panel, opened by the remote's Settings button |
| [`tv/`](tv) | Scripts that run on the TV (boot hooks, the Settings-button handler) and `revert.sh` to undo everything |
| `home/` | *Planned:* our own home screen, in the spirit of [Lumen](https://github.com/ammar027/lumen-webos) but written from scratch |

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

## TV scripts (`tv/`)

Copied to `/var/lib/webosbrew/init.d/` on the TV, where the Homebrew Channel runs them at boot. Several LG settings only last until the next reboot or wake from standby, so these re-apply them.

| Script | What it does |
|---|---|
| `settings-key` + `settingskey.js` | Adds a key handler to surface-manager (`com.webos.surfacemanager.keyFilters`) so the Settings button opens Quick Settings, and preloads the app hidden. No system files are patched. |
| `default-home` | Opens the default Home app at power-on and keeps LG's home out of memory while something else is the default. |
| `trim-preloads` | Stops LG's preload manager keeping unused apps (browser, voice, Prime Video, Netflix, Live TV, LG Channels) in memory, at boot and after every wake. |
| `oyg-allow` + `dns-allow.txt` | An allowlist for [own-your-glass](https://github.com/JulioFerrero/own-your-glass)'s DNS blocking (keeps the LG Content Store working). |

`settingskey.js` and `dns-allow.txt` live in `/var/lib/lgtv-tweaks/` on the TV.

`tv/revert.sh` undoes everything: Settings button, Home and power-on, preloads, the allowlist, own-your-glass, and the privacy settings changed along the way. It uses the saved originals in `tv/*_before.json`.

## Notes for working on the TV

- `luna-send` prints nothing over SSH unless stdin is redirected: `luna-send -n 1 <uri> '<json>' </dev/null`.
- `/usr` and `/etc` are read-only. Changes go in `/var`, through LG's config service (`com.webos.service.config/setConfigs`), or as bind mounts.
- LG's settings schema is `/etc/palm/description.json` (about 1,950 settings with types, allowed values and dimensions).
- `ls-monitor` on the TV records live Luna traffic, useful for seeing exactly what LG's own menus call.

## Credits

- [Lumen](https://github.com/ammar027/lumen-webos) (MIT): reference for the home screen and for proven Luna calls. No code copied.
- [own-your-glass](https://github.com/JulioFerrero/own-your-glass) (MIT): installed alongside for privacy hardening.

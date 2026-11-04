# Working on the TV

Things learned the hard way while developing on the TV (an LG C5, webOS 10.3.1) over SSH.

## Basics

- `luna-send` prints nothing over SSH unless stdin is redirected: `luna-send -n 1 <uri> '<json>' </dev/null`.
- `/usr` and `/etc` are read-only. Changes go in `/var`, through LG's config service (`com.webos.service.config/setConfigs`), or as bind mounts.
- LG's settings schema is `/etc/palm/description.json` (about 1,950 settings with types, allowed values and dimensions).
- `ls-monitor` on the TV records live Luna traffic, useful for seeing exactly what LG's own menus call.

## Logs

Under the app manager a native app's stdout and stderr go to `/dev/null`, and it runs as a jailed
user in a chroot (`/var/palm/jail/<id>`) with its own `/tmp`. LG's SDL also swallows `SDL_Log`
(`common/log.h` writes to stderr instead). To see output, run the binary by hand over SSH:

```sh
XDG_RUNTIME_DIR=/tmp/xdg WAYLAND_DISPLAY=wayland-0 ./home
```

## Crashes

Core dumps land in the app's own folder:
`/media/developer/apps/usr/palm/applications/<id>/core.*`. Read them with the toolchain's gdb and a
sysroot of the TV's libraries. The binaries in `build/tv` are stripped; for an unstripped one,
touch a source file and run `cmake --build <app>/build/tv`.

## Screenshots

- `luna-send -n 1 luna://com.webos.surfacemanager/captureCompositorOutput
  '{"output":"/tmp/cap.png","format":"PNG"}' </dev/null` (as root) captures the UI only: video is
  black. It also stalls every app's frames for 1–2 s, so don't capture mid-animation and trust the
  timing. `home/tools/key.sh -c` uses it.
- `com.webos.service.capture/executeOneShot` (`method` `DISPLAY`, `VIDEO` or `GRAPHIC`, plus
  `width`/`height`) includes video. Root only; about 0.25 s per call.
- The screensaver (`com.webos.app.screensaver`) can come up during long tests and cover the
  captures. Any key dismisses it.

## Input that can't be simulated

- Remote keys work through `com.webos.service.networkinput/sendSpecialKey` (`home/tools/key.sh`).
  No way was found to simulate a held key.
- A uinput keyboard is ignored: surface-manager only reads input devices that existed when it
  started.
- The Magic Remote pointer can't be simulated either. networkinput's `getPointerInputSocket`
  (`/tmp/netinput.pointer.sock`, a 4-byte big-endian length then `type:move\ndx:N\ndy:N\n\n`)
  creates a uinput device, which surface-manager ignores for the same reason. Test pointer
  behaviour by hand with the remote.

## Things to avoid

- **`systemctl restart bootd`.** It left eula-service crash-looping, and the TV then rebooted by
  fault at the next power-off. Reboot the whole TV instead, or reinstall the ipk to force a cold
  start of an app.
- **`pkill -f <pattern>` over SSH.** The pattern matches the SSH shell's own command line, so it
  kills itself.
- **`rm -rf` on a jail.** The jailer unmounts lazily and the jail's binds share mount peer groups
  with the real app folders; remove leftover jail directories with `rmdir` only
  (`tv/uninstall.sh` does this).

## Remote pointer setting

The Magic Remote pointer has an on/off switch: settingsservice `general` / `remotePointer`
(`"on"` or `"off"`). LG offers it only in Easy Mode; the Control Center shows it anyway
(More settings → General).

- `/usr/lib/qml/KeyFilters/systemUi.js` (`cb_systemProfileStatus`) sets it back to `"on"` whenever
  Easy Mode (systemprofile `seniorMode`) is off, and that runs when surface-manager starts. So
  `"off"` most likely doesn't survive a reboot; a boot script would have to set it again.
- Not yet confirmed with the remote in hand that `"off"` really hides the cursor.
- lginput2 (`com.webos.service.mrcu`) and surface-manager (`StarfishViewAdjustments.qml`) both
  subscribe to it. mrcu also has private calls (`activateMotionSensor`, `setHotkeyCursorPolicy`,
  `cursor/setWakeUpCondition`) whose parameters are unknown.

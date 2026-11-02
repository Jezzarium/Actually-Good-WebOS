#!/bin/sh
# Undo tv/install.sh, from this machine: tv/uninstall.sh
#
# Gives the Home button, power-on and the Settings key back to LG, removes the
# boot scripts and both apps. The home screen's layout and wallpapers in
# /var/lib/lgtv-home stay, for a reinstall, unless --purge.
#
#   --purge  also delete /var/lib/lgtv-home
# Environment: LGTV_HOST, the ssh host of the TV (default lgtv, logged in as root).
set -e
cd "$(dirname "$0")/.."
HOST=${LGTV_HOST:-lgtv}
HOME_ID=com.lgtvtweaks.home
SETTINGS_ID=com.lgtvtweaks.settings
purge=
for arg; do
    case "$arg" in
        --purge) purge=1 ;;
        *) echo "usage: $0 [--purge]" >&2; exit 2 ;;
    esac
done

tv() { ssh "$HOST" "$@"; }
# luna-send only prints over ssh when stdin is redirected.
luna() { tv "luna-send -n 1 '$1' '$2' </dev/null"; }
step() { echo "== $*"; }
# A shell function for the TV: kill a process and everything it started.
KILLTREE='killtree() { for c in $(pgrep -P "$1"); do killtree "$c"; done; kill "$1"; }'

# Prints the setConfigs payload for config $1 with the entries matching the
# Python expression $2 (on an entry e) left out, or nothing if none match.
without() {
    cur=$(luna luna://com.webos.service.config/getConfigs "{\"configNames\":[\"$1\"]}")
    printf '%s' "$cur" | python3 -I -c '
import json, sys
name, drop = sys.argv[1:]
items = json.load(sys.stdin)["configs"][name]
kept = [e for e in items if not eval(drop)]
if len(kept) != len(items):
    print(json.dumps({"configs": {name: kept}}, separators=(",", ":")))
' "$1" "$2"
}

step "boot scripts"
tv "$KILLTREE
for s in settings-key default-home trim-preloads; do
    rm -f /var/lib/webosbrew/init.d/\$s
    [ -f /tmp/\$s.pid ] && killtree \$(cat /tmp/\$s.pid) 2>/dev/null
    rm -f /tmp/\$s.pid
done; true"

step "Settings key back to LG"
# The handler goes out of surface-manager's list before its file is deleted.
new=$(without com.webos.surfacemanager.keyFilters 'e.get("handler") == "handleQuickSettingsKey"')
[ -n "$new" ] && luna luna://com.webos.service.config/setConfigs "$new"
tv 'rm -f /var/lib/lgtv-tweaks/settingskey.js; rmdir /var/lib/lgtv-tweaks 2>/dev/null; true'

step "Home button and power-on back to LG Home"
tv 'rm -f /var/webos-profile/override/lgtvtweaks_profile.json'
luna luna://com.webos.service.systemprofile/debug/rescan '{}'
luna luna://com.webos.service.applicationmanager/setDefaultApp '{"category":"home","appId":"com.webos.app.home"}'
# LG's default; LG Home then opens at power-on again.
luna luna://com.webos.settingsservice/setSystemSettings '{"category":"general","settings":{"homeAutoLaunch":"on"}}'
# default-home and trim-preloads turned these preloads off (netflix was already off).
for app in com.webos.app.home com.webos.app.lgchannels com.webos.app.browser com.webos.app.voice amazon com.webos.app.livetv; do
    luna luna://com.webos.service.preloadmanager/setPreloadPolicy "{\"application\":{\"id\":\"$app\",\"isEnabled\":true}}" >/dev/null
done
new=$(without com.webos.applicationManager.keepAliveApps "e == '$HOME_ID'")
[ -n "$new" ] && luna luna://com.webos.service.config/setConfigs "$new"

step "removing the apps"
luna luna://com.webos.applicationManager/launch '{"id":"com.webos.app.home"}'
for id in $HOME_ID $SETTINGS_ID; do
    luna luna://com.webos.applicationManager/closeByAppId "{\"id\":\"$id\"}" >/dev/null || true
done
# Home bind-mounts other apps' folders and /var/lib/lgtv-home into its jail
# (kJailMounts in home/src/main.cpp). Unmount those first, so removing the
# jail can't delete through them; the jailer's own mounts are its business.
tv 'sleep 2; J=/var/palm/jail/com.lgtvtweaks.home
for d in /var/lib/lgtv-home /media/cryptofs/apps/usr/palm/applications /mnt/otncabi/usr/palm/applications /usr/palm/applications; do
    while grep -q " $J$d " /proc/mounts; do umount "$J$d" || { echo "cannot unmount $J$d" >&2; exit 1; }; done
done'
for id in $HOME_ID $SETTINGS_ID; do
    luna luna://com.webos.appInstallService/dev/remove "{\"id\":\"$id\"}"
    echo
    # dev/remove only starts the removal; wait for the app's folder to go.
    tv "for i in \$(seq 1 60); do [ -e /media/developer/apps/usr/palm/applications/$id ] || exit 0; sleep 1; done
        echo 'still installed: $id' >&2; exit 1"
done
# Removing the app tears down the jail but leaves the empty mount points the
# app made for its own mounts. Delete them, once nothing is mounted there, so a
# reinstall gets a jail set up from scratch.
tv 'J=/var/palm/jail/com.lgtvtweaks.home
if [ -d $J ] && ! grep -q " $J/" /proc/mounts; then rm -rf $J; fi'

if [ -n "$purge" ]; then
    step "deleting /var/lib/lgtv-home"
    tv 'rm -rf /var/lib/lgtv-home'
fi
step done

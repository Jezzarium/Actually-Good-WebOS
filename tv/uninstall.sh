#!/bin/sh
# Undo tv/install.sh, from this machine: tv/uninstall.sh
#
# Gives the Home button, power-on and the Settings key back to LG, removes the
# boot scripts and both apps (and the web Quick Settings, if an older install
# left it). The home screen's layout and wallpapers in
# /var/lib/lgtv-home stay, for a reinstall, unless --purge.
#
#   --purge  also delete /var/lib/lgtv-home
# Environment: LGTV_HOST, the ssh host of the TV (default lgtv, logged in as root).
set -e
cd "$(dirname "$0")/.."
HOST=${LGTV_HOST:-lgtv}
HOME_ID=com.lgtvtweaks.home
CONTROL_ID=com.lgtvtweaks.control
OLD_SETTINGS_ID=com.lgtvtweaks.settings
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
for s in settings-key default-home remote-pointer trim-preloads; do
    rm -f /var/lib/webosbrew/init.d/\$s
    [ -f /tmp/\$s.pid ] && killtree \$(cat /tmp/\$s.pid) 2>/dev/null
    rm -f /tmp/\$s.pid
done
# What settings-key wrote for the Control Center.
rm -f /tmp/lgtv-control.foreground /tmp/lgtv-control.preload; true"

step "Settings key and app transitions back to LG"
# The handlers go out of surface-manager's list before their files are deleted.
# The transitions layer then stops and gives LG's views back; it is gone for
# good at the next reboot.
new=$(without com.webos.surfacemanager.keyFilters 'e.get("handler") in ("handleQuickSettingsKey", "handleTransitionKeys")')
[ -n "$new" ] && luna luna://com.webos.service.config/setConfigs "$new"
tv 'rm -f /var/lib/lgtv-tweaks/settingskey.js /var/lib/lgtv-tweaks/transitions.js /var/lib/lgtv-tweaks/Transitions.qml /var/lib/lgtv-tweaks/remote-pointer
rmdir /var/lib/lgtv-tweaks 2>/dev/null; true'

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
for id in $HOME_ID $CONTROL_ID $OLD_SETTINGS_ID; do
    luna luna://com.webos.applicationManager/closeByAppId "{\"id\":\"$id\"}" >/dev/null || true
done
# Home bind-mounts other apps' folders and /var/lib/lgtv-home into its jail
# (kJailMounts in home/src/main.cpp). Unmount those first, so removing the
# jail can't delete through them; the jailer's own mounts are its business.
# Closing can take a moment (and LG Home may open it again), so stop whatever
# still runs in the jail. Other apps' own mounts (an overlay per store app)
# show up inside the binds, and in the same peer group as the real ones:
# unmounting them there would also unmount the real ones. So make each bind
# private first, then unmount deepest first, with -l when one is still busy.
tv 'sleep 2; J=/var/palm/jail/com.lgtvtweaks.home
in_jail() { for p in /proc/[0-9]*; do [ "$(readlink $p/root 2>/dev/null)" = $J ] && echo ${p#/proc/}; done; }
for i in 1 2 3 4 5; do pids=$(in_jail); [ -z "$pids" ] && break; kill $pids 2>/dev/null; sleep 1; done
for d in /var/lib/lgtv-home /media/cryptofs/apps/usr/palm/applications /mnt/otncabi/usr/palm/applications /usr/palm/applications; do
    grep -q " $J$d[ /]" /proc/mounts || continue
    mount --make-rprivate "$J$d" || { echo "cannot make $J$d private" >&2; exit 1; }
    for m in $(awk -v p="$J$d" "\$2 == p || index(\$2, p \"/\") == 1 { print \$2 }" /proc/mounts | sort -r); do
        umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null || true
    done
    if grep -q " $J$d[ /]" /proc/mounts; then echo "cannot unmount $J$d" >&2; exit 1; fi
done'
for id in $HOME_ID $CONTROL_ID $OLD_SETTINGS_ID; do
    tv "[ -d /media/developer/apps/usr/palm/applications/$id ]" || continue
    luna luna://com.webos.appInstallService/dev/remove "{\"id\":\"$id\"}"
    echo
    # dev/remove only starts the removal; wait for the app's folder to go.
    tv "for i in \$(seq 1 60); do [ -e /media/developer/apps/usr/palm/applications/$id ] || exit 0; sleep 1; done
        echo 'still installed: $id' >&2; exit 1"
done
# Removing the app tears down the jail but leaves empty mount points (the
# app's own and some of the jailer's). Remove them, so a reinstall gets a jail
# set up from scratch. Only rmdir, which never deletes files: the jailer
# unmounts lazily, so a folder can still be a mount point after it vanishes
# from /proc/mounts, and rm -rf there could reach the real folder.
tv 'J=/var/palm/jail/com.lgtvtweaks.home
[ -d $J ] && ! grep -q " $J/" /proc/mounts && find $J -depth -type d -exec rmdir {} \; 2>/dev/null; true'

if [ -n "$purge" ]; then
    step "deleting /var/lib/lgtv-home"
    tv 'rm -rf /var/lib/lgtv-home'
fi
step done

#!/bin/sh
# Install the home screen and Quick Settings on a rooted TV (Homebrew Channel and
# its SSH server), from this machine: tv/install.sh
#
# Builds both apps, installs them, makes Home the TV's home (Home button and
# power-on), keeps it resident, sends the Settings key to Quick Settings and
# installs the boot scripts that re-apply what LG resets. Safe to run again,
# e.g. to update the apps. Undo with tv/uninstall.sh.
#
#   --trim-preloads  also install tv/trim-preloads (stops LG preloading unused apps)
#   --no-build       install the .ipk files already in home/build and settings/build
# Environment: LGTV_HOST, the ssh host of the TV (default lgtv, logged in as root).
set -e
cd "$(dirname "$0")/.."
HOST=${LGTV_HOST:-lgtv}
HOME_ID=com.lgtvtweaks.home
SETTINGS_ID=com.lgtvtweaks.settings
build=1 trim=
for arg; do
    case "$arg" in
        --trim-preloads) trim=1 ;;
        --no-build) build= ;;
        *) echo "usage: $0 [--trim-preloads] [--no-build]" >&2; exit 2 ;;
    esac
done

tv() { ssh "$HOST" "$@"; }
# luna-send only prints over ssh when stdin is redirected.
luna() { tv "luna-send -n 1 '$1' '$2' </dev/null"; }
step() { echo "== $*"; }
# A shell function for the TV: kill a process and everything it started.
KILLTREE='killtree() { for c in $(pgrep -P "$1"); do killtree "$c"; done; kill "$1"; }'

if [ -n "$build" ]; then
    step building
    home/build.py
    settings/build.py
fi
# The newest package of each app.
ipk() { ls -t "$1"/build/"$2"_*_all.ipk 2>/dev/null | head -1; }
for pkg in "$(ipk home $HOME_ID)" "$(ipk settings $SETTINGS_ID)"; do
    [ -f "$pkg" ] || { echo "no package in home/build or settings/build; run without --no-build" >&2; exit 1; }
done

# A jail left empty by an earlier uninstall: the jailer would reuse it instead of
# setting it up, and Home would start in an empty folder (see tv/uninstall.sh).
tv 'J=/var/palm/jail/com.lgtvtweaks.home
if [ -d $J ] && [ ! -e $J/bin ] && ! grep -q " $J/" /proc/mounts; then rm -rf $J; fi'

for app in "home $HOME_ID" "settings $SETTINGS_ID"; do
    set -- $app
    pkg=$(ipk "$1" "$2")
    step "installing $(basename "$pkg")"
    tv "cat > /tmp/$2.ipk && luna-send -n 1 luna://com.webos.appInstallService/dev/install \
        '{\"id\":\"$2\",\"ipkUrl\":\"/tmp/$2.ipk\"}' </dev/null; rm -f /tmp/$2.ipk" < "$pkg"
    echo
done

step "boot scripts"
scripts="settings-key default-home"
[ -n "$trim" ] && scripts="$scripts trim-preloads"
tv 'mkdir -p /var/lib/webosbrew/init.d /var/lib/lgtv-tweaks && cat > /var/lib/lgtv-tweaks/settingskey.js' < tv/settingskey.js
for s in $scripts; do
    tv "cat > /var/lib/webosbrew/init.d/$s && chmod 755 /var/lib/webosbrew/init.d/$s" < "tv/$s"
done

step "home at power-on and on the Home button"
# bootd opens the system profile's home app at power-on (see tv/default-home).
tv 'mkdir -p /var/webos-profile/override && cat > /var/webos-profile/override/lgtvtweaks_profile.json' < tv/home-profile.json
luna luna://com.webos.service.systemprofile/debug/rescan '{}'
luna luna://com.webos.settingsservice/setSystemSettings '{"category":"general","settings":{"homeAutoLaunch":"on"}}'
luna luna://com.webos.service.applicationmanager/setDefaultApp "{\"category\":\"home\",\"appId\":\"$HOME_ID\"}"

step "keep Home resident"
NAME=com.webos.applicationManager.keepAliveApps
cur=$(luna luna://com.webos.service.config/getConfigs "{\"configNames\":[\"$NAME\"]}")
new=$(printf '%s' "$cur" | python3 -I -c '
import json, sys
name, app = sys.argv[1:]
apps = json.load(sys.stdin)["configs"][name]
if app not in apps:
    print(json.dumps({"configs": {name: apps + [app]}}, separators=(",", ":")))
' "$NAME" "$HOME_ID")
if [ -n "$new" ]; then luna luna://com.webos.service.config/setConfigs "$new"; else echo already; fi

step "starting the boot scripts"
# Stop a copy already running (with its children: its luna-send subscription and
# the loop reading it), so an update takes effect. NO_BOOT_OPEN: the TV is in
# use, so default-home only keeps LG Home out of memory instead of opening Home.
for s in $scripts; do
    tv "$KILLTREE; [ -f /tmp/$s.pid ] && killtree \$(cat /tmp/$s.pid) 2>/dev/null; rm -f /tmp/$s.pid
        NO_BOOT_OPEN=1 /var/lib/webosbrew/init.d/$s </dev/null >/dev/null 2>&1"
done
luna luna://com.webos.applicationManager/launch "{\"id\":\"$HOME_ID\"}"
echo
step done

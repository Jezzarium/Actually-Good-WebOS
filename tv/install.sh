#!/bin/sh
# Install the home screen and the Control Center on a rooted TV (Homebrew Channel
# and its SSH server), from this machine: tv/install.sh
#
# Builds both apps (downloading Apple's SF Pro first if it isn't there yet;
# tools/get-sf-pro.sh), installs them, makes Home the TV's home (Home button and
# power-on), keeps it resident, sends the Settings key to the Control Center and
# installs the boot scripts that re-apply what LG resets. Safe to run again,
# e.g. to update the apps. Removes the web Quick Settings (an earlier version) the
# Control Center replaces. Undo with tv/uninstall.sh.
#
#   --trim-preloads  also install tv/trim-preloads (stops LG preloading unused apps)
#   --no-build       install the .ipk files already in home/build and control/build
# Environment: LGTV_HOST, the ssh host of the TV (default lgtv, logged in as root).
set -e
cd "$(dirname "$0")/.."
HOST=${LGTV_HOST:-lgtv}
HOME_ID=com.lgtvtweaks.home
CONTROL_ID=com.lgtvtweaks.control
OLD_SETTINGS_ID=com.lgtvtweaks.settings  # the web Quick Settings
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
    # SF Pro isn't in git (see tools/get-sf-pro.sh). Without it the apps build
    # with LG's own font, so a failed download only warns.
    if [ ! -f home/assets/fonts/SF-Pro-Display-Semibold.otf ] || [ ! -f control/assets/fonts/SF-Pro-Display-Regular.otf ]; then
        step "downloading SF Pro"
        tools/get-sf-pro.sh || echo "SF Pro not installed; the apps will use LG's font" >&2
    fi
    step building
    home/build.py
    control/build.py
fi
# The newest package of each app.
ipk() { ls -t "$1"/build/"$2"_*_all.ipk 2>/dev/null | head -1; }
for pkg in "$(ipk home $HOME_ID)" "$(ipk control $CONTROL_ID)"; do
    [ -f "$pkg" ] || { echo "no package in home/build or control/build; run without --no-build" >&2; exit 1; }
done

# A jail left empty by an earlier uninstall (see tv/uninstall.sh): clear it, so
# the jailer sets it up from scratch.
# Only empty folders go (rmdir), never files.
tv 'J=/var/palm/jail/com.lgtvtweaks.home
[ -d $J ] && [ ! -e $J/bin ] && ! grep -q " $J/" /proc/mounts && find $J -depth -type d -exec rmdir {} \; 2>/dev/null; true'

for app in "home $HOME_ID" "control $CONTROL_ID"; do
    set -- $app
    pkg=$(ipk "$1" "$2")
    step "installing $(basename "$pkg")"
    # dev/install only starts the install; follow its status until it's done,
    # and keep the package until then.
    tv "P=/tmp/$2.ipk L=/tmp/$2.install.log
        cat > \$P || exit 1
        luna-send -i luna://com.webos.appInstallService/dev/install \
            '{\"id\":\"$2\",\"ipkUrl\":\"/tmp/$2.ipk\",\"subscribe\":true}' </dev/null >\$L 2>&1 &
        pid=\$! ok=
        for i in \$(seq 1 120); do
            grep -q '\"state\":\"installed\"' \$L && { ok=1; break; }
            grep -qE '\"state\":\"[^\"]*fail|\"returnValue\":false' \$L && break
            sleep 1
        done
        kill \$pid 2>/dev/null; rm -f \$P
        [ -n \"\$ok\" ] && { echo installed; rm -f \$L; } || { echo 'install failed:'; tail -2 \$L; exit 1; }" < "$pkg"
done

if tv "[ -d /media/developer/apps/usr/palm/applications/$OLD_SETTINGS_ID ]"; then
    step "removing the web Quick Settings"
    luna luna://com.webos.applicationManager/closeByAppId "{\"id\":\"$OLD_SETTINGS_ID\"}" >/dev/null || true
    luna luna://com.webos.appInstallService/dev/remove "{\"id\":\"$OLD_SETTINGS_ID\"}"
    echo
fi

step "boot scripts"
scripts="settings-key default-home remote-pointer"
[ -n "$trim" ] && scripts="$scripts trim-preloads"
tv 'mkdir -p /var/lib/webosbrew/init.d /var/lib/lgtv-tweaks && cat > /var/lib/lgtv-tweaks/settingskey.js' < tv/settingskey.js
# The app transitions, loaded into surface-manager by settings-key.
for f in transitions.js Transitions.qml; do
    tv "cat > /var/lib/lgtv-tweaks/$f" < "tv/$f"
done
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

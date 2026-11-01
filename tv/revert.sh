#!/bin/sh
# Undo everything lgtv-tweaks changed on the TV. Run from this machine: ./revert.sh
set -e
cd "$(dirname "$0")"

# 0. Settings key back to LG's panel. Restore the original key filter list
# before anything deletes /var/lib/lgtv-tweaks/settingskey.js.
ORIG=$(python3 -I -c 'import json; d=json.load(open("keyFilters_before.json")); print(json.dumps({"configs": d["configs"]}, separators=(",", ":")))')
ssh lgtv "rm -f /var/lib/webosbrew/init.d/settings-key
luna-send -n 1 luna://com.webos.service.config/setConfigs '$ORIG' </dev/null >/dev/null 2>&1; echo settings key restored"

# 0a. Home button and power-on back to LG Home (Lumen stays installed; uninstall it from the TV's app list)
ssh lgtv 'rm -f /var/lib/webosbrew/init.d/default-home; [ -f /tmp/default-home.pid ] && kill $(cat /tmp/default-home.pid) 2>/dev/null
luna-send -n 1 luna://com.webos.service.applicationmanager/setDefaultApp "{\"category\":\"home\",\"appId\":\"com.webos.app.home\"}" </dev/null >/dev/null 2>&1
luna-send -n 1 luna://com.webos.settingsservice/setSystemSettings "{\"category\":\"general\",\"settings\":{\"homeAutoLaunch\":\"on\"}}" </dev/null >/dev/null 2>&1
luna-send -n 1 luna://com.webos.service.preloadmanager/setPreloadPolicy "{\"application\":{\"id\":\"com.webos.app.home\",\"isEnabled\":true}}" </dev/null >/dev/null 2>&1
echo home restored'

# 0b-pre. DNS allowlist (own-your-glass's restore below regenerates nothing, so just stop pruning)
ssh lgtv 'rm -f /var/lib/webosbrew/init.d/oyg-allow; rm -rf /var/lib/lgtv-tweaks; [ -f /tmp/oyg-allow.pid ] && kill $(cat /tmp/oyg-allow.pid) 2>/dev/null; echo allowlist removed'

# 0b. own-your-glass: same as its app's "Undo hardening". Undoing Variant C restarts
# ConnMan (Wi-Fi drops briefly), so run it detached and wait for it.
ssh lgtv 'F="OYG_NETWORK_BLOCK=1 OYG_NETWORK_STRICT=1 OYG_NETWORK_IPBLOCK=1 OYG_DNS_OVERRIDE=1 OYG_DNS_RESOLVER=1"
nohup sh -c "$F sh /var/lib/own-your-glass/oyg restore; echo EXIT=\$?" </dev/null >/tmp/oyg-restore.log 2>&1 &'
until ssh -o ConnectTimeout=5 lgtv 'grep -q "^EXIT=" /tmp/oyg-restore.log' 2>/dev/null; do sleep 5; done
echo "own-your-glass restored"

# 1. Hosts blocking
ssh lgtv 'rm -f /var/lib/webosbrew/init.d/block-telemetry /var/lib/webosbrew/telemetry-blocklist.txt /var/lib/webosbrew/hosts.blocked
grep -q " /etc/hosts " /proc/mounts && umount /etc/hosts; echo hosts restored'

# 2. Preload trimming (policy values from preload_policy_before.json)
ssh lgtv 'rm -f /var/lib/webosbrew/init.d/trim-preloads; [ -f /tmp/trim-preloads.pid ] && kill $(cat /tmp/trim-preloads.pid) 2>/dev/null
for app in com.webos.app.lgchannels com.webos.app.browser com.webos.app.voice amazon com.webos.app.livetv; do
    luna-send -n 1 luna://com.webos.service.preloadmanager/setPreloadPolicy "{\"application\":{\"id\":\"$app\",\"isEnabled\":true}}" </dev/null >/dev/null 2>&1
done; echo preloads restored'

# 3. Settings, back to their values before these changes
ssh -tt lgtv 'luna-send -n 1 luna://com.webos.service.settings/setSystemSettings "{\"category\":\"option\",\"settings\":{\"livePlus\":\"on\",\"livePromotion\":\"on\",\"webOSPromotionVideo\":\"on\"}}"; echo
luna-send -n 1 luna://com.webos.service.settings/setSystemSettings "{\"category\":\"general\",\"settings\":{\"adCookie\":\"on\",\"lmt\":\"off\"}}"; echo; exit'

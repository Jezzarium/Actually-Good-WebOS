#!/bin/sh
# Undo everything lgtv-tweaks changed on the TV. Run from this machine: ./revert.sh
set -e
cd "$(dirname "$0")"

# 0. Settings key back to LG's panel. Restore the original key filter list
# before anything deletes /var/lib/lgtv-tweaks/settingskey.js.
ORIG=$(python3 -I -c 'import json; d=json.load(open("keyFilters_before.json")); print(json.dumps({"configs": d["configs"]}, separators=(",", ":")))')
ssh lgtv "rm -f /var/lib/webosbrew/init.d/settings-key
luna-send -n 1 luna://com.webos.service.config/setConfigs '$ORIG' </dev/null >/dev/null 2>&1; echo settings key restored"

# 0a. Home button and power-on back to LG Home (our home screen stays installed; uninstall it from the TV's app list)
ssh lgtv 'rm -f /var/lib/webosbrew/init.d/default-home; [ -f /tmp/default-home.pid ] && kill $(cat /tmp/default-home.pid) 2>/dev/null
luna-send -n 1 luna://com.webos.service.applicationmanager/setDefaultApp "{\"category\":\"home\",\"appId\":\"com.webos.app.home\"}" </dev/null >/dev/null 2>&1
luna-send -n 1 luna://com.webos.settingsservice/setSystemSettings "{\"category\":\"general\",\"settings\":{\"homeAutoLaunch\":\"on\"}}" </dev/null >/dev/null 2>&1
rm -f /var/webos-profile/override/lgtvtweaks_profile.json
luna-send -n 1 luna://com.webos.service.systemprofile/debug/rescan "{}" </dev/null >/dev/null 2>&1
luna-send -n 1 luna://com.webos.service.preloadmanager/setPreloadPolicy "{\"application\":{\"id\":\"com.webos.app.home\",\"isEnabled\":true}}" </dev/null >/dev/null 2>&1
echo home restored'

# 0a2. Keep-alive list back to LG's (the home screen was added to it)
KEEP=$(python3 -I -c 'import json; print(json.dumps(json.load(open("keepAlive_before.json")), separators=(",", ":")))')
ssh lgtv "luna-send -n 1 luna://com.webos.service.config/setConfigs '$KEEP' </dev/null >/dev/null 2>&1; echo keep-alive restored"

# 0b. Our tweaks directory (settingskey.js)
ssh lgtv 'rm -rf /var/lib/lgtv-tweaks; echo tweaks removed'

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

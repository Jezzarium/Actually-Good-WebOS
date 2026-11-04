#!/bin/sh
# Installs the newest build/*.ipk on the TV and waits until it's installed
# (tv/install.sh does this for every app; this is for quick iterations).
set -e
cd "$(dirname "$0")/.."
HOST=${LGTV_HOST:-lgtv}
pkg=$(ls -t build/com.lgtvtweaks.home_*_all.ipk | head -1)
ssh "$HOST" 'ID=com.lgtvtweaks.home P=/tmp/home.ipk L=/tmp/home.install.log
cat > $P
luna-send -n 1 luna://com.webos.applicationManager/closeByAppId "{\"id\":\"$ID\"}" </dev/null >/dev/null 2>&1
luna-send -i luna://com.webos.appInstallService/dev/install "{\"id\":\"$ID\",\"ipkUrl\":\"$P\",\"subscribe\":true}" </dev/null >$L 2>&1 &
pid=$!; ok=
for i in $(seq 1 120); do
    grep -q "\"state\":\"installed\"" $L && { ok=1; break; }
    grep -qE "\"state\":\"[^\"]*fail|\"returnValue\":false" $L && break
    sleep 0.5
done
kill $pid 2>/dev/null; rm -f $P
[ -n "$ok" ] && echo installed || { tail -2 $L; exit 1; }' < "$pkg"

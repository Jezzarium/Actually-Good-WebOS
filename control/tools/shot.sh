#!/bin/sh
# shot.sh out.png [keys...]: press remote keys on the TV, then capture the screen here.
out=$1; shift
cmds=
for k in "$@"; do
    cmds="$cmds luna-send -n 1 luna://com.webos.service.networkinput/sendSpecialKey '{\"key\":\"$k\"}' </dev/null >/dev/null; sleep 0.4;"
done
ssh ${LGTV_HOST:-lgtv} "$cmds sleep 0.5; luna-send -n 1 luna://com.webos.surfacemanager/captureCompositorOutput '{\"output\":\"/tmp/cap.png\",\"format\":\"PNG\"}' </dev/null >/dev/null"
scp -q ${LGTV_HOST:-lgtv}:/tmp/cap.png "$out"

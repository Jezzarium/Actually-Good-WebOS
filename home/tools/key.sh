#!/bin/sh
# key.sh: press remote keys on the TV and optionally capture the screen, for
# testing the home screen from this machine.
#   tools/key.sh RIGHT DOWN ENTER        key names as LG's remote API knows them:
#                                        UP DOWN LEFT RIGHT ENTER BACK HOME ...
#   tools/key.sh -c out.png DOWN         capture the screen afterwards
# Keys go through com.webos.service.networkinput/sendSpecialKey (the path LG's
# phone remote uses): a virtual keyboard isn't picked up by surface-manager.
set -e
cap=
if [ "$1" = -c ]; then cap=$2; shift 2; fi
cmds=
for k in "$@"; do
    cmds="$cmds luna-send -n 1 luna://com.webos.service.networkinput/sendSpecialKey '{\"key\":\"$k\"}' </dev/null >/dev/null; sleep 0.35;"
done
if [ -n "$cap" ]; then
    cmds="$cmds sleep 0.4; luna-send -n 1 luna://com.webos.surfacemanager/captureCompositorOutput '{\"output\":\"/tmp/cap.png\",\"format\":\"PNG\"}' </dev/null >/dev/null"
fi
ssh lgtv "$cmds"
[ -n "$cap" ] && scp -q lgtv:/tmp/cap.png "$cap"
exit 0

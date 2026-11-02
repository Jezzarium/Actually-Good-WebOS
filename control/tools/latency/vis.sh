#!/bin/sh
# vis.sh <app id> [params JSON]: how soon an app's window shows. Launches it on
# the TV and captures the screen back to back for 1.6 s (one capture about every
# 110 ms), then prints the grey level of the panel's area in each capture: the
# first one that changes is when it appeared. Needs ffmpeg here.
set -e
D=$(dirname "$0")
params=${2:-'{}'}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
scp -q "$D/vis.py" "${LGTV_HOST:-lgtv}:/tmp/vis.py"
ssh "${LGTV_HOST:-lgtv}" "rm -rf /tmp/vis; python3 /tmp/vis.py $1 '$params' /tmp/vis && tar cf - -C /tmp vis; rm -rf /tmp/vis /tmp/vis.py" |
    tar xf - -C "$out"
"$D/vischeck.sh" "$out/vis"

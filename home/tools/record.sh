#!/bin/sh
# record.sh: record the TV screen to a GIF, for showing the UI in the README.
#   tools/record.sh RIGHT RIGHT DOWN 1.5 UP   press keys while recording; a number
#                                             pauses that many seconds
#   tools/record.sh -t 10                     record 10 s while you use the remote
#   tools/record.sh -o demo.gif -w 1280 ...   output file (demo.gif) and width (960)
#   tools/record.sh -g 0.25 RIGHT RIGHT ...   seconds after each key press (0.6)
# Frames come from surfacemanager's captureCompositorOutput in a loop, about
# 10 per second at 1920x1080; ffmpeg turns them into a GIF here. Only the UI
# layer is captured: video playing behind it comes out black.
set -e
out=demo.gif width=960 secs=8 gap=0.6
while getopts o:w:t:g: opt; do
    case $opt in
        o) out=$OPTARG ;;
        w) width=$OPTARG ;;
        t) secs=$OPTARG ;;
        g) gap=$OPTARG ;;
        *) sed -n '2,10s/^# \{0,1\}//p' "$0"; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

if [ $# -gt 0 ]; then
    acts=
    for k in "$@"; do
        case $k in
            [0-9]*) acts="$acts sleep $k;" ;;
            *) acts="$acts luna-send -n 1 luna://com.webos.service.networkinput/sendSpecialKey '{\"key\":\"$k\"}' </dev/null >/dev/null; sleep $gap;" ;;
        esac
    done
else
    acts="sleep $secs;"
    echo "recording for $secs s, use the remote now" >&2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Capture until the stop file appears, then report the frame count and elapsed time.
ssh lgtv "rm -rf /tmp/rec; mkdir /tmp/rec
( read s _ </proc/uptime; i=0
  while [ ! -e /tmp/rec/stop ]; do
      i=\$((i+1))
      luna-send -n 1 luna://com.webos.surfacemanager/captureCompositorOutput \"{\\\"output\\\":\\\"/tmp/rec/\$(printf %04d \$i).jpg\\\",\\\"format\\\":\\\"JPG\\\"}\" </dev/null >/dev/null
  done
  read e _ </proc/uptime; echo \$s \$e >/tmp/rec/time ) &
sleep 0.5; $acts sleep 1; touch /tmp/rec/stop; wait"
ssh lgtv 'cd /tmp/rec && tar cf - time *.jpg && rm -rf /tmp/rec' | tar xf - -C "$tmp"

n=$(ls "$tmp"/*.jpg | wc -l)
fps=$(awk -v n="$n" '{ printf "%.2f", n / ($2 - $1) }' "$tmp/time")
echo "$n frames at $fps fps" >&2

ffmpeg -loglevel error -y -framerate "$fps" -i "$tmp/%04d.jpg" \
    -vf "scale=$width:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a" \
    "$out"
echo "$out: $(du -h "$out" | cut -f1)" >&2

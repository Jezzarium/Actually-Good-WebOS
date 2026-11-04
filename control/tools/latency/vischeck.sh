#!/bin/sh
# vischeck.sh <dir>: mean grey level of the panel area (x 1340-1820, y 300-600) per capture.
d=$1
python3 -I -c 'import json,sys; [print(*t) for t in json.load(open(sys.argv[1]))]' "$d/times.json" | while read i s e; do
    f=$(printf '%s/%03d.jpg' "$d" "$i")
    v=$(ffmpeg -loglevel error -i "$f" -vf "crop=480:300:1340:300,scale=1:1:flags=area" -f rawvideo -pix_fmt gray - | od -An -tu1 | tr -d ' ')
    echo "$i start=${s}ms end=${e}ms grey=$v"
done

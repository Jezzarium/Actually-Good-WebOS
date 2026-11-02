#!/bin/sh
# Renders control/icons/*.svg (24px stroke glyphs, white) to 64px PNGs in
# control/assets/icons/, which the app tints when it draws them. Needs rsvg-convert.
set -e
cd "$(dirname "$0")/.."
for svg in icons/*.svg; do
    name=$(basename "$svg" .svg)
    rsvg-convert -w 64 -h 64 "$svg" -o "assets/icons/$name.png"
done
ls assets/icons

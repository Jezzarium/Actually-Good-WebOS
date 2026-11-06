#!/bin/sh
# Downloads Apple's SF Pro and puts SF Pro Display (Regular, Semibold) in
# home/assets/fonts and control/assets/fonts, where the apps pick it up over
# LG's own font (common/fonts.h). Rebuild and reinstall both apps afterwards.
#
# SF Pro is under Apple's license (shown in the installer), which doesn't
# allow redistributing it, so the fonts are gitignored, not committed.
# Needs 7-Zip's 7zz (or 7z) to open the .dmg; set SEVENZIP to point at one.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
Z=${SEVENZIP:-$(command -v 7zz || command -v 7z || true)}
[ -n "$Z" ] || { echo "needs 7-Zip (7zz or 7z), or SEVENZIP=/path/to/7zz" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
curl -fsSL -o SF-Pro.dmg https://devimages-cdn.apple.com/design/resources/download/SF-Pro.dmg
"$Z" x -y -odmg SF-Pro.dmg >/dev/null
"$Z" x -y -opkg dmg/SFProFonts.pkg >/dev/null
"$Z" x -y -opay "$(find pkg -name 'Payload*' | head -1)" >/dev/null
mkdir payload && (cd payload && cpio -idm --quiet < "$(find ../pay -type f | head -1)")

for app in home control; do
    mkdir -p "$ROOT/$app/assets/fonts"
    for weight in Regular Semibold; do
        cp "payload/Library/Fonts/SF-Pro-Display-$weight.otf" "$ROOT/$app/assets/fonts/"
    done
done
echo "SF Pro Display installed in home/assets/fonts and control/assets/fonts"

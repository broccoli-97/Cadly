#!/usr/bin/env bash
# Regenerate cadly.icns from cadly-icon.svg. The .icns is committed so the
# build never needs an SVG rasterizer; rerun this after editing the SVG.
# Uses rsvg-convert (brew install librsvg), falling back to qlmanage.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
svg="$here/cadly-icon.svg"
iconset=$(mktemp -d)/cadly.iconset
mkdir -p "$iconset"

render() { # size, output
  if command -v rsvg-convert >/dev/null; then
    rsvg-convert -w "$1" -h "$1" "$svg" -o "$2"
  else
    local tmp; tmp=$(mktemp -d)
    qlmanage -t -s "$1" -o "$tmp" "$svg" >/dev/null
    mv "$tmp/$(basename "$svg").png" "$2"
  fi
}

for entry in 16:16x16 32:16x16@2x 32:32x32 64:32x32@2x 128:128x128 \
             256:128x128@2x 256:256x256 512:256x256@2x 512:512x512 \
             1024:512x512@2x; do
  render "${entry%%:*}" "$iconset/icon_${entry#*:}.png"
done

iconutil -c icns "$iconset" -o "$here/cadly.icns"
echo "wrote $here/cadly.icns"

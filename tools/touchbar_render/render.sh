#!/bin/bash
# Renders the touch bar zones and hint pills for the four positions to PNG (host): render.sh <out dir>
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?out dir}
mkdir -p "$out"
g++ -std=gnu++11 -Wall -Werror -I "$root/include" -I "$root/lib/trmnl/include" "$root/tools/touchbar_render/render.cpp" \
  "$root/lib/trmnl/src/touchbar_layout.cpp" -o "$out/render"
"$out/render" "$out"
for f in "$out"/*.pgm; do convert "$f" "${f%.pgm}.png" && rm "$f"; done
rm "$out/render"

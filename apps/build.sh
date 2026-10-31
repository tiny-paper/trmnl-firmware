#!/bin/sh
# Builds apps into <out>/<name>.elf with the flags of Espressif's elf_loader (its elf_loader.cmake,
# project_elf): a stripped position-independent shared object with app_main as its entry, and
# app_info kept. Needs the Xtensa toolchain PlatformIO installs for the TRMNL_X env.
#   apps/build.sh                    every app in apps/ into apps/build/
#   apps/build.sh hello              one of them
#   OUT=dir apps/build.sh path/app   an app's source folder elsewhere (the sim tests' apps)
# An app folder may hold build.flags: -I folders and more .c files, relative to the folder, one or more per line
# (# starts a comment), for an app whose code lives in its own repository beside it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
tc=${XTENSA_TOOLCHAIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}
cc=$tc/xtensa-esp32s3-elf-gcc
strip=$tc/xtensa-esp32s3-elf-strip
out=${OUT:-$here/build}
mkdir -p "$out"
# What the X's loader resolves: elf_loader's export table plus src/app_heap.cpp's allocators
symtab=$here/../managed_components/espressif__elf_loader/src/esp_elf_symbol.c
[ -f "$symtab" ] || { echo "no $symtab: run pio run -e TRMNL_X once" >&2; exit 1; }
allowed=$(mktemp)
trap 'rm -f "$allowed"' EXIT
{ sed -n 's/.*ESP_ELFSYM_EXPORT(\([A-Za-z0-9_]*\)).*/\1/p' "$symtab"; printf 'malloc\ncalloc\nrealloc\n'; } > "$allowed"
# every folder with C sources, but build/ and common/ (not __pycache__ and such)
apps=${*:-$(cd "$here" && for d in */; do d=${d%/}; ls "$d"/*.c >/dev/null 2>&1 && echo "$d"; done |
  grep -v -e '^build$' -e '^common$')}
for app in $apps; do
  dir=$app
  [ -d "$dir" ] || dir=$here/$app
  name=$(basename "$dir")
  extra=
  if [ -f "$dir/build.flags" ]; then
    extra=$(cd "$dir" && sed 's/#.*//' build.flags | tr -s ' \t' '\n\n' | while read -r a; do
      [ -n "$a" ] || continue
      case $a in -I*) echo "-I$(realpath "${a#-I}")" ;; *) realpath "$a" ;; esac
    done)
  fi
  "$cc" -std=gnu11 -Os -Wall -Wextra -Werror -mlongcalls -I "$here/../include" -I "$here" \
    -nostartfiles -nostdlib -fPIC -shared -e app_main -fdata-sections -ffunction-sections \
    -Wl,--gc-sections -Wl,--undefined=app_info -Wl,--undefined=app_rodata_align -fvisibility=hidden \
    -Wl,--strip-all -Wl,--strip-debug -Wl,--strip-discarded \
    "$dir"/*.c $extra -o "$out/$name.elf" -lgcc
  "$strip" --strip-unneeded --remove-section=.comment --remove-section=.got.loc --remove-section=.dynamic \
    --remove-section=.xt.lit --remove-section=.xt.prop --remove-section=.xtensa.info "$out/$name.elf"
  # Every import must be in the loader's table, else the X refuses the app at start ("needs a function the
  # firmware lacks"; device 2026-10-31: the FPV app's compiler-made memmove call)
  missing=$("$tc/xtensa-esp32s3-elf-nm" -D --undefined-only "$out/$name.elf" | awk '{print $2}' |
    grep -v -x -F -f "$allowed" || true)
  if [ -n "$missing" ]; then
    rm -f "$out/$name.elf"
    echo "$name: the X's loader has no $(echo $missing | sed 's/ /, /g') (allowed: elf_loader's esp_elf_symbol.c, src/app_heap.cpp)" >&2
    exit 1
  fi
  echo "built $out/$name.elf ($(wc -c < "$out/$name.elf") bytes)"
done

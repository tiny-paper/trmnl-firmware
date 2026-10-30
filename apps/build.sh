#!/bin/sh
# Builds apps into <out>/<name>.elf with the flags of Espressif's elf_loader (its elf_loader.cmake,
# project_elf): a stripped position-independent shared object with app_main as its entry, and
# app_info kept. Needs the Xtensa toolchain PlatformIO installs for the TRMNL_X env.
#   apps/build.sh                    every app in apps/ into apps/build/
#   apps/build.sh hello              one of them
#   OUT=dir apps/build.sh path/app   an app's source folder elsewhere (the sim tests' apps)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
tc=${XTENSA_TOOLCHAIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}
cc=$tc/xtensa-esp32s3-elf-gcc
strip=$tc/xtensa-esp32s3-elf-strip
out=${OUT:-$here/build}
mkdir -p "$out"
apps=${*:-$(cd "$here" && ls -d */ | tr -d / | grep -v -e '^build$' -e '^common$')}
for app in $apps; do
  dir=$app
  [ -d "$dir" ] || dir=$here/$app
  name=$(basename "$dir")
  "$cc" -std=gnu11 -Os -Wall -Wextra -Werror -mlongcalls -I "$here/../include" -I "$here" \
    -nostartfiles -nostdlib -fPIC -shared -e app_main -fdata-sections -ffunction-sections \
    -Wl,--gc-sections -Wl,--undefined=app_info -Wl,--undefined=app_rodata_align -fvisibility=hidden \
    -Wl,--strip-all -Wl,--strip-debug -Wl,--strip-discarded \
    "$dir"/*.c -o "$out/$name.elf" -lgcc
  "$strip" --strip-unneeded --remove-section=.comment --remove-section=.got.loc --remove-section=.dynamic \
    --remove-section=.xt.lit --remove-section=.xt.prop --remove-section=.xtensa.info "$out/$name.elf"
  echo "built $out/$name.elf ($(wc -c < "$out/$name.elf") bytes)"
done

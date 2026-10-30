#!/bin/sh
# Builds each app in apps/<name>/ into apps/build/<name>.elf, with the flags of Espressif's
# elf_loader (its elf_loader.cmake, project_elf): a stripped position-independent shared object
# with app_main as its entry. Needs the Xtensa toolchain PlatformIO installs for the TRMNL_X env.
#   apps/build.sh            all apps
#   apps/build.sh hello      one
set -eu
here=$(cd "$(dirname "$0")" && pwd)
tc=${XTENSA_TOOLCHAIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}
cc=$tc/xtensa-esp32s3-elf-gcc
strip=$tc/xtensa-esp32s3-elf-strip
out=$here/build
mkdir -p "$out"
apps=${*:-$(cd "$here" && ls -d */ | tr -d / | grep -v -e '^build$' -e '^common$')}
for app in $apps; do
  srcs=$(ls "$here/$app"/*.c)
  "$cc" -std=gnu11 -Os -Wall -Wextra -Werror -mlongcalls -I "$here/../include" \
    -nostartfiles -nostdlib -fPIC -shared -e app_main -fdata-sections -ffunction-sections \
    -Wl,--gc-sections -Wl,--undefined=app_info -fvisibility=hidden -Wl,--strip-all -Wl,--strip-debug -Wl,--strip-discarded \
    $srcs -o "$out/$app.elf" -lgcc
  "$strip" --strip-unneeded --remove-section=.comment --remove-section=.got.loc --remove-section=.dynamic \
    --remove-section=.xt.lit --remove-section=.xt.prop --remove-section=.xtensa.info "$out/$app.elf"
  echo "built $out/$app.elf ($(wc -c < "$out/$app.elf") bytes)"
done

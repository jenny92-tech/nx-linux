#!/usr/bin/env bash
# INPUT:  Existing bogo-builder:dev image; this checkout, no game files.
# OUTPUT: ARM64 GNU Linux probes and the SDL/Vulkan game host under out/.
# POS:    Fixed-image Linux bring-up build, separate from Rudroid's build.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
image=bogo-builder:dev
game_build="${D2R_GAME_BUILD:-90447}"
case "$game_build" in 90447|94012) ;; *) echo "D2R_GAME_BUILD must be 90447 or 94012" >&2; exit 2 ;; esac
[ "$(docker image inspect "$image" --format '{{.Os}}/{{.Architecture}}')" = linux/amd64 ]
mkdir -p "$root/dm/linux/out"
if [ "${1:-}" = --host-only ]; then
  docker run --rm --pull=never --name d2r-linux-build --platform linux/amd64 \
    --cpus 2 --memory 4g --read-only --tmpfs /tmp:rw,nosuid,size=512m \
    --mount "type=bind,src=$root,dst=/src,readonly" \
    --mount "type=bind,src=$root/dm/linux/out,dst=/out" \
    --workdir /src/dm/linux "$image" \
    make --always-make --jobs=2 OUT=/out "GAME_BUILD=$game_build" /out/d2r-linux-host-probe
  exit
fi
headers="${D2R_VULKAN_HEADERS:?Set D2R_VULKAN_HEADERS to the Vulkan-Headers include directory}"
[ -f "$headers/vulkan/vulkan.h" ]
[ -f "$root/dm/linux/out/target-libs/libvulkan.so.1" ] || {
  echo 'Copy the target Vulkan loader to dm/linux/out/target-libs/libvulkan.so.1 first.' >&2
  exit 1
}
docker run --rm --pull=never --name d2r-linux-build --platform linux/amd64 \
  --cpus 2 --memory 4g --read-only --tmpfs /tmp:rw,nosuid,size=512m \
  --mount "type=bind,src=$root,dst=/src,readonly" \
  --mount "type=bind,src=$root/dm/linux/out,dst=/out" \
  --mount "type=bind,src=$headers,dst=/vulkan-headers,readonly" \
  --workdir /src/dm/linux "$image" \
  make --always-make --jobs=2 OUT=/out "GAME_BUILD=$game_build" CPPFLAGS=-I/vulkan-headers \
  'LDLIBS=/out/target-libs/libvulkan.so.1 -lm' all

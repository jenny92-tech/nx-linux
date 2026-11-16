#!/usr/bin/env bash
# INPUT: Explicit libGPUCounters source and the existing bogo-builder:dev image.
# OUTPUT: Optional ARM64 counter sampler, under out/gpu-counters-public-build/.
# POS: Fixed build environment; no download, image creation, device or game launch.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
source_root="$(cd "${1:?Usage: build_gpu_counters.sh LIBGPUCOUNTERS_SOURCE}" && pwd)"
test -f "$source_root/hwcpipe/include/hwcpipe/gpu.hpp"
test "$(docker image inspect bogo-builder:dev --format '{{.Os}}/{{.Architecture}}')" = linux/amd64
mkdir -p "$root/dm/linux/out"
docker run --rm --pull=never --name d2r-gpu-counters-build --platform linux/amd64 \
  --cpus 2 --memory 4g --read-only --tmpfs /tmp:rw,nosuid,size=512m \
  --mount "type=bind,src=$root,dst=/src,readonly" \
  --mount "type=bind,src=$source_root,dst=/hwcpipe-source,readonly" \
  --mount "type=bind,src=$root/dm/linux/out,dst=/out" \
  --workdir /src/dm/linux bogo-builder:dev bash -c '
    set -e
    cmake -S /src/dm/linux/gpu_counters -B /out/gpu-counters-public-build \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
      -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
      -DHWCPIPE_SOURCE=/hwcpipe-source
    cmake --build /out/gpu-counters-public-build --parallel 2
  '

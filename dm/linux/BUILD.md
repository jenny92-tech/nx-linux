# Build and validate

The native build needs an ARM64 GNU/Linux compiler/sysroot, SDL2 and zlib development libraries, Vulkan headers, Make and Python 3. Game files are not needed to compile the runtime.

## Existing cross-build environment

The maintained container entry reuses the existing `bogo-builder:dev` linux/amd64 image. It never pulls or creates an image. Set `D2R_VULKAN_HEADERS` to an existing Vulkan-Headers `include/` directory; the probe targets also need the target Vulkan loader at `dm/linux/out/target-libs/libvulkan.so.1`.

```sh
D2R_GAME_BUILD=94012 D2R_VULKAN_HEADERS=/path/to/Vulkan-Headers/include bash dm/linux/build.sh
```

The wrapper uses a read-only source mount, the fixed `d2r-linux-build` container name, 2 CPUs and 4 GiB. `--host-only` builds the diagnostic CPU host. Production runtime targets use `NDEBUG`, strip debug/symbol tables and embed their source revision, kind and game build. Probe executables remain diagnostic and cannot pass the public package gate.

## Other existing toolchains

On ARM64 Linux, or with another prepared cross sysroot, select the compiler and library locations explicitly:

```sh
make -C dm/linux CC=aarch64-linux-gnu-gcc GAME_BUILD=94012   CPPFLAGS=-I/path/to/Vulkan-Headers/include   SDL2_LIBS=/path/to/sysroot/lib/libSDL2.so ZLIB_LIBS=/path/to/sysroot/lib/libz.so   OUT=/path/to/build /path/to/build/d2r-linux-novideo
```

Use `CC=cc` only on a suitable ARM64 Linux host. Direct ABI requirements and the target's transitive SDL/Vulkan/driver dependencies still need checking; a successful link does not prove compatibility.

## Checks

Use Python 3.9–3.12 with the pinned preparation dependencies. A task-local environment keeps these separate from system packages:

```sh
python3 -m venv .scratch/checks/python
.scratch/checks/python/bin/python -m pip install --only-binary=:all: -r dm/linux/requirements-prepare.txt
.scratch/checks/python/bin/python -B -m unittest discover -s dm/linux/tests -p 'test_*.py'
```

Optional ARM64 instruction-emulation cases skip when Unicorn is unavailable. They use synthetic instructions and do not require game files.

```sh
python3 dm/linux/package_runtime.py --build-dir /path/to/build --out /path/to/new-runtime.zip --with-preparer
```

Packaging statically validates a complete ARM64 executable, its Release marker, kind, game tag and current source revision. It rejects a stale or diagnostic binary without executing it. Optional WSI libraries require separate provenance and target validation.

Synthetic Python tests use temporary fixtures. Hardware, original-game and driver acceptance are separate tasks. Do not start games or connect to devices for a source-only review.

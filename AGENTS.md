# NX Linux

ARM64 Linux compatibility runtime and offline preparation tools for a player's own D2R Switch files. Start in `dm/linux/`; shared host, graphics, Android service helpers and shader translation remain in their original directories.

## Boundaries

- Keep game files, keys, rewritten guest modules, generated shaders, saves, caches and diagnostic captures outside source control. Work only from explicitly selected inputs.
- The default public D2R adaptation is the 2 GB classic profile. A profile name is not hardware acceptance; preserve the distinction between the 4 GiB test device and smaller target machines.
- Visible launcher files use `Je-` and the NS suffix. Internal directories and existing player profile paths retain their identifiers.
- Public software packages use the explicit allowlists in `package_runtime.py` and `package_preparer.py`. Do not add directories by recursively collecting a prepared installation.
- Production runtimes require the source-matched Release identity. Diagnostic executables, incomplete ELF fixtures and stale source builds cannot pass the package gate.
- Normal launches retain bounded error logs. Detailed events, frames and memory reports require explicit diagnostics.
- Preserve all upstream licenses, attribution and documented source boundaries. Do not include personal device paths or raw experiment diaries in public documentation.

## Build and verification

Read [build instructions](dm/linux/BUILD.md) before compiling. Reuse an existing toolchain or the documented existing container; do not install toolchains, pull new images or change the system driver as a side effect of a routine check. Keep native build inputs unchanged during compilation.

Run synthetic tests from the repository root with `python3 -B -m unittest discover -s dm/linux/tests -p 'test_*.py'`. Hardware and original-game acceptance is separate; a source review does not authorize starting or deploying a game.

## Workspace lifecycle

| Content | Location |
|---|---|
| Maintained sources | `dm/`, `tools/`, repository documents |
| Fixed Linux build output | `dm/linux/out/` |
| Current audit or experiment | `.scratch/<component>/`, one active directory per component |
| Accepted delivery | `dist/`, one current package per product with checksums |

Keep unique originals and player profiles intact. Clear completed build staging and duplicate packages; never globally prune containers/caches or empty Trash. Source-only review must not read local credential stores, environment files or crash dumps.

## Navigation

| Path | Responsibility |
|---|---|
| `dm/linux/` | Linux service, desktop preparation, installation and regression tests |
| `dm/host/` | Guest loading, OS/service adaptation and shared runtime interfaces |
| `dm/gfx/` | NVN recording, Vulkan/GLES adaptation, texture handling and shader tools |
| `dm/android/` | Shared service/input helpers and retained upstream mobile frontend |
| `dm/ios/` | Retained upstream frontend and preparation helper sources |
| `dm/third_party/` | Upstream translators/support code with original licenses |
| `docs/` | Shared input, dependency and interface-fact documentation |
| `tools/` | Upstream build and preparation helpers |
| `SOURCE-ORIGINS.md` | Origin revisions and third-party license boundaries |
| `SECURITY.md` | Vulnerability reporting and private-input handling |

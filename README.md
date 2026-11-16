# NX Linux

ARM64 Linux compatibility runtime and offline preparation tools for explicitly supported Nintendo Switch titles, based on D2R Mobile. It adapts the original program's OS services and NVN graphics calls. Current title adapters cover D2R and Soul Knight NS 1.2.0; it is not a general Switch emulator.

中文说明：[README.zh-CN.md](README.zh-CN.md).

## Current scope

- Soul Knight NS 1.2.0 has a separate title adapter, Vulkan/GLES builds and source identity; see [its scope and build entry](dm/linux/soulknight_ns/README.md). Player launch and packaging belong to Launcher Pack.
- Linux Vulkan rendering with classic resource preparation for game builds 90447 and 94012.
- The default `rk3566-2gb` profile uses the flat classic menu. It is an adaptation target, not a claim of verified play on every 2 GB device.
- Recorded gameplay validation covers the MiniLoong RK3566/4 GiB configuration. Rocknix and other driver/firmware combinations need their own validation; 1 GiB support is not established.
- OpenGL ES remains experimental. A compatible ARM64 ABI alone does not establish graphics, input, audio or save compatibility.

## Prepare and install

1. Place your own matching base/update packages and keys in a private input directory.
2. Run `bash dm/linux/prepare-classic.sh --device rk3566-2gb --input /path/to/input --out /path/to/prepared`. Compact Chinese/English classic resources are the default; use `--resources full` only when needed.
3. Install the prepared files with a matching Release runtime, keeping the existing player profile. See [preparation](dm/linux/PREPARE.zh-CN.md) and [device paths](dm/linux/devices/README.md).

Game files, keys, rewritten game modules, generated shaders, saves and driver caches are not included. Preparation stays on the player's computer.

## Development

[Build](dm/linux/BUILD.md) · [Architecture and runtime policy](dm/linux/DEVELOPMENT.md) · [WSI integration](dm/linux/WSI.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)

The shared host, graphics and translator sources retain their upstream layout. Mobile frontend code remains for source continuity; this fork's release and verification scope is ARM64 Linux.

## Sources and licenses

Based on d2r-mobile, with source origins and third-party terms recorded in [SOURCE-ORIGINS.md](SOURCE-ORIGINS.md). The existing [MIT license](LICENSE) and upstream notices are retained. Optional Rudroid WSI components have their own source and license requirements. This project does not grant rights to the original game content.

# ARM64 Linux runtime and preparation

The native service adapts shared guest OS/NVN interfaces to Linux, SDL and Vulkan. Desktop tools prepare the player's matching game modules and classic resources; installation binds them to a Release runtime and an existing player profile.

## Contracts

- The default public NS entry uses the 2 GB classic adaptation. Keep profile targets separate from actual hardware validation.
- Prepare only from explicit private inputs. Compact resources are the default; retain optional resources only when explicitly selected.
- The user-facing preparation guide has three steps. Public software packages exclude game content, private inputs, diagnostics and experiment records.
- Preserve existing saves and settings. Validate paths before mutation and publish manifests/receipts atomically without following links.
- Production packaging requires the native Release identity, runtime kind, game build and current source revision. Keep probes outside this path.
- Normal startup is quiet apart from errors. `D2R_DIAGNOSTICS=1` explicitly enables detailed reports; frame capture requires a separate explicit count.
- Runtime sources, installer inputs, defaults and tests change together. Do not rewrite existing schema/version values merely to record an internal change.
- WSI is an optional separately licensed Rudroid component; see [WSI.md](WSI.md) and do not substitute system graphics drivers.

## Navigation

| Path | Responsibility |
|---|---|
| `BUILD.md` | Native toolchain, fixed-container entry and Release verification |
| `DEVELOPMENT.md` | Runtime architecture, owned resources and diagnostic policy |
| `PREPARE.zh-CN.md` | Private input preparation and installation |
| `DISTRIBUTE.zh-CN.md` | Public/private package boundaries |
| `devices/` | Reviewed profiles and installation path selection |
| `Makefile`, `build.sh` | Native build targets and existing-container entry |
| `build_revision.py`, `runtime_build.h`, `runtime_identity.py` | Source identity, embedded production marker and package validation |
| `package_runtime.py`, `package_preparer.py`, `device_release.py` | Explicit public source/software allowlists and private preparation selection |
| `prepare_classic.py`, `prepare_game.py` | Preparation orchestration and verified resumable stages |
| `classic_bundle.py`, `device_profiles.py`, `compact_classic.py` | Bundle, profile and resource contracts |
| `install_game.py`, `install_handheld_pack.py` | Installation, profile preservation and atomic receipts |
| `run-bringup.sh`, `portmaster-launch.sh.in`, `log-errors.py` | Runtime launch, PortMaster integration and bounded error logging |
| `tests/` | Synthetic Python and host C regression fixtures |
| `soulknight_ns/` | Separate Switch 1.2.0 title services, resolution guards and source-matched Vulkan/GLES builds |

The formal launcher is also integrated into [Launcher Pack](https://github.com/jenny92-tech/portmaster-launcher-pack/tree/main/ports/d2r_ns_2g). Directory identifiers and player profile locations remain independent of the visible `Je-` script name.

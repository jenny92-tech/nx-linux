# Runtime architecture and policy

## Data flow

Private Switch inputs are verified and prepared on the computer. The preparation pipeline rewrites the matching guest modules, builds classic resource/shader data and emits an identity-bound manifest. Installation selects explicit runtime, game, controller and player-profile paths; the Linux host loads the modules and adapts OS/NVN calls to SDL/Vulkan.

The shared host and graphics implementation remains in `dm/host` and `dm/gfx`. Linux services live here; input/filesystem helpers reused from `dm/android` stay behind their Linux platform guards.

## Ownership and safety

- Inputs are selected explicitly and remain separate from generated output.
- Stage reuse checks the input/output identities before accepting existing work.
- Archive names, bounds and overlap are checked before extraction.
- Existing player settings are seeded only on first installation; saves and identities remain in the selected profile.
- Receipt publication uses an exclusive temporary file and atomic replacement, rejecting symbolic links.
- Native resources remain owned until their actual GPU fence/CPU writeback boundary. Unknown layouts and unsupported mutations retain the conservative path.

## Build and release

Only production runtime targets define `DM_LINUX_PRODUCTION_BUILD`; they also require `NDEBUG`, a runtime kind and a source revision. The `.d2r_build` ELF section records these facts. `runtime_identity.py` checks the ELF structure, Release profile, kind, game tag and source match before packaging.

`build_revision.py` hashes maintained native sources and build policy. The resulting identity is a build fact, not a semantic format-version increment. Rebuild after native/source-policy changes; never edit an identity record to relabel an old binary.

## Logging and diagnostics

Default launches unset debug/service traces, frame capture and memory-report output. The formal launcher drains output through `log-errors.py`, retaining at most 64 KiB of matching error records. Normal transient guest work uses the OS temporary directory.

`D2R_DIAGNOSTICS=1` explicitly enables persistent per-session reports. Frame capture also needs `D2R_CAPTURE_FRAMES` set to a nonzero count. Keep captures and player data private; publish only a minimal sanitized error excerpt.

## Graphics scope

Runtime capability, game resource identity, actual driver extensions and display ownership all matter. Optional WSI is a process-local layer and does not replace the installed driver. Its Wayland surface wrapper also relies on a usable downstream interface; see [WSI.md](WSI.md).

Memory targets and rendering strategies are independent of complete-device acceptance. A smaller measured resident set, a limited scene or another board with the same SoC is not a general gameplay guarantee.

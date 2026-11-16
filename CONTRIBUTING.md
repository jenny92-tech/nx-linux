# Contributing

中文：[CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md).

Keep changes focused on ARM64 Linux compatibility, preparation and installation. Shared host/graphics changes must preserve the other platform guards and upstream attribution.

## Validation

Run the synthetic Python suite: `python3 -B -m unittest discover -s dm/linux/tests -p 'test_*.py'`. For native changes, use the [documented build](dm/linux/BUILD.md), validate the resulting Release identity and run the relevant host-side C fixtures. State the checks actually run and any missing prerequisites.

Games and hardware are optional private validation inputs. Use explicit paths and separate player profiles for experiments; do not infer gameplay support from a build, a directory name or a synthetic test. Preserve existing saves and preferences.

## Public changes

- Include no game content, keys, generated guest programs/shaders, player profiles, device captures or private machine paths.
- Keep production and diagnostic behavior separate. Failed inputs must not write partial output or follow links outside an owned installation.
- Update source/build identity and tests when the contract changes. Do not relabel an old binary to match new source.
- Keep documentation about current commands, behavior and limits. Consolidate experimental conclusions instead of appending per-run notes.
- Preserve licenses and source origins. Interface facts may be documented; the original game content remains private.

Report a problem with the device/OS, game build, runtime/source identity, selected graphics path and a minimal sanitized error excerpt. Do not upload full environment dumps, credentials, saves or prepared game packages. See [SECURITY](SECURITY.md) for security-sensitive reports.

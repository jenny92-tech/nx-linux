# Security

Use synthetic inputs when reporting extraction, path validation, installation or packaging issues. Describe the affected revision, expected boundary and a minimal reproduction.

Never attach real keys, credentials, game packages, generated guest code, saves, complete environment dumps or unfiltered device captures. A short sanitized error excerpt is enough to start a report. Use private vulnerability reporting when the hosting service makes it available.

## Maintained boundaries

- Extracted archive entries are validated before publication.
- Installation paths and receipts must not follow links outside the selected installation.
- Public software packages use fixed file allowlists and a source-matched Release runtime identity.
- Original game inputs are read privately by the preparation tools and do not enter source or public runtime packages.
- Default logging retains bounded errors; detailed diagnostics are explicit.

These checks protect their documented boundaries. They do not establish complete game compatibility or make an unknown graphics driver/firmware combination supported.

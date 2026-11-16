#!/usr/bin/env python3
# INPUT: User-owned 90447 Settings.json and an explicit new output path.
# OUTPUT: A separate minimum-quality JSON, preserving all unrelated preferences.
# POS: Computer-side profile preparation; no game files, keys or device actions.
"""Request twelve verified 90447 quality/detail settings at zero.

This changes neither render dimensions nor the private texture pack. A running
game does not reload this file automatically. Install it in a separate profile,
then verify LINUX_SETTINGS_VALUE and the rendered game on that device.
"""
import argparse
import hashlib
import json
from pathlib import Path

QUALITY = ('Texture Quality', 'Shadow Quality', 'Light Quality',
    'Atmospherics Quality', 'Transparency Quality', 'Vfx Quality',
    'Ambient Occlusion Quality', 'Anti Aliasing', 'Vfx Lighting Quality',
    'Character Detail', 'Environment Detail', 'Texture Anisotropy')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--settings', type=Path, required=True, help='original Settings.json')
    p.add_argument('--out', type=Path, required=True, help='new JSON; never overwrites an existing file')
    a = p.parse_args()
    raw = a.settings.read_bytes()
    if len(raw) > 65536:
        p.error('Settings.json exceeds 64 KiB')
    settings = json.loads(raw)
    if not isinstance(settings, dict) or any(k in settings and type(settings[k]) is not int for k in QUALITY):
        p.error('requires a JSON object with integer quality values')
    before = {k: settings.get(k) for k in QUALITY}
    settings.update({k: 0 for k in QUALITY})
    with a.out.open('x') as out:
        json.dump(settings, out, indent=2, ensure_ascii=False); out.write('\n')
    print(json.dumps(dict(source_sha256=hashlib.sha256(raw).hexdigest(),
        output_sha256=hashlib.sha256(a.out.read_bytes()).hexdigest(), requested_quality=0,
        previous_values=before, dimensions_changed=False, original_modified=False,
        runtime_values_verified=False), ensure_ascii=False, indent=2))


if __name__ == '__main__': main()

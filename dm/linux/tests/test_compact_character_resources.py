# INPUT: Synthetic menu dependencies, shared classic catalog and CASC addresses.
# OUTPUT: Regression checks for dependency closure and omitted-archive isolation.
# POS: Host-only tests for the character-menu resource preparer.
from pathlib import Path
import sys
import tempfile
import struct
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compact_character_resources import Source, fresh_location, references, select
import casc_overlay as casc


class CharacterResourcesTest(unittest.TestCase):
    def test_legacy_decimal_archive_uses_verified_payload_not_name_guess(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'data').mkdir()
            content = b'the retained localized dependency'
            raw = casc.blte_encode(content)
            key = casc.ekey_for(raw, 10, 1, 0)
            (root / 'data/0a-00000001.data').write_bytes(casc.blte_encode(b'different chunk'))
            (root / 'data/10-00000001.data').write_bytes(raw)
            source = Source.__new__(Source)
            source.root = root
            source.tvfs = key + struct.pack('>I', len(raw)) + b'\0' + struct.pack('>I', len(content))
            source.positions = {'locales/data/zhcn/example.bin': 0}
            row = [''] * 11
            row[8] = 'b:256K*=z'
            source.rows = {'locales/data/zhcn/example.bin': row}
            self.assertEqual(source.encoded(0), raw)
            self.assertEqual(source.decoded('locales/data/zhcn/example.bin'), content)

    def test_references_cover_json_escaping_mixed_case_and_utf16(self):
        content = (b'{"mesh":"DATA\\\\HD\\\\Character\\\\Player\\\\Amazon.model",'
                   b'"skeleton":"data/hd/character/player/amazon.skeleton"}\0' +
                   'Data\\HD\\VFX\\particles\\flame.particles'.encode('utf-16le'))
        self.assertEqual(references(content), {
            'data/hd/character/player/amazon.model',
            'data/hd/character/player/amazon.skeleton',
            'data/hd/vfx/particles/flame.particles'})

    def test_dependency_closure_preserves_animation_gear_and_summons(self):
        amazon = 'data/hd/character/player/amazon.json'
        animation = 'data/hd/character/player/amazon/intro.animations'
        summon = 'data/hd/character/enemy/valkyrie.json'
        summon_mesh = 'data/hd/character/enemy/valkyrie.model'
        gear = 'data/hd/items/armor/unseen_gear.texture'
        texture = 'data/hd/character/enemy/valkyrie.texture'
        alias = 'data/hd/character/enemy/valkyrie_alias.texture'
        classic = 'data/global/tiles/act1/grass.dt1'
        catalog = 'data/hd/texture_desc_cache.json'
        unused = 'data/hd/env/terrain/unused.texture'
        contents = {
            amazon: ('{"animations":"' + animation + '","summon":"' + summon + '"}').encode(),
            animation: b'unchanged animated bytes',
            summon: ('{"owner":"' + amazon + '","mesh":"' + summon_mesh + '"}').encode(),
            summon_mesh: texture.encode('utf-16le'),
            gear: b'gear', texture: b'texture', alias: b'texture', classic: b'classic',
            catalog: unused.encode(), unused: b'world',
        }
        scanned = []

        def decoded(name):
            scanned.append(name)
            return contents[name]

        positions = {name: i * 25 for i, name in enumerate(contents)}
        positions[alias] = positions[texture]
        source = SimpleNamespace(rows={name: [] for name in contents}, positions=positions, decoded=decoded)
        selected, report = select(source)
        self.assertEqual(selected, set(contents) - {unused})
        self.assertNotIn(catalog, scanned)  # A global catalog is not a menu dependency root.
        self.assertEqual(scanned.count(amazon), 1)  # Cycles must terminate.
        self.assertEqual(report['unavailable_in_source'], [])

    def test_missing_references_are_reported_without_adding_fictional_assets(self):
        name = 'data/hd/character/player/warlock.json'
        missing = 'data/hd/vfx/particles/missing.particles'
        source = SimpleNamespace(rows={name: []}, positions={name: 0}, decoded=lambda _: missing.encode())
        selected, report = select(source)
        self.assertEqual(selected, {name})
        self.assertEqual(report['unavailable_in_source'], [missing])

    def test_printable_binary_lengths_do_not_join_neighboring_references(self):
        root = 'data/hd/character/player/warlock.particles'
        mesh = 'data/hd/vfx/meshes/flame_lod0.model'
        texture_a = 'data/hd/vfx/textures/flame_a.texture'
        texture_b = 'data/hd/vfx/textures/flame_b.texture'
        bank = 'data/hd/character/enemy/imp/animation/combined.animations'
        timeline = 'data/hd/character/enemy/imp/animation/combined.timelines'
        contents = {
            root: (b'data/hd/vfx/meshes/flame.model1' + texture_a.encode() +
                   b'a' + texture_b.encode() + b'5scene.intersect_float3\0' +
                   b'data/hd/character/enemy/imp/animation/intro.animationX'),
            mesh: b'mesh', texture_a: b'a', texture_b: b'b',
            bank: b'animation', timeline: b'timing',
        }
        source = SimpleNamespace(rows={n: [] for n in contents},
                                 positions={n: i * 25 for i, n in enumerate(contents)},
                                 decoded=lambda n: contents[n])
        selected, report = select(source)
        self.assertEqual(selected, set(contents))
        self.assertEqual(report['unavailable_in_source'], [])

    def test_named_lighting_sidecars_and_shared_lod_catalog_remain_available(self):
        root = 'data/hd/character/player/warlock.frontend'
        ibl = 'data/hd/env/ibl/default_cubemap.ibls'
        diffuse = 'data/hd/env/ibl/default_cubemap.sh3'
        catalog = 'data/hd/global/assetlodmappingtable.json'
        units = 'data/hd/character/monsters.json'
        visual = 'data/hd/env/vis/act1_caves_desecrated.json'
        unused = 'data/hd/env/terrain/unused.texture'
        contents = {root: b'frontend', ibl: b'cube', diffuse: b'coefficients',
                    catalog: unused.encode(), units: unused.encode(), visual: unused.encode(),
                    unused: b'world'}
        source = SimpleNamespace(rows={n: [] for n in contents},
                                 positions={n: i * 25 for i, n in enumerate(contents)},
                                 decoded=lambda n: contents[n])
        selected, report = select(source)
        self.assertEqual(selected, set(contents) - {unused})
        self.assertEqual(report['unavailable_in_source'], [])

    def test_switch_compiled_models_and_animation_banks_remain_reachable(self):
        root = 'data/hd/character/player/druid.json'
        model = 'data/hd/character/enemy/bear/torso.model'
        animation = 'data/hd/character/enemy/bear/animation/intro.animation'
        lod = 'data/hd/character/enemy/bear/torso_lod2.model'
        bank = 'data/hd/character/enemy/bear/animation/combined.animations'
        timeline = 'data/hd/character/enemy/bear/animation/combined.timelines'
        texture = 'data/hd/character/enemy/bear/torso.texture'
        contents = {root: ('"' + model + '":"' + animation + '"').encode(), model: b'base mesh',
                    lod: texture.encode('utf-16le'), bank: b'animation', timeline: b'timing',
                    texture: b'texture'}
        source = SimpleNamespace(rows={n: [] for n in contents},
                                 positions={n: i * 25 for i, n in enumerate(contents)},
                                 decoded=lambda n: contents[n])
        selected, report = select(source)
        self.assertEqual(selected, set(contents))
        self.assertEqual(report['unavailable_in_source'], [])

    def test_archive_selection_excludes_absent_files_still_referenced_by_cft(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'data').mkdir()
            # Archive 1 has no file, but omitted TVFS entries still address it.
            cft = bytearray(25)
            cft[9:11] = bytes((0, 1))
            (root / 'data/00-00000002.data').touch()
            key = bytearray(16)
            key[9:11] = bytes((0, 3))
            source = SimpleNamespace(root=root, tvfs=cft, co=0, cs=25,
                                     config={k: '00 ' + key.hex() for k in ('vfs-1', 'vfs-root', 'vfs-2')})
            self.assertEqual(fresh_location(source), (0, 4))


if __name__ == '__main__':
    unittest.main()

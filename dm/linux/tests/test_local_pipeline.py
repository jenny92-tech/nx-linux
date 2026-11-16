# INPUT: Synthetic NSP/ticket/shader fixtures and public software source files.
# OUTPUT: Extraction boundaries, conditional shader guards and release separation.
# POS: Host-only reproducibility tests; no game, real keys or device required.
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_fixtures import make_runtime_elf
from extract_local import extract, pfs_entries, ticket_key
from install_game import render, verify_game
from package_runtime import package, WSI_FILES
from prepare_game import Pipeline, QUERY, KNOWN_EXCLUDED, known_exclusions, buffer_inventory, merge_caches, tree_files


def pfs(path, entries):
    strings, table, payload = b'', b'', b''
    for name, data in entries:
        table += struct.pack('<QQII', len(payload), len(data), len(strings), 0)
        strings += name.encode() + b'\0'
        payload += data
    path.write_bytes(b'PFS0' + struct.pack('<III', len(entries), len(strings), 0) + table + strings + payload)


def write_json(path, data):
    path.write_text(json.dumps(data), encoding='utf-8')


class ExtractTests(unittest.TestCase):
    def test_pfs_boundaries_and_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'dump.nsp'
            pfs(path, [('a.nca', b'ab'), ('b.tik', b'cd')])
            self.assertEqual([r[0] for r in pfs_entries(path)], ['a.nca', 'b.tik'])
            for name in ('../escape', '/absolute', 'C:x', 'a\\b', '.', 'a\nx'):
                pfs(path, [(name, b'a')])
                with self.assertRaises(ValueError):
                    pfs_entries(path)
            pfs(path, [('A.nca', b'a'), ('a.nca', b'b')])
            with self.assertRaises(ValueError):
                pfs_entries(path)
            pfs(path, [('a.nca', b'ab')])
            raw = bytearray(path.read_bytes())
            struct.pack_into('<Q', raw, 24, 100000)
            path.write_bytes(raw)
            with self.assertRaises(ValueError):
                pfs_entries(path)

    def test_common_ticket_and_personalized_ticket(self):
        data = bytearray(0x2c0)
        struct.pack_into('<I', data, 0, 0x10004)
        data[0x180:0x190], data[0x2a0:0x2b0] = b'\x33' * 16, b'\x44' * 16
        self.assertEqual(ticket_key(data), ('44' * 16, '33' * 16))
        data[0x281] = 1
        self.assertIsNone(ticket_key(data))
        with self.assertRaises(ValueError):
            ticket_key(data[:80])

    def test_failed_hactool_cleans_temporary_keys_and_hides_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = b'synthetic encrypted content'
            name = hashlib.sha256(payload).hexdigest()[:32] + '.nca'
            source, keys, output = root / 'dump.nsp', root / 'prod.keys', root / 'output'
            pfs(source, [(name, payload)])
            keys.write_text('dummy user input')
            result = type('Result', (), dict(returncode=0, stdout=b'Invalid NCA header! PRIVATE_VALUE'))()
            with patch('extract_local.subprocess.run', return_value=result):
                with self.assertRaises(ValueError) as caught:
                    extract([source], keys, None, Path('/tool/hactool'), output)
            self.assertNotIn('PRIVATE_VALUE', str(caught.exception))
            self.assertEqual(list(output.iterdir()), [])


class PipelineTests(unittest.TestCase):
    def test_known_exclusions_require_full_original_identity(self):
        key, full = next(iter(KNOWN_EXCLUDED.items()))
        self.assertEqual(len(known_exclusions([dict(key=key, full_sha256=full)])), 1)
        with self.assertRaises(ValueError):
            known_exclusions([dict(key=key, full_sha256='0' * 64)])
        self.assertEqual(known_exclusions([dict(key='unknown')]), [])

    def test_resume_verifies_outputs_and_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / 'pipeline'
            pipeline = Pipeline(root, False, {'input': 'version1'})
            def action(path):
                path.mkdir()
                (path / 'data').write_bytes(b'unchanged')
            stage = pipeline.stage('stage', action)
            reused = Pipeline(root, True, {'input': 'version1'})
            reused.stage('stage', lambda _: self.fail('should reuse'))
            (stage / 'data').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                reused.stage('stage', action)
            with self.assertRaises(ValueError):
                Pipeline(root, True, {'input': 'other'})

    def test_conditional_buffer_variant_preserves_runtime_guard(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            inv, spv, validation = (root / name for name in ('inv', 'spv', 'validation'))
            for path in (inv, spv, validation):
                path.mkdir()
            (inv / 'original').mkdir()
            key = 'a' * 16
            (inv / 'original' / (key + '.bin')).write_bytes(b'synthetic original')
            write_json(inv / 'inventory.json', dict(programs=[dict(key=key)]))
            write_json(inv / 'jobs.json', [dict(key=key, tex={'1': '2d'})])
            write_json(validation / 'validation.json', dict(records=[dict(key=key, validated=False)]))
            write_json(spv / (key + '.res.json'), dict(notes=[QUERY]))
            out = root / 'variant'
            self.assertEqual(buffer_inventory(inv, spv, validation, out), 1)
            job = json.loads((out / 'jobs.json').read_text())[0]
            self.assertEqual(job['runtime_buffer_slots'], [8])
            self.assertEqual(job['tex'], {'1': '2d', '8': 'buffer'})
            write_json(spv / (key + '.res.json'), dict(notes=[QUERY, 'unknown query']))
            with self.assertRaises(ValueError):
                buffer_inventory(inv, spv, validation, root / 'bad')

    def test_incomplete_shader_coverage_is_not_exported(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, inventory = root / 'source', root / 'inventory'
            source.mkdir()
            inventory.mkdir()
            index = struct.pack('<3I', 0x494b5644, 2, 1)
            # Truncated indexes must not be promoted by a matching receipt hash.
            (source / 'shader_index.bin').write_bytes(index)
            write_json(source / 'validation.json', dict(index_sha256=hashlib.sha256(index).hexdigest(), records=[]))
            write_json(inventory / 'inventory.json', dict(programs=[dict(key='a' * 16)]))
            with self.assertRaises(ValueError):
                merge_caches([source], inventory, root / 'merged')


class InstallReleaseTests(unittest.TestCase):
    def test_launcher_pool_windows_keeps_other_render_flags(self):
        import subprocess
        template=(Path(__file__).resolve().parents[1]/'portmaster-launch.sh.in').read_text()
        block=template.split('    # Optional CPU-accessible pool residency,',1)[1]
        block=block.split('\n',1)[1].split('    # End optional pool residency.',1)[0]
        tail='\nprintf "%s %s %s %s %s" "$d2r_perf_flags" "${DM_LINUX_POOL_WINDOWS:-off}" "${DM_RENDER_SLOTS:-off}" "${DM_LINUX_COMPACT_2D_IMAGES:-off}" "${DM_TEMP_CACHE_IDLE_FRAMES:-off}"'
        cases=[({},'4292739279 off off off off'),
               ({'D2R_POOL_WINDOWS':'0'},'4292739279 off off off off'),
               ({'D2R_POOL_WINDOWS':'1'},'4292739279 1 2 1 300'),
               ({'DM_LINUX_POOL_WINDOWS':'1'},'4292739279 1 2 1 300'),
               ({'D2R_POOL_WINDOWS':'1','DM_RENDER_SLOTS':'1','DM_LINUX_COMPACT_2D_IMAGES':'0'},'4292739279 1 1 0 300'),
               ({'D2R_POOL_WINDOWS':'1','D2R_RENDER_SLOTS':'1','DM_RENDER_SLOTS':'2'},'4292739279 1 1 1 300'),
               ({'D2R_POOL_WINDOWS':'1','DM_TEMP_CACHE_IDLE_FRAMES':'0'},'4292739279 1 2 1 0')]
        for options,expected in cases:
            result=subprocess.check_output(['/bin/bash','-c','d2r_perf_flags=4292739279\n'+block+tail],env=options,text=True)
            self.assertEqual(result,expected)
        invalid=[{'D2R_POOL_WINDOWS':'2'},
                 {'D2R_POOL_WINDOWS':'1','D2R_RENDER_SLOTS':'3'},
                 {'D2R_POOL_WINDOWS':'1','DM_RENDER_SLOTS':'0'},
                 {'D2R_POOL_WINDOWS':'1','DM_LINUX_COMPACT_2D_IMAGES':'2'},
                 {'D2R_POOL_WINDOWS':'1','DM_TEMP_CACHE_IDLE_FRAMES':'1'}]
        for options in invalid:
            result=subprocess.run(['/bin/bash','-c',block],env=options,capture_output=True)
            self.assertNotEqual(result.returncode,0)

    def test_launcher_uses_selected_pool_floor_and_keeps_default(self):
        import os
        import subprocess
        template=(Path(__file__).resolve().parents[1]/'portmaster-launch.sh.in').read_text()
        line=next(row for row in template.splitlines() if row.startswith('export DM_LINUX_SHARED_POOLS='))
        environment=os.environ.copy();environment.pop('D2R_SHARED_POOL_MIN_MB',None)
        for selected,expected in ((None,'384'),('256','256'),('384','384')):
            if selected is not None:environment['D2R_SHARED_POOL_MIN_MB']=selected
            output=subprocess.check_output(['/bin/bash','-c',line+'\nprintf "%s" "$DM_LINUX_SHARED_POOL_MIN_MB"'],
                                           env=environment,text=True)
            self.assertEqual(output,expected)

    def test_optional_wsi_keeps_notices_and_excludes_extra_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            wsi = root / 'wsi'
            wsi.mkdir()
            binary = bytearray(64)
            binary[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', binary, 18, 183)
            (root / 'd2r-linux-novideo').write_bytes(make_runtime_elf())
            for name in WSI_FILES:
                (wsi / name).write_bytes(b'synthetic notice')
            (wsi / WSI_FILES[1]).write_bytes(binary)
            write_json(wsi / WSI_FILES[0], dict(layer=dict(library_path='./libVkLayer_window_system_integration.so')))
            (wsi / 'prod.keys').write_bytes(b'PRIVATE_SENTINEL')
            archive = root / 'with-wsi.zip'
            package(root, archive, wsi)
            with zipfile.ZipFile(archive) as z:
                selected = [n for n in z.namelist() if n.startswith('runtime/wayland-wsi/')]
                self.assertEqual(len(selected), len(WSI_FILES))
                self.assertTrue(all(b'PRIVATE_SENTINEL' not in z.read(n) for n in z.namelist()))
            (wsi / 'LICENSE-Rudroid.txt').unlink()
            with self.assertRaises(ValueError):
                package(root, root / 'incomplete.zip', wsi)

    def test_launcher_shell_quoting(self):
        value = Path('/device/with spaces/$(untrusted)\'s')
        text = render('game=@D2R_GAME_ROOT@\n', {'@D2R_GAME_ROOT@': value})
        import shlex
        self.assertEqual(shlex.split(text.split('=', 1)[1]), [str(value)])
        with self.assertRaises(ValueError):
            render('@D2R_GAME_ROOT@', {'@D2R_GAME_ROOT@': Path('relative')})

    def test_private_game_payload_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            game = Path(temp)
            (game / 'resource').write_bytes(b'private synthetic resource')
            write_json(game / 'game-manifest.json', dict(game_build='90447', files=tree_files(game)))
            verify_game(game)
            (game / 'resource').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                verify_game(game)
            write_json(game / 'game-manifest.json', dict(game_build='90447', files={'../escape': {}}))
            with self.assertRaises(ValueError):
                verify_game(game)

    def test_public_zip_never_walks_game_or_key_directories(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binary = bytearray(64)
            binary[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', binary, 18, 183)
            (root / 'd2r-linux-novideo').write_bytes(make_runtime_elf())
            for name in ('prod.keys', 'main.patched.bin', 'shader_index.bin', 'save.d2s', '00-00000009.data'):
                (root / name).write_bytes(b'PRIVATE_SENTINEL')
            archive = root / 'runtime.zip'
            files = package(root, archive)
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(set(z.namelist()), set(files) | {'runtime-manifest.json'})
                self.assertTrue(all(b'PRIVATE_SENTINEL' not in z.read(name) for name in z.namelist()))
                self.assertFalse(json.loads(z.read('runtime-manifest.json'))['contains_game_files'])


if __name__ == '__main__':
    unittest.main()

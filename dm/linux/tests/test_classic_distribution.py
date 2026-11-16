# INPUT: Synthetic input folders, source-only bundle inventory and fresh/existing profiles.
# OUTPUT: Ambiguous inputs, descriptor tampering and unsafe profile edits are rejected.
# POS: Host distribution regressions; no proprietary files or running game required.
import hashlib
import contextlib
import io
import json
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from runtime_fixtures import make_runtime_elf
from classic_bundle import DEFAULTS, LEGACY_DEFAULTS, PATHS, descriptor, legacy_descriptor, load_bundle, seed_settings
from prepare_classic import discover, assemble
from prepare_classic import main as prepare_main, TOOL_NAMES
from prepare_game import tree_files
from package_preparer import preparer_files
from package_runtime import runtime_files
from install_game import verify_game, backup_entry


class ClassicDistributionTests(unittest.TestCase):
    def test_capacity_check_reports_selected_pools_without_creating_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);inputs=root/'input';tools=root/'tools';inputs.mkdir();tools.mkdir()
            for name in ('base.nsp','update.nsp','prod.keys'):
                (inputs/name).write_bytes(b'private input fixture')
            for name in TOOL_NAMES.values():
                path=tools/name;path.write_text('#!/bin/sh\nexit 0\n');path.chmod(0o755)
            out=root/'out'
            args=['prepare_classic.py','--resources','full','--input',str(inputs),'--out',str(out),
                  '--tools-bin',str(tools),'--check']
            for options,expected,experiment,terrain in (([],dict(buffer=400,texture=710,cpu=250),False,128),
                    (['--buffer-pool-mib','384','--texture-pool-mib','512','--cpu-pool-mib','128'],
                     dict(buffer=384,texture=512,cpu=128),True,128),
                    (['--terrain-tile-side','64'],dict(buffer=400,texture=710,cpu=250),False,64),
                    (['--terrain-auto'],dict(buffer=400,texture=710,cpu=250),False,'auto'),
                    (['--classic-resource-gate','--terrain-auto'],dict(buffer=400,texture=710,cpu=250),False,'auto'),
                    (['--shader-nan-minmax'],dict(buffer=400,texture=710,cpu=250),False,128),
                    (['--buffer-pool-mib','256','--texture-pool-mib','320','--terrain-tile-side','64'],
                     dict(buffer=256,texture=320,cpu=250),True,64)):
                stream=io.StringIO()
                with patch.object(sys,'argv',args+options),contextlib.redirect_stdout(stream):
                    prepare_main()
                report=json.loads(stream.getvalue())
                self.assertEqual(report['pool_capacity_mib'],expected)
                self.assertIs(report['pool_capacity_experiment'],experiment)
                self.assertEqual(report['terrain_tile_side'],terrain)
                self.assertIs(report['terrain_resolution_experiment'],terrain in (64,'auto'))
                self.assertIs(report['classic_resource_gate'],'--classic-resource-gate' in options)
                self.assertIs(report['shader_nan_minmax'],'--shader-nan-minmax' in options)
                self.assertFalse(out.exists())
            with patch.object(sys,'argv',args+['--texture-pool-mib','711']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):prepare_main()
            self.assertFalse(out.exists())
            with patch.object(sys,'argv',args+['--terrain-tile-side','32']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):prepare_main()
            with patch.object(sys,'argv',args+['--terrain-tile-side','64','--terrain-auto']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):prepare_main()
            self.assertFalse(out.exists())

    def test_repeated_upgrades_preserve_all_distinct_launchers(self):
        with tempfile.TemporaryDirectory() as temp:
            entry=Path(temp)/'D2R.sh';entry.write_bytes(b'first')
            first=backup_entry(entry);entry.write_bytes(b'second')
            second=backup_entry(entry);self.assertNotEqual(first,second)
            self.assertEqual(first.read_bytes(),b'first');self.assertEqual(second.read_bytes(),b'second')
            self.assertEqual(backup_entry(entry),second)
            self.assertEqual(entry.read_bytes(),b'second')

    def test_backup_does_not_follow_an_existing_link(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);entry=root/'D2R.sh';entry.write_bytes(b'launcher')
            other=root/'other';other.write_bytes(b'keep')
            (root/'D2R.sh.before-d2r').symlink_to(other)
            result=backup_entry(entry)
            self.assertEqual(result.name,'D2R.sh.before-d2r.1')
            self.assertEqual(other.read_bytes(),b'keep')

    def test_installer_backup_can_stay_outside_the_ports_menu(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);ports=root/'Ports';ports.mkdir()
            entry=ports/'D2R.sh';entry.write_bytes(b'original launcher')
            backup=backup_entry(entry,root/'profile/launcher-backups')
            self.assertEqual(backup.read_bytes(),entry.read_bytes())
            self.assertEqual(list(ports.iterdir()),[entry])

    def test_two_packages_and_private_keys_are_unambiguous(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for name in ['更新.NSP','本体.nsp','prod.keys']:(root/name).write_bytes(b'user-owned fixture')
            packages,keys,title=discover(root)
            self.assertEqual(len(packages),2);self.assertEqual(keys.name,'prod.keys');self.assertIsNone(title)
            (root/'third.nsp').write_bytes(b'extra')
            with self.assertRaises(ValueError):discover(root)
            (root/'third.nsp').unlink();(root/'prod.keys').unlink();(root/'prod.keys').symlink_to(root/'本体.nsp')
            with self.assertRaises(ValueError):discover(root)

    def test_fixed_descriptor_and_manifest_binding(self):
        with tempfile.TemporaryDirectory() as temp:
            game=Path(temp);(game/'classic-bundle.json').write_text(json.dumps(descriptor()))
            (game/PATHS['settings']).write_text(json.dumps(DEFAULTS))
            required={'classic-bundle.json',PATHS['settings'],PATHS['shaders'],
                PATHS['modules']+'/classic-native.json',PATHS['frontend']+'/complete.json'}
            manifest={'files':dict.fromkeys(required,{})}
            self.assertEqual(load_bundle(game,manifest)['modules'],game/'classic-native-v6')
            damaged=descriptor();damaged['paths']=dict(PATHS,modules='../outside')
            (game/'classic-bundle.json').write_text(json.dumps(damaged))
            with self.assertRaises(ValueError):load_bundle(game,manifest)
            (game/'classic-bundle.json').write_text(json.dumps(descriptor()))
            manifest['files'].pop(PATHS['shaders'])
            with self.assertRaises(ValueError):load_bundle(game,manifest)

    def test_defaults_seed_once_and_never_overwrite_user_preferences(self):
        with tempfile.TemporaryDirectory() as temp:
            profile=Path(temp);self.assertTrue(seed_settings(profile))
            settings=profile/'save/Settings/Settings.json';meta=settings.with_name(settings.name+'.meta')
            self.assertEqual(json.loads(settings.read_text()),DEFAULTS)
            self.assertGreater(json.loads(meta.read_text())['last_modified_time'],0)
            settings.write_bytes(b'{"Perspective":0,"user":123}\n');before=settings.read_bytes(),meta.read_bytes()
            self.assertFalse(seed_settings(profile));self.assertEqual((settings.read_bytes(),meta.read_bytes()),before)
            self.assertEqual(sorted(p.name for p in settings.parent.iterdir()),['Settings.json','Settings.json.meta'])

    def test_legacy_bundle_keeps_its_own_defaults_on_first_install(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);game=root/'game';game.mkdir()
            (game/'classic-bundle.json').write_text(json.dumps(legacy_descriptor()))
            (game/PATHS['settings']).write_text(json.dumps(LEGACY_DEFAULTS))
            required={'classic-bundle.json',PATHS['settings'],PATHS['shaders'],
                PATHS['modules']+'/classic-native.json',PATHS['frontend']+'/complete.json'}
            paths=load_bundle(game,{'files':dict.fromkeys(required,{})})
            old=json.loads(paths['settings'].read_text())
            self.assertTrue(seed_settings(root/'profile',old))
            self.assertEqual(json.loads((root/'profile/save/Settings/Settings.json').read_text()),LEGACY_DEFAULTS)
            (game/PATHS['settings']).write_text(json.dumps(DEFAULTS))
            with self.assertRaises(ValueError):load_bundle(game,{'files':dict.fromkeys(required,{})})

    def test_linked_settings_and_orphan_metadata_are_not_repaired_silently(self):
        with tempfile.TemporaryDirectory() as temp:
            profile=Path(temp);outside=profile/'outside';outside.mkdir();(profile/'save').mkdir()
            (profile/'save/Settings').symlink_to(outside)
            with self.assertRaises(ValueError):seed_settings(profile)
            self.assertFalse(list(outside.iterdir()))
            (profile/'save/Settings').unlink();(profile/'save/Settings').mkdir()
            (profile/'save/Settings/Settings.json.meta').write_text('keep')
            with self.assertRaises(ValueError):seed_settings(profile)
            self.assertEqual((profile/'save/Settings/Settings.json.meta').read_text(),'keep')

    def test_failed_default_publication_rolls_back_only_its_own_files(self):
        with tempfile.TemporaryDirectory() as temp:
            profile=Path(temp);actual=os.link;calls=[]
            def fail_second(source,target):
                calls.append(target)
                if len(calls)==2:raise OSError('test failure')
                actual(source,target)
            with patch('classic_bundle.os.link',side_effect=fail_second):
                with self.assertRaises(OSError):seed_settings(profile)
            self.assertEqual(list((profile/'save/Settings').iterdir()),[])

    def test_assembly_contains_every_classic_component_and_no_original_key(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);base=root/'base';native=root/'native';shaders=root/'shaders';frontend=root/'frontend'
            for path,data in [(base/'13-device/game/Update/RomFS/data/build_manifest.psv',b'owned game'),
                (base/'13-device/game/handheld-prepared-128/manifest.json',b'{}'),
                (native/'main.patched.bin',b'private module'),(shaders/'shaders.squashfs',b'hsqs private'),
                (shaders/'complete.json',b'{}'),(frontend/'overlay/data/.build.config',b'config'),
                (frontend/'overlay/data/00-00000001.data',b'menu'),(frontend/'complete.json',b'{}')]:
                path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            (root/'prod.keys').write_bytes(b'private key outside generated trees')
            (native/'classic-native.json').write_text(json.dumps({'output_files':{'main.patched.bin':{}}}))
            out=root/'ready';assemble(base,native,shaders,frontend,out)
            manifest=verify_game(out/'game');self.assertTrue(load_bundle(out/'game',manifest))
            self.assertTrue((out/'install-classic.sh').is_file());self.assertTrue((out/'classic_bundle.py').is_file())
            self.assertFalse(any(p.suffix=='.keys' for p in out.rglob('*')))
            (out/'game/classic-shaders/shaders.squashfs').write_bytes(b'changed')
            with self.assertRaises(ValueError):verify_game(out/'game')

    def test_public_preparer_allowlist_never_walks_private_outputs(self):
        files=preparer_files()
        self.assertIn('preparer/dm/linux/prepare_classic.py',files)
        self.assertIn('preparer/dm/linux/prepare_shader_nan_minmax.py',files)
        self.assertIn('preparer/dm/linux/spirv_nan_minmax.py',files)
        self.assertIn('preparer/dm/linux/terrain_resolution.py',files)
        self.assertIn('preparer/dm/linux/classic_resource_gate.py',files)
        self.assertIn('preparer/dm/linux/classic_resource_gate.S.in',files)
        self.assertIn('preparer/dm/ios/tools/touch_layout.py',files)
        self.assertIn('preparer/dm/third_party/ryujinx/src/Spv.Generator/LICENSE',files)
        for name in files:
            self.assertNotIn('out',Path(name).parts);self.assertNotIn('bin',Path(name).parts)
            self.assertFalse(name.endswith(('.keys','.nsp','.xci','.d2s','.squashfs')))

    def test_exported_rewriter_dependencies_import_without_the_checkout(self):
        # The rewriter imports these helpers at runtime. Check the exported
        # files in isolation so the developer checkout cannot hide omissions.
        files=preparer_files()
        with tempfile.TemporaryDirectory() as temp:
            tools=Path(temp)/'tools';tools.mkdir()
            for name,path in files.items():
                if Path(name).parent.as_posix()=='preparer/dm/tools':
                    (tools/Path(name).name).write_bytes(path.read_bytes())
            result=subprocess.run([sys.executable,'-E','-c',
                'import sys;from pathlib import Path;sys.path.insert(0,sys.argv[1]);'
                'import relative_jump,community_patch_plan;'
                'assert all(Path(m.__file__).resolve().parent==Path(sys.argv[1]).resolve() '
                'for m in (relative_jump,community_patch_plan))',
                str(tools)],cwd=temp,capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_public_runtime_keeps_the_three_step_guide_without_audit_documents(self):
        with tempfile.TemporaryDirectory() as temp:
            build=Path(temp)
            elf=bytearray(20);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=(183).to_bytes(2,'little')
            (build/'d2r-linux-novideo').write_bytes(make_runtime_elf())
            files=runtime_files(build)
            self.assertIn('README.md',files)
            self.assertEqual(sum(line.startswith(('1. ','2. ','3. ')) for line in files['README.md'].read_text().splitlines()),3)
            self.assertFalse(any(name.startswith(('docs/','tools/')) for name in files))
            self.assertFalse(any('PROFILING' in name or 'PERFORMANCE' in name or 'validation.json' in name for name in files))


if __name__=='__main__':unittest.main()

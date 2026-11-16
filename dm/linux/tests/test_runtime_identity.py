# INPUT: Nonexecuted complete ELF fixtures and malformed/non-production variants.
# OUTPUT: Fail-closed architecture, profile, kind, source and truncation assertions.
# POS: Regression for the public runtime packaging boundary.
from pathlib import Path
import sys,unittest,tempfile
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from runtime_identity import production_identity
from runtime_fixtures import make_runtime_elf

class RuntimeIdentityTests(unittest.TestCase):
    def test_compiled_bc_decoder_changes_the_source_identity(self):
        import build_revision
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);header=root/'dm/third_party/bcdec/bcdec.h'
            header.parent.mkdir(parents=True);header.write_text('synthetic decoder one')
            with patch.object(build_revision,'ROOT',root):
                first=build_revision.source_revision()
                header.write_text('synthetic decoder two')
                self.assertNotEqual(first,build_revision.source_revision())
    def test_release_source_and_game_are_explicit(self):
        for game in ('90447','94012'):
            self.assertEqual(production_identity(make_runtime_elf(game_build=game))['game_build'],game)
    def test_short_magic_and_stale_or_diagnostic_artifacts_are_refused(self):
        invalid=[b'\x7fELF\x02\x01'+b'\0'*12+b'\xb7\0',make_runtime_elf(profile='Debug'),make_runtime_elf(kind='probe'),make_runtime_elf(revision='0'*64),make_runtime_elf(debug=True)]
        for data in invalid:
            with self.subTest(size=len(data)),self.assertRaises(ValueError): production_identity(data)
    def test_truncated_sections_are_refused(self):
        data=make_runtime_elf()
        for cut in (20,64,120,len(data)-1):
            with self.subTest(cut=cut),self.assertRaises(ValueError): production_identity(data[:cut])
if __name__=='__main__': unittest.main()

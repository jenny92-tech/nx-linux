from pathlib import Path
import importlib.util,json,struct,sys,tempfile,unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import runtime_identity as gate
import source_revision as revision

REVISION='a'*64

def elf(profile='Release',title=gate.TITLE,source=REVISION,debug=None,allocated=True,mapped=True,kind='linux-native-vulkan'):
    identity=json.dumps({'profile':profile,'kind':kind,'title_id':title,'game_build':gate.BUILD,'source_revision':source}).encode()+b'\0'
    bodies=[('.text',b'\xc0\x03\x5f\xd6'),('.rodata',gate.MARKER),('.soulknight_build',identity)]
    if debug:bodies.append((debug,b'diagnostic'))
    names=b'\0'+b''.join(n.encode()+b'\0' for n,_ in bodies)+b'.shstrtab\0'
    data=bytearray(256);sections=[(0,0,0,0,0,0,0,0,0,0)]
    for name,body in bodies+[('.shstrtab',names)]:
        offset=len(data);data.extend(body);flags=6 if name=='.text' else 2
        if name=='.soulknight_build' and not allocated:flags=0
        address=0x400000+offset+(4096 if name=='.soulknight_build' and not mapped else 0)
        sections.append((names.index(name.encode()+b'\0'),3 if name=='.shstrtab' else 1,flags,address,offset,len(body),0,0,1,0))
    shoff=(len(data)+7)&~7;data.extend(b'\0'*(shoff-len(data)))
    for section in sections:data.extend(struct.pack('<IIQQQQIIQQ',*section))
    data[:16]=b'\x7fELF\x02\x01\x01'+b'\0'*9
    struct.pack_into('<HHIQQQIHHHHHH',data,16,2,183,1,0x400100,64,shoff,0,64,56,1,64,len(sections),len(sections)-1)
    struct.pack_into('<IIQQQQQQ',data,64,1,5,0,0x400000,0x400000,len(data),len(data),4096)
    return bytes(data)

class RuntimeIdentityTests(unittest.TestCase):
    def setUp(self):
        self.patcher=patch.object(gate,'source_revision',return_value=REVISION);self.patcher.start();self.addCleanup(self.patcher.stop)
    def test_complete_matching_stripped_runtime(self):
        self.assertEqual(gate.production_identity(elf())['source_revision'],REVISION)
    def test_diagnostic_stale_wrong_title_and_symbols_are_rejected(self):
        for data in (elf(profile='Debug'),elf(title='0100726014352000'),elf(source='b'*64),elf(debug='.debug_info'),elf(debug='.symtab')):
            with self.subTest(data=data[-16:]),self.assertRaises(ValueError):gate.production_identity(data)
    def test_identity_must_be_allocated_and_mapped(self):
        for data in (elf(allocated=False),elf(mapped=False)):
            with self.assertRaises(ValueError):gate.production_identity(data)
    def test_gles_experiment_cannot_pass_the_production_gate(self):
        with self.assertRaises(ValueError):gate.production_identity(elf(kind='linux-native-gles-experiment'))
    def test_section_only_fixture_and_non_file_entry_are_rejected(self):
        data=bytearray(elf());struct.pack_into('<H',data,56,0)
        with self.assertRaises(ValueError):gate.production_identity(data)
        data=bytearray(elf());struct.pack_into('<Q',data,24,0x500000)
        with self.assertRaises(ValueError):gate.production_identity(data)
    def test_truncated_program_or_section_table_is_rejected(self):
        for size in (20,80,len(elf())-1):
            with self.assertRaises(ValueError):gate.production_identity(elf()[:size])

class SourceRevisionTests(unittest.TestCase):
    def test_build_and_gate_sources_count_but_outputs_and_private_tables_do_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for name in ('dm/linux/soulknight_ns','dm/host','dm/gfx','dm/android','dm/third_party/bcdec'):(root/name).mkdir(parents=True)
            (root/'dm/third_party/bcdec/bcdec.h').write_text('original third party')
            with patch.object(revision,'ROOT',root):
                previous=revision.source_revision()
                for name in ('source_revision.py','runtime_identity.py','runtime_build.h','build.py','Makefile','host.c'):
                    (root/'dm/linux/soulknight_ns'/name).write_text(name)
                    current=revision.source_revision();self.assertNotEqual(previous,current);previous=current
                for name in ('dm/linux/out/generated.c','dm/linux/tests/test.c','dm/host/dm_vault_tab.h'):
                    p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('excluded')
                    self.assertEqual(previous,revision.source_revision())

if __name__=='__main__':unittest.main()

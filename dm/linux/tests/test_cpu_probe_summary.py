# INPUT: Executable maps, original-module boundaries and a short CPU recording.
# OUTPUT: Exclusive costs stay separate from overlapping call-chain membership.
# POS: Host regression for interpreting profiling evidence rather than FPS.
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from summarize_cpu_probe import summarize


class CpuSummaryTests(unittest.TestCase):
    def test_exclusive_samples_do_not_add_callchain_parents(self):
        maps = '1000-2000 r-xp 0000 00:00 0\n3000-4000 r-xp 0000 00:00 0 /lib/libc.so\n'
        modules = '[dm] main: base 0x1000 size 0x1000 build_id=' + 'a'*32
        rows = [dict(pid='7',tid='8',ip='0x1100',callchain=';0x1100;0x3100'),
                dict(pid='7',tid='9',ip='0x3100',callchain=';0x3100;0x1100')]
        result = summarize(rows,maps,modules,None)
        self.assertEqual([r['percent'] for r in result['exclusive']],[50,50])
        self.assertEqual(result['inclusive_images'],{'main':2,'/lib/libc.so':2})

    def test_unmapped_instruction_cannot_be_assigned_to_a_module_hole(self):
        result=summarize([dict(pid='7',tid='8',ip='0x2100')],
            '1000-2000 r-xp 0000 00:00 0\n',
            '[dm] main: base 0x1000 size 0x2000 build_id='+'a'*32,None)
        self.assertEqual(result['exclusive'][0]['image'],'[outside captured executable maps]')

    def test_mixed_process_samples_are_rejected(self):
        with self.assertRaises(ValueError):
            summarize([dict(pid=p,tid='8',ip='0x1100') for p in ('7','8')],
                '1000-2000 r-xp 0000 00:00 0\n',
                '[dm] main: base 0x1000 size 0x1000 build_id='+'a'*32,None)


if __name__ == '__main__':
    unittest.main()

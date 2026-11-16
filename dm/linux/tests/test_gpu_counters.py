# INPUT: Synthetic counter intervals with unequal activity and truncated CSV.
# OUTPUT: Weighted cycle-ratio and complete-inventory rejection checks.
# POS: Host-only profiling regression; no hardware or game data needed.
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from summarize_gpu_counters import summarize

HEADER = 'gpu,cores,1,bus_width,128\nsample,counter_id,name,value\n'


class GpuCounterTests(unittest.TestCase):
    def test_weighted_cycles_and_nominal_rate(self):
        text = HEADER
        for sample, active, stalled in [(0, 100, 90), (1, 900, 90)]:
            text += f'{sample},1,Programmable core active cycles,{active}\n'
            text += f'{sample},2,Instruction issue starvation cycles,{stalled}\n'
            text += f'{sample},3,Output external read bytes,100\n'
        result = summarize(text, 500)
        self.assertEqual(result['ratios']['instruction_starvation_per_core_active'], .18)
        self.assertEqual(result['rates_per_nominal_second']['Output external read bytes'], 200)
        self.assertEqual(result['nominal_sample_seconds'], 1)

    def test_missing_samples_and_partial_inventory_rejected(self):
        for text in [HEADER, HEADER+'1,1,core,1\n',
                     HEADER+'0,1,core,1\n0,2,other,1\n1,1,core,2\n']:
            with self.assertRaises(ValueError): summarize(text, 1000)

    def test_duplicate_nonfinite_and_changed_names_rejected(self):
        for rows in ['0,1,a,1\n0,1,a,2\n', '0,1,a,nan\n',
                     '0,1,a,1\n1,1,b,2\n']:
            with self.assertRaises(ValueError): summarize(HEADER+rows, 1000)


if __name__ == '__main__': unittest.main()

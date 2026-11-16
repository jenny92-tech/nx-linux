#!/usr/bin/env python3
# INPUT: Synthetic procfs/ELF/renderer evidence, independent of a live device.
# OUTPUT: Regression coverage for accounting, identity and absent metadata.
# POS: Offline tests for the memory snapshot collector and scene comparison.
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('memory_snapshot', Path(__file__).parents[1] / 'memory_snapshot.py')
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)

SMAPS = '''1000-3000 rw-p 00000000 00:00 0
Size: 8 kB
Rss: 4 kB
Pss: 4 kB
Private_Dirty: 4 kB
80003000-80004000 r--p 00002000 08:01 42 /a path/with spaces.so
Size: 4 kB
Rss: 4 kB
Pss: 2 kB
Shared_Clean: 4 kB
'''


def fixture(label='before'):
    return dict(schema=1, label=label, identity=dict(pid=7, boot_id='boot',
                process_start_ticks='123', exe_sha256='sha', exe='/game'),
                captured_unix_ns=1, elapsed_seconds=.1,
                renderer=dict(globals=dict(g_heap=4096, g_heap_cap=8192, g_as=4096, g_reserve=65536)),
                mali=None, files=dict(smaps=SMAPS, status_after='VmRSS: 16 kB\nVmHWM: 20 kB\n',
                meminfo='MemAvailable: 100 kB\nHugePages_Total: 0\n'))


class SnapshotTests(unittest.TestCase):
    def test_cpu_and_gpu_are_not_summed(self):
        raw = fixture()
        raw['mali'] = dict(context_bytes=12 * 1024)
        x = m.summarize(raw)
        self.assertEqual(x['status']['VmRSS'], 16 * 1024)
        self.assertEqual(x['smaps_totals']['Rss'], 8 * 1024)
        self.assertEqual(x['smaps_totals']['Pss'], 6 * 1024)
        self.assertEqual(x['status_minus_smaps_rss_bytes'], 8 * 1024)
        self.assertNotIn('physical_total_bytes', x)
        self.assertIn('file:/a path/with spaces.so', x['groups'])
        self.assertEqual(x['groups']['guest_heap']['Pss'], 4 * 1024)

    def test_unavailable_ledger_is_not_a_zero_allocation(self):
        a, b = m.summarize(fixture()), m.summarize(fixture('after'))
        b['identity']['process_start_ticks'] = '124'
        d = m.diff_reports(a, b)
        self.assertFalse(d['same_process'])
        self.assertIsNone(d['vulkan_class_deltas'])
        self.assertIsNone(d['mali_context_delta'])
        self.assertFalse(d['causal_savings_proven'])

    def test_diff_across_aslr_and_missing_mapping(self):
        a = m.summarize(fixture())
        raw = fixture('after')
        raw['files']['smaps'] = SMAPS.replace('1000-3000', '5000-7000').split('80003000-80004000')[0]
        raw['renderer']['globals']['g_heap'] = 0x5000
        b = m.summarize(raw)
        d = m.diff_reports(a, b)
        self.assertTrue(d['same_process'])
        self.assertEqual(d['groups']['guest_heap']['Pss'], 0)
        self.assertEqual(d['groups']['file:/a path/with spaces.so']['Pss'], -2048)

    def test_missing_bounds_do_not_fake_guest_release(self):
        a = m.summarize(fixture())
        raw = fixture('missing-symbols')
        raw['renderer'] = {}
        b = m.summarize(raw)
        d = m.diff_reports(a, b)
        self.assertFalse(b['classification_available'])
        self.assertIn('anonymous_unattributed', b['groups'])
        self.assertFalse(d['group_deltas_available'])
        self.assertEqual(d['groups'], {})
        self.assertEqual(d['smaps_totals']['Pss'], 0)

    def test_nonzero_load_virtual_address(self):
        elf = dict(elf_type=3, loads=[dict(offset=0, vaddr=0x10000, file_bytes=4096)])
        self.assertEqual(m.elf_load_bias(elf, 0x810000, 4096), 0x800000)

    def test_no_mali_context_is_unknown_not_zero(self):
        raw = fixture()
        raw['mali'] = dict(context_bytes=None, no_matching_context=True)
        a = m.summarize(raw)
        d = m.diff_reports(a, a)
        self.assertIsNone(d['mali_context_delta'])
        self.assertNotIn('| Mali context |', m.report_text(a))

    def test_ledger_rejects_inconsistent_and_wrong_abi(self):
        values = [0] * 37
        values[0], values[9], values[18], values[27], values[28] = 100, 200, 1, 100, 300
        raw = struct.pack('<II37Q', 1, 9, *values)
        x = m.ledger_decode(raw)
        self.assertEqual(x['classes']['texture']['bytes'], 100)
        self.assertFalse(x['physical_residency'])
        values[27] = 101
        with self.assertRaises(ValueError):
            m.ledger_decode(struct.pack('<II37Q', 1, 9, *values))
        with self.assertRaises(ValueError):
            m.ledger_decode(struct.pack('<II37Q', 2, 9, *values))

    def test_truncated_elf_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'elf'
            p.write_bytes(b'\x7fELF\x02\x01')
            with self.assertRaises(ValueError):
                m.elf_symbols(p)

    def test_bundle_preserves_existing_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'snapshot'
            x = m.summarize(fixture())
            m.write_bundle(p, x, fixture())
            self.assertIn('raw.json.gz', json.loads((p / 'manifest.json').read_text()))
            before = (p / 'summary.json').read_bytes()
            with self.assertRaises(FileExistsError):
                m.write_bundle(p, x)
            self.assertEqual((p / 'summary.json').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()

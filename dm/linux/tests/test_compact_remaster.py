# INPUT: Synthetic full-3D asset names, aliases and encoded CASC blocks.
# OUTPUT: Regressions against classic-only pruning, unmapped loss and corruption.
# POS: Host-only tests; actual retained resources are checked by the preparer.
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compact_remaster import omission_reason, select, verify_encoded
import casc_overlay as casc


class RemasterCompactionTests(unittest.TestCase):
    def test_full_world_models_and_ui_are_never_classic_gated(self):
        for name in ('data/hd/env/model/act5.model', 'data/hd/character/enemy/diablo.model',
                     'data/hd/env/texture/grass.texture', 'data/hd/global/ui/panel.sprite',
                     'data/local/font/latin.dc6', 'locales/data/zhcn/strings.json',
                     'locales/data/enus/strings.json'):
            self.assertIsNone(omission_reason(name), name)
        self.assertEqual(omission_reason('locales/audio/dede/line.wav'), 'unused-language')
        self.assertEqual(omission_reason('data/hd/global/video/intro.webm'), 'disabled-video')

    def test_retained_alias_keeps_shared_payload_and_unknown_entries_fail(self):
        names = {'data/hd/env/a.model': 0, 'locales/data/dede/alias.bin': 0,
                 'data/hd/global/video/intro.webm': 25}
        source = SimpleNamespace(positions=names, rows={n: [] for n in names}, co=0, cs=50)
        selected, positions = select(source)
        self.assertEqual(selected, {'data/hd/env/a.model'})
        self.assertEqual(positions, {0})
        source.cs = 75
        with self.assertRaisesRegex(ValueError, 'unmapped'):
            select(source)

    def test_blte_payload_corruption_is_rejected_even_when_header_key_matches(self):
        raw = casc.blte_encode(b'full world data' * 30, block_size=64)
        self.assertEqual(len(verify_encoded(raw, 'b:256K*=z', 450)), 32)
        bad = bytearray(raw)
        bad[-1] ^= 1
        self.assertEqual(casc.ekey_for(raw, 0, 8, 0), casc.ekey_for(bad, 0, 8, 0))
        with self.assertRaisesRegex(ValueError, 'checksum'):
            verify_encoded(bytes(bad), 'b:256K*=z', 450)
        with self.assertRaises(ValueError):
            verify_encoded(raw + b'garbage', 'b:256K*=z', 450)

    def test_raw_zlib_requires_exact_size_and_single_complete_stream(self):
        encoded = zlib.compress(b'model data')
        self.assertEqual(len(verify_encoded(encoded, 'z', 10)), 32)
        for raw, size in ((encoded[:-1], 10), (encoded + b'extra', 10), (encoded, 11)):
            with self.assertRaises((ValueError, zlib.error)):
                verify_encoded(raw, 'z', size)


if __name__ == '__main__':
    unittest.main()

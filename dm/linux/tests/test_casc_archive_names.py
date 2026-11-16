# INPUT: Synthetic static-CASC archive locations, containers and TVFS entries.
# OUTPUT: Hexadecimal runtime-name compatibility beyond archive number nine.
# POS: Host-only regression for the archive path used by TACT on the device.
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_classic_frontend import overlay_asset
import casc_overlay as casc


class ArchiveNamesTests(unittest.TestCase):
    def test_location_names_follow_ekey_bytes_in_hex(self):
        self.assertEqual(casc.archive_filename(0, 10), '00-0000000a.data')
        self.assertEqual(casc.archive_filename(10, 175), '0a-000000af.data')
        for pair in ((-1, 0), (0, 256), (256, 1), (True, 1)):
            with self.assertRaises(ValueError):
                casc.archive_filename(*pair)

    def test_location_scan_handles_hex_and_exhausts_one_byte_before_next_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for archive in range(1, 12):
                (root / casc.archive_filename(0, archive)).touch()
            (root / '00-00000010.data').touch()  # Archive 16, not archive 10.
            self.assertEqual(casc.available_location(root), (0, 12))
            for archive in range(12, 256):
                (root / casc.archive_filename(0, archive)).touch()
            self.assertEqual(casc.available_location(root), (1, 1))

    def test_blte_archive_reader_opens_the_name_requested_by_game(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            encoded = casc.blte_encode(b'original decoded content')
            (root / '00-0000000a.data').write_bytes(encoded)
            key = casc.ekey_for(encoded, 0, 10, 0)
            self.assertEqual(casc.read_data_file(root, key, len(encoded)), b'original decoded content')

    def test_overlay_reader_uses_hex_for_non_blte_entries_too(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / 'data').mkdir()
            encoded = b'raw texture content'
            key = casc.ekey_for(encoded, 0, 10, 0)
            (source / 'data/00-0000000a.data').write_bytes(encoded)
            logical = 'data/test.bin'
            fields = [''] * 11
            fields[6], fields[8] = key.hex(), 'n'
            cft = key + struct.pack('>I', len(encoded)) + b'\0' + struct.pack('>I', len(encoded))
            tvfs = bytearray(64) + cft
            struct.pack_into('>II', tvfs, 28, 64, len(cft))
            self.assertEqual(overlay_asset(source, {logical: fields}, tvfs, logical), encoded)


if __name__ == '__main__':
    unittest.main()

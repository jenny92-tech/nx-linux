# INPUT: Synthetic pack/base manifests, external sentinels and receipt links.
# OUTPUT: Linked-write negative control and atomic receipt publication checks.
# POS: Host-only install security regression; no game inputs or native build.
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import install_handheld_pack as installer


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture(root):
    base, pack = root / "base", root / "pack"
    (base / "data").mkdir(parents=True)
    pack.mkdir()
    (base / "data/build_manifest.psv").write_bytes(b"synthetic manifest")
    (base / "data/.build.config").write_bytes(b"synthetic config")
    (base / "data/00-00000000.data").write_bytes(b"base")
    (pack / "shaders.squashfs").write_bytes(b"prepared")
    manifest = dict(
        schema=1, game_build="90447",
        source_manifest_sha256=digest(b"synthetic manifest"),
        base_config_sha256=digest(b"synthetic config"),
        files=[dict(path="shaders.squashfs", size=8, sha256=digest(b"prepared"))],
        base_files=[dict(path="data/00-00000000.data", size=4)],
    )
    (pack / "manifest.json").write_text(json.dumps(manifest))
    return pack, base


def legacy_write_receipt(pack, receipt):
    # Original in-place publication, retained only as the vulnerable control.
    (pack / "installed.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


class ReceiptSecurityTests(unittest.TestCase):
    def run_install(self, pack, base):
        with contextlib.redirect_stdout(io.StringIO()):
            installer.install(pack, base)

    def assert_no_temporary_receipts(self, pack):
        self.assertEqual(list(pack.glob(".installed-*")), [])

    def test_legacy_follows_symlink_but_fixed_install_rejects_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pack, base = fixture(root)
            sentinel = root / "outside-save"
            original = b"synthetic player sentinel"
            sentinel.write_bytes(original)
            receipt = pack / "installed.json"
            receipt.symlink_to(sentinel)

            with patch.object(installer, "_write_receipt", legacy_write_receipt):
                self.run_install(pack, base)
            self.assertNotEqual(sentinel.read_bytes(), original)
            self.assertTrue(json.loads(sentinel.read_text())["verified"])

            sentinel.write_bytes(original)
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                self.run_install(pack, base)
            self.assertEqual(sentinel.read_bytes(), original)
            self.assertTrue(receipt.is_symlink())
            self.assert_no_temporary_receipts(pack)

    def test_dangling_receipt_link_cannot_create_an_external_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pack, base = fixture(root)
            outside = root / "absent-outside-file"
            (pack / "installed.json").symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                self.run_install(pack, base)
            self.assertFalse(outside.exists())
            self.assert_no_temporary_receipts(pack)

    def test_atomic_replace_preserves_an_external_hardlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pack, base = fixture(root)
            outside = root / "outside-save"
            original = b"synthetic player sentinel"
            outside.write_bytes(original)
            receipt = pack / "installed.json"
            os.link(outside, receipt)
            self.run_install(pack, base)
            self.assertEqual(outside.read_bytes(), original)
            self.assertTrue(json.loads(receipt.read_text())["verified"])
            self.assertNotEqual(receipt.stat().st_ino, outside.stat().st_ino)
            self.assert_no_temporary_receipts(pack)

    def test_failed_replace_keeps_the_old_receipt_and_removes_temporary_file(self):
        with tempfile.TemporaryDirectory() as directory:
            pack, base = fixture(Path(directory))
            receipt = pack / "installed.json"
            original = b"previous receipt"
            receipt.write_bytes(original)
            with patch.object(installer.os, "replace", side_effect=OSError("synthetic failure")):
                with self.assertRaisesRegex(OSError, "synthetic failure"):
                    self.run_install(pack, base)
            self.assertEqual(receipt.read_bytes(), original)
            self.assert_no_temporary_receipts(pack)


if __name__ == "__main__":
    unittest.main()

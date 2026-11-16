# INPUT: Synthetic complete windows and texture/shader provenance records.
# OUTPUT: Checks that scene boundaries and duplicate work cannot inflate claims.
# POS: Host-side tests; no game files, device or ARM64 execution required.
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("resource_analysis", Path(__file__).parents[1] / "analyze_resource_probe.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def window(frame, **changes):
    row = dict(frame=frame, frames=60, uploads=0, first_uploads=0, reuploads=0,
        refresh_transfers=0, upload_pass_breaks=0, source_bytes=0, staged_bytes=0,
        upload_wall_ns=0, copies=0, expanded_copies=0, copy_pass_breaks=0,
        expanded_live_images=0, expanded_live_bytes=0)
    row.update(changes)
    return "[nvn-vulkan] RESOURCE_PATH_PROFILE " + " ".join(f"{key}={value}" for key, value in row.items())


def shader(key, stage, warm, adapt=20):
    return (f"[nvn-vulkan] RESOURCE_SHADER_LOAD key={key} stage={stage} warm={warm} "
            f"read_meta_ns=10 adapt_ns={adapt} finalize_ns=5 total_ns={adapt+15}")


class ResourceAnalysisTests(unittest.TestCase):
    def test_scene_boundary_excludes_partial_windows(self):
        text = "\n".join([window(120, uploads=100), window(180, uploads=2), window(240, uploads=50)])
        result = MODULE.summarize(text, [], first_frame=100, last_frame=220)
        self.assertEqual(result["totals"]["uploads"], 2)
        self.assertEqual(result["totals"]["frames"], 60)
        self.assertFalse(result["playable_fps_valid"])

    def test_repeated_window_rejected(self):
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            MODULE.summarize(window(120) + "\n" + window(120), [])

    def test_warm_cold_pair_uses_key_and_stage(self):
        text = "\n".join([window(120), shader("abc", 0, 1), shader("abc", 0, 0, 30),
                          shader("other", 0, 1), shader("other", 1, 0, 50)])
        result = MODULE.summarize(text, [])
        repeated = result["repeated_warm_cold_shaders"]
        self.assertEqual(len(repeated), 1)
        self.assertEqual(repeated[0]["cold_adapt_ns"], 30)
        self.assertEqual(result["shader_cold_adapt_ns"], 80)

    def test_compressed_logical_size_is_not_reported_as_measured_saving(self):
        row = dict(serial=10, width=8, height=8, depth=1, levels=4, layers=2,
            nvn_format=68, vk_format=37, usage=7, cpu_owned=0, allocation_bytes=4096,
            extra_bytes=0, cpu_baseline_bytes=512, astc_expanded_copies=1, astc_source_format=121)
        result = MODULE.summarize(window(120), [{key: str(value) for key, value in row.items()}])
        self.assertEqual(result["expanded_astc4x4_logical_bytes"], (64 + 16 + 16 + 16) * 2)
        self.assertEqual(result["expanded_target_allocation_bytes"], 4096)
        self.assertIn("do not measure savings", result["expansion_limit"])

    def test_malformed_shader_timing_rejected(self):
        text = window(120) + "\n" + shader("abc", 0, 0).replace("total_ns=35", "total_ns=36")
        with self.assertRaisesRegex(ValueError, "do not sum"):
            MODULE.summarize(text, [])

    def test_duplicate_texture_is_not_double_counted(self):
        row = {key: "0" for key in ("serial", "allocation_bytes", "extra_bytes", "cpu_baseline_bytes",
                                    "nvn_format", "vk_format", "cpu_owned", "astc_expanded_copies")}
        with self.assertRaisesRegex(ValueError, "duplicate live texture"):
            MODULE.summarize(window(120), [row, row])


if __name__ == "__main__":
    unittest.main()

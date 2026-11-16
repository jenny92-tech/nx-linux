# INPUT: Synthetic logical pools, overlapping windows and CPU free-page censuses.
# OUTPUT: Checks that diagnostic headroom cannot be reported as saved memory.
# POS: Host-only analysis tests; no target process or game payload required.
import copy
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("pool_analysis", Path(__file__).parents[1] / "analyze_pool_residency.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
MIB = 1024**2


def snapshot():
    return dict(read_only=True, stopped_snapshot=True, pid=12, elapsed_seconds=.5,
        pools=[dict(accounting_verified=True, nvn_header=dict(base=4096, size=32*MIB, flags=34),
            accounted_used_bytes=8*MIB, free_bytes=24*MIB, allocation_failures=0,
            allocations=[dict(raw=4096, reserved_bytes=8*MIB)])],
        runtime_resources=dict(pools=[dict(serial=1, guest=4096, size=32*MIB,
            buffer=dict(allocation_size=0))], windows_summary=dict(bytes=16*MIB), windows=[
                dict(owner_serial=1, guest=4096, size=8*MIB, allocation_size=8*MIB),
                dict(owner_serial=1, guest=4096+4*MIB, size=8*MIB, allocation_size=8*MIB)]))


def line(frame, created=4, reclaimed=2):
    return (f"[nvn-vulkan] POOL_WINDOW_PROFILE frame={frame} logical=100 dense=30 "
            f"window_bytes=16 windows=2 peak_window_bytes=24 created={created} "
            f"reclaimed={reclaimed} promoted=0 demoted=0 upload_waits=0 async=1")


class PoolResidencyAnalysisTests(unittest.TestCase):
    def test_overlapping_windows_keep_actual_allocations_and_union_separate(self):
        report = MODULE.allocator_snapshot(snapshot())
        self.assertEqual(report["pool_buffer_backing_bytes"], 16*MIB)
        self.assertEqual(report["window_ranges"][0]["unique_range_bytes"], 12*MIB)
        self.assertEqual(report["window_ranges"][0]["overlapping_range_bytes"], 4*MIB)
        self.assertIn("not measured reclaimable RAM", report["limitation"])

    def test_unused_logical_capacity_is_not_dense_backing(self):
        report = MODULE.allocator_snapshot(snapshot())
        pool = report["native_pools"][0]
        self.assertEqual(pool["free_capacity_bytes"], 24*MIB)
        self.assertEqual(pool["dense_backing_bytes"], 0)
        self.assertEqual(pool["highest_live_end_bytes"], 8*MIB)

    def test_out_of_pool_and_inconsistent_window_ledger_rejected(self):
        data = snapshot()
        data["runtime_resources"]["windows"][0]["guest"] = 0
        with self.assertRaisesRegex(ValueError, "outside"):
            MODULE.allocator_snapshot(data)
        data = snapshot()
        data["runtime_resources"]["windows_summary"]["bytes"] += 1
        with self.assertRaisesRegex(ValueError, "ledger"):
            MODULE.allocator_snapshot(data)

    def test_invalid_native_accounting_rejected(self):
        data = snapshot()
        data["pools"][0]["free_bytes"] += 1
        with self.assertRaisesRegex(ValueError, "capacity accounting"):
            MODULE.allocator_snapshot(data)

    def test_log_restart_cannot_be_mixed_into_one_run(self):
        with self.assertRaisesRegex(ValueError, "separate log"):
            MODULE.window_log(line(120) + "\n" + line(60))
        with self.assertRaisesRegex(ValueError, "separate log"):
            MODULE.window_log(line(120) + "\n" + line(180, created=3))
        report = MODULE.window_log(line(120) + "\n" + line(180, created=5))
        self.assertEqual(report["maximum_sampled_backing_bytes"], 46)
        self.assertEqual(report["lifetime_peak_window_bytes"], 24)

    def test_cpu_double_walk_does_not_become_an_atomic_reclaim_permission(self):
        data = dict(read_only=True, stable_double_walk=True, atomic_snapshot=False,
            capacity_bytes=250*MIB, nonfree_including_headers_bytes=192*MIB,
            free_bytes=58*MIB, largest_free_bytes=57*MIB, resident_bytes=195*MIB,
            resident_free_interior_bytes=3*MIB)
        result = MODULE.cpu_snapshot(data)
        self.assertFalse(result["atomic_snapshot"])
        self.assertEqual(result["resident_free_interior_bytes"], 3*MIB)
        unstable = copy.deepcopy(data)
        unstable["stable_double_walk"] = False
        with self.assertRaisesRegex(ValueError, "stable"):
            MODULE.cpu_snapshot(unstable)


if __name__ == "__main__":
    unittest.main()

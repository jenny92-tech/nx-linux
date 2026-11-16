# INPUT: Synthetic scheduler timelines and capture failure receipts.
# OUTPUT: Attribution and completeness regressions without a device or game.
# POS: Verify that wake latency is not silently counted as sleeping or CPU work.
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('wait_trace', Path(__file__).resolve().parents[1] / 'analyze_wait_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)
collector_spec = importlib.util.spec_from_file_location('wait_capture', Path(__file__).resolve().parents[1] / 'wait_trace_capture.py')
collector = importlib.util.module_from_spec(collector_spec)
collector_spec.loader.exec_module(collector)


def event(clock, name, **fields):
    return dict(ns=clock, event=name, fields={k: str(v) for k, v in fields.items()})


class WaitTrace(unittest.TestCase):
    def capture(self, end_ticket=2, end_sequence=11, boundary_pid=1000):
        def line(at, payload, tid=7, name='tracing_mark_write'):
            return f'worker-{tid} [001] ...1 1.{at:06d}: {name}: {payload}'
        return '\n'.join([
            line(0, f'dm_capture_begin pid={boundary_pid}', tid=42),
            line(1, 'dm_wait seq=10 kind=completion_lock_begin epoch=3 ticket=2 value=0'),
            line(3, 'prev_pid=7 prev_state=S next_pid=8', name='sched_switch'),
            line(7, 'pid=7', tid=8, name='sched_wakeup'),
            line(10, 'prev_pid=8 prev_state=R next_pid=7', tid=8, name='sched_switch'),
            line(11, f'dm_wait seq={end_sequence} kind=completion_lock_acquired epoch=3 ticket={end_ticket} value=0'),
            line(12, f'dm_capture_end pid={boundary_pid}', tid=42)])

    def receipt(self):
        return dict(complete=True, lost=0, errors=[], target=dict(pid=1000))

    def test_complete_capture_pairs_and_attributes_the_actual_ticket(self):
        r = trace.analyze(self.capture(), self.receipt())
        s = r['samples']['completion_lock'][0]
        self.assertTrue(r['valid'])
        self.assertEqual((s['ticket'], s['wall_ns'], s['running_ns'], s['sleeping_ns'], s['runnable_ns']),
                         (2, 10_000, 3_000, 4_000, 3_000))

    def test_record_work_and_submit_are_both_measured(self):
        text = '\n'.join([
            'collector-42 [000] .... 1.000000: tracing_mark_write: dm_capture_begin pid=1000',
            'worker-7 [001] .... 1.000001: tracing_mark_write: dm_wait seq=1 kind=record_begin epoch=3 ticket=0 value=1',
            'worker-7 [001] .... 1.000010: tracing_mark_write: dm_wait seq=2 kind=submit_begin epoch=3 ticket=2 value=0',
            'worker-7 [001] .... 1.000012: tracing_mark_write: dm_wait seq=3 kind=submitted epoch=3 ticket=2 value=0',
            'collector-42 [000] .... 1.000013: tracing_mark_write: dm_capture_end pid=1000'])
        r = trace.analyze(text, self.receipt())
        self.assertEqual(r['samples']['render_record_work'][0]['wall_ns'], 9000)
        self.assertEqual(r['samples']['submit'][0]['wall_ns'], 2000)

    def test_missing_marker_rejected_even_with_kernel_loss_zero(self):
        with self.assertRaisesRegex(ValueError, 'sequence'):
            trace.analyze(self.capture(end_sequence=12), self.receipt())

    def test_capture_without_complete_phase_is_not_valid(self):
        text = '\n'.join([
            'collector-42 [000] .... 1.000000: tracing_mark_write: dm_capture_begin pid=1000',
            'worker-7 [001] .... 1.000001: tracing_mark_write: dm_wait seq=1 kind=process_begin epoch=0 ticket=0 value=0',
            'collector-42 [000] .... 1.000002: tracing_mark_write: dm_capture_end pid=1000'])
        result = trace.analyze(text, self.receipt())
        self.assertFalse(result['valid'])
        self.assertEqual(result['phase_summaries'], {})

    def test_mismatched_ticket_rejected(self):
        with self.assertRaisesRegex(ValueError, 'identifier'):
            trace.analyze(self.capture(end_ticket=9), self.receipt())

    def test_boundary_pid_requires_exact_match(self):
        with self.assertRaisesRegex(ValueError, 'boundaries'):
            trace.analyze(self.capture(boundary_pid=10001), self.receipt())

    def test_sleep_wakeup_and_scheduling_are_separate(self):
        rows = [event(2, 'sched_switch', prev_pid=7, next_pid=8, prev_state='S'),
                event(6, 'sched_wakeup', pid=7),
                event(9, 'sched_switch', prev_pid=8, next_pid=7, prev_state='R')]
        r = trace.attribute(rows, 7, 0, 10)
        self.assertEqual((r['running_ns'], r['sleeping_ns'], r['runnable_ns']), (3, 4, 3))
        self.assertEqual(r['missing_switches'], 0)

    def test_preempted_thread_was_already_runnable(self):
        rows = [event(2, 'sched_switch', prev_pid=7, next_pid=8, prev_state='R+'),
                event(8, 'sched_switch', prev_pid=8, next_pid=7, prev_state='S')]
        r = trace.attribute(rows, 7, 0, 10)
        self.assertEqual((r['running_ns'], r['runnable_ns'], r['sleeping_ns']), (4, 6, 0))

    def test_missing_wakeup_does_not_invent_sleep_duration(self):
        rows = [event(2, 'sched_switch', prev_pid=7, next_pid=8, prev_state='S'),
                event(8, 'sched_switch', prev_pid=8, next_pid=7, prev_state='R')]
        r = trace.attribute(rows, 7, 0, 10)
        self.assertEqual(r['unresolved_off_cpu_ns'], 6)
        self.assertEqual(r['sleeping_ns'], 0)

    def test_missing_switch_in_is_flagged(self):
        rows = [event(2, 'sched_switch', prev_pid=7, next_pid=8, prev_state='S')]
        self.assertEqual(trace.attribute(rows, 7, 0, 10)['missing_switches'], 1)

    def test_loss_receipt_is_rejected(self):
        with self.assertRaises(ValueError):
            trace.analyze('', dict(complete=True, lost=1))

    def test_missing_kernel_loss_counter_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'per_cpu/cpu0/stats'
            path.parent.mkdir(parents=True)
            path.write_text('entries: 20\noverrun: 0\ncommit overrun: 0\n')
            with self.assertRaisesRegex(ValueError, 'loss counters'):
                collector.stats(Path(directory))

    def test_kernel_loss_counters_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'per_cpu/cpu0/stats'
            path.parent.mkdir(parents=True)
            path.write_text('entries: 20\noverrun: 3\ncommit overrun: 2\ndropped events: 1\n')
            self.assertEqual(collector.stats(Path(directory))['cpu0'],
                             {'entries': 20, 'overrun': 3, 'commit overrun': 2, 'dropped events': 1})

    def test_parser_keeps_kernel_clock_and_ids(self):
        line = '     nvn-worker-7 [001] ...1 12.123456: tracing_mark_write: dm_wait seq=42 kind=published epoch=3 ticket=2 value=0'
        r = trace.parse(line)[0]
        self.assertEqual((r['tid'], r['ns'], r['marker']['ticket']), (7, 12_123_456_000, 2))

    def test_nearest_rank_tail_is_not_window_average(self):
        r = trace.distribution([1_000_000] * 99 + [100_000_000])
        self.assertEqual((r['p99_ms'], r['max_ms']), (1, 100))


if __name__ == '__main__':
    unittest.main()

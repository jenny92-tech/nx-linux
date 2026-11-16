# INPUT: Synthetic profile windows with actual acknowledgement/log field syntax.
# OUTPUT: Measurement boundaries and compile/upload exclusion checks.
# POS: Host-only guard against reporting overlapping or non-warm benchmark data.
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from benchmark_runtime import batch_counts_valid, complete_windows, warm_windows


def window(frame, pipeline=0, upload=0):
    return (f'RENDER_PROFILE frame={frame} frames=60 fps=2.500 elapsed_ms=24000.000 '
            f'pipeline_ms={pipeline:.3f} gpu_wait_ms=100.000 upload_decode_ms={upload:.3f} '
            'submits=60 draws=60000\n')


ACK = 'LINUX_PERF_CONTROL sequence=3 after_frame=122 flags=24 query_range=0\n'


class RuntimeBenchmarkTests(unittest.TestCase):
    def test_work_probe_windows_require_explicit_diagnostic_measurement(self):
        _,rows=complete_windows(ACK+window(182)+
            'RENDER_WORK_PROBE frame=182 mask=1 early_dropped_draws=0 gameplay_fps_valid=0\n'+window(242)+
            'RENDER_WORK_PROBE frame=242 mask=0 early_dropped_draws=0 gameplay_fps_valid=1\n',3,24)
        selected,rejected=warm_windows(rows,1)
        self.assertEqual([w['frame'] for w in selected],[242])
        self.assertEqual(rejected,[182])
        selected,_=warm_windows(rows,1,allow_diagnostic=True)
        self.assertEqual([w['frame'] for w in selected],[182])

    def test_only_an_actual_work_probe_can_explain_inhibited_batching(self):
        row=dict(quad_batch_available=True,quad_input_draws=0,quad_gpu_draws=0,quad_merged_draws=0)
        self.assertFalse(batch_counts_valid(row,'quad',allow_diagnostic=True))
        row['work_probe_mask']=1
        self.assertFalse(batch_counts_valid(row,'quad'))
        self.assertTrue(batch_counts_valid(row,'quad',allow_diagnostic=True))
        row['quad_input_draws']=1
        self.assertFalse(batch_counts_valid(row,'quad',allow_diagnostic=True))

    def test_quad_reference_validation_cannot_be_counted_as_fps(self):
        _,rows=complete_windows(ACK+window(182)+
            'QUAD_KEY_PROFILE frame=182 direct=1 state_bytes=12300 copies=5000 '
            'state_checks=4000 reference_verified=4500 validation_only=1\n'+window(242)+
            'QUAD_KEY_PROFILE frame=242 direct=1 state_bytes=12300 copies=500 '
            'state_checks=4000 reference_verified=0 validation_only=0\n',3,24)
        selected,rejected=warm_windows(rows,1)
        self.assertEqual([w['frame'] for w in selected],[242])
        self.assertEqual(rejected,[182])
        self.assertEqual(rows[0]['quad_key_verified'],4500)

    def test_shared_alias_counts_and_unsigned_flag_stay_with_the_frame(self):
        flags=4292739279
        ack=f'LINUX_PERF_CONTROL sequence=3 after_frame=122 flags={flags} query_range=0\n'
        _,rows=complete_windows(ack+window(182)+
            'SHARED_ALIAS_WRITEBACK_PROFILE frame=182 available=1 enabled=1 alias_ranges=280 '
            'alias_bytes=62932320 dirty_prepared_ranges=280 copied_bytes=0\n',3,flags)
        self.assertTrue(rows[0]['shared_alias_available'])
        self.assertTrue(rows[0]['shared_alias'])
        self.assertEqual(rows[0]['shared_alias_ranges'],280)
        self.assertEqual(rows[0]['shared_alias_prepared'],280)
        self.assertEqual(rows[0]['shared_alias_bytes'],62932320)
        self.assertEqual(rows[0]['shared_alias_copied_bytes'],0)

    def test_work_drop_flags_need_explicit_diagnostic_opt_in(self):
        tool=Path(__file__).resolve().parents[1]/'benchmark_runtime.py'
        r=subprocess.run([sys.executable,str(tool),'--serial','unused','--pid','1',
            '--run','/test','--control','/run/test','--flags','8392',
            '--output','/unused-work-receipt.json'],capture_output=True,text=True)
        self.assertEqual(r.returncode,2)
        self.assertIn('diagnostic work bits require',r.stderr)

    def test_replay_cpu_cost_stays_with_its_frame(self):
        _,rows=complete_windows(ACK+window(182)+
            'RENDER_PHASE_CPU_PROFILE frame=182 frames=60 clock=thread_cpu replay_inclusive_ms=5500.25\n',3,24)
        self.assertEqual(rows[0]['replay_thread_cpu_ms'],5500.25)

    def test_classic_request_is_distinct_from_actual_admitted_batches(self):
        _,rows=complete_windows(ACK+window(182)+
            'CLASSIC_CULL_PROFILE frame=182 enabled=1 batches=0 dropped_draws=0 dropped_clears=0 plan_ms=1.25 fallbacks=60\n',3,24)
        self.assertTrue(rows[0]['classic_enabled'])
        self.assertEqual(rows[0]['classic_batches'],0)
        self.assertEqual(rows[0]['classic_dropped_draws'],0)
        self.assertEqual(rows[0]['classic_plan_ms'],1.25)

    def test_classic_compute_and_copy_counts_use_the_same_window(self):
        _,rows=complete_windows(ACK+window(182)+
            'CLASSIC_CULL_PROFILE frame=182 enabled=1 batches=60 dropped_draws=900 '
            'dropped_clears=60 plan_ms=3.75 dropped_copies=120 dropped_dispatches=180 fallbacks=0\n',3,24)
        self.assertEqual(rows[0]['classic_dropped_copies'],120)
        self.assertEqual(rows[0]['classic_dropped_dispatches'],180)

    def test_actual_classic_memo_is_read_from_profile(self):
        _,rows=complete_windows(ACK+window(182)+
            'CLASSIC_CULL_PROFILE frame=182 enabled=1 batches=60 dropped_draws=900 '
            'dropped_clears=60 plan_ms=3.75 memo=0 fallbacks=0\n',3,24)
        self.assertIs(rows[0]['classic_memo'],False)

    def test_native_mask_is_separate_from_actual_admission(self):
        _,rows=complete_windows(ACK+window(182)+
            'LINUX_NATIVE_HD_PROFILE frame=182 requested_mask=7 active_mask=0 admitted_batches=2 declined_batches=58\n',3,24)
        self.assertEqual(rows[0]['native_requested_mask'],7)
        self.assertEqual(rows[0]['native_active_mask'],0)
        self.assertEqual(rows[0]['native_admitted_batches'],2)
        self.assertEqual(rows[0]['native_declined_batches'],58)

    def test_actual_hot_env_cache_stays_with_its_window(self):
        _,rows=complete_windows(ACK+window(182)+
            'HOT_ENV_PROFILE frame=182 enabled=1\n',3,24)
        self.assertIs(rows[0]['hot_env_cache'],True)

    def test_specialization_records_use_actual_draw_admission(self):
        _,rows=complete_windows(ACK+window(182)+
            'CLASSIC_FLAGS_PROFILE frame=182 enabled=1 specialized_draws=60 fallback_draws=0\n',3,24)
        self.assertIs(rows[0]['classic_flag_specialization'],True)
        self.assertEqual(rows[0]['classic_specialized_draws'],60)
        self.assertEqual(rows[0]['classic_flag_fallbacks'],0)

    def test_effect_freeze_is_taken_from_actual_classic_profile(self):
        _,rows=complete_windows(ACK+window(182)+
            'CLASSIC_CULL_PROFILE frame=182 enabled=1 batches=60 dropped_draws=900 '
            'dropped_clears=60 plan_ms=3.75 memo=1 freeze_hd_effects=1 fallbacks=0\n',3,24)
        self.assertIs(rows[0]['classic_effects'],True)

    def test_code_hazard_cache_requires_its_own_actual_counts(self):
        _,rows=complete_windows(ACK+window(182)+
            'CLASSIC_CODE_PROFILE frame=182 enabled=1 full_checks=120 reused_checks=4000\n'+
            'CLASSIC_CODE_PROFILE frame=242 enabled=0 full_checks=1 reused_checks=0\n',3,24)
        self.assertIs(rows[0]['classic_code_cache'],True)
        self.assertEqual(rows[0]['classic_code_checks'],120)
        self.assertEqual(rows[0]['classic_code_hits'],4000)

    def test_support_constants_are_measured_on_actual_draws(self):
        _,rows=complete_windows(ACK+window(182)+
            'SUPPORT_SCALE_PROFILE frame=182 enabled=1 specialized_draws=24000 fallback_draws=0\n',3,24)
        self.assertIs(rows[0]['support_scale'],True)
        self.assertEqual(rows[0]['support_scale_draws'],24000)
        self.assertEqual(rows[0]['support_scale_fallbacks'],0)

    def test_sprite_filter_uses_actual_draw_and_fallback_counts(self):
        _,rows=complete_windows(ACK+window(182)+
            'SPRITE_NEAREST_PROFILE frame=182 enabled=1 nearest_draws=17880 fallback_draws=2\n',3,24)
        self.assertIs(rows[0]['sprite_nearest'],True)
        self.assertEqual(rows[0]['sprite_nearest_draws'],17880)
        self.assertEqual(rows[0]['sprite_nearest_fallbacks'],2)

    def test_shared_async_requires_actual_submission_counts(self):
        _,rows=complete_windows(ACK+window(182)+
            'SHARED_ASYNC_PROFILE frame=182 available=1 enabled=1 submits=60 presents=60 completed_submits=60 completed_presents=59\n',3,24)
        self.assertIs(rows[0]['shared_async_available'],True)
        self.assertIs(rows[0]['shared_async'],True)
        self.assertEqual(rows[0]['shared_async_submits'],60)
        self.assertEqual(rows[0]['shared_async_presents'],60)
        self.assertEqual(rows[0]['shared_async_completed_submits'],60)
        self.assertEqual(rows[0]['shared_async_completed_presents'],59)

    def test_only_full_windows_after_exact_ack(self):
        log = window(122) + ACK + window(150) + window(182) + window(242)
        boundary, rows = complete_windows(log, 3, 24)
        self.assertEqual(boundary, 122)
        self.assertEqual([r['frame'] for r in rows], [182, 242])
        self.assertEqual(complete_windows(log, 3, 8), (None, []))

    def test_second_controller_invalidates_phase(self):
        with self.assertRaises(ValueError):
            complete_windows(ACK + window(182) +
                'LINUX_PERF_CONTROL sequence=4 after_frame=182 flags=8\n', 3, 24)

    def test_upload_or_compile_requires_new_consecutive_warm_run(self):
        log = ACK + window(182) + window(242, upload=1.3) + window(302) + window(362, pipeline=2) + window(422) + window(482)
        _, rows = complete_windows(log, 3, 24)
        selected, rejected = warm_windows(rows, 2)
        self.assertEqual([r['frame'] for r in selected], [422, 482])
        self.assertEqual(rejected, [242, 362])

    def test_partial_warm_run_does_not_pass(self):
        _, rows = complete_windows(ACK + window(182, upload=1) + window(242), 3, 24)
        self.assertEqual(warm_windows(rows, 2), ([], [182]))

    def test_readback_window_is_rejected_at_exact_boundary(self):
        log = (ACK + 'PRESENT_READBACK frame=182 extent=960,720\n' +
               window(182) + window(242) + window(302))
        _, rows = complete_windows(log, 3, 24)
        selected, rejected = warm_windows(rows, 2)
        self.assertEqual(rejected, [182])
        self.assertEqual([r['frame'] for r in selected], [242, 302])

    def test_explicit_animated_uploads_include_their_real_cost(self):
        _, rows = complete_windows(ACK+window(182, upload=20)+window(242, upload=30), 3, 24)
        self.assertEqual(warm_windows(rows, 2), ([], [182, 242]))
        selected, rejected = warm_windows(rows, 2, include_uploads=True)
        self.assertEqual([r['upload_decode_ms'] for r in selected], [20, 30])
        self.assertEqual(rejected, [])

    def test_draw_census_window_is_not_a_gameplay_benchmark(self):
        log = ACK + 'DRAW_CENSUS_FRAME frame=182 gameplay_fps_valid=0\n' + window(182) + window(242) + window(302)
        _, rows = complete_windows(log, 3, 24)
        selected, rejected = warm_windows(rows, 2, include_uploads=True)
        self.assertEqual(rejected, [182])
        self.assertEqual([r['frame'] for r in selected], [242, 302])

    def test_loading_compositor_does_not_establish_live_world_fps(self):
        log=ACK
        for frame,ready in ((182,0),(242,1),(302,1)):
            log+=window(frame)+f'LINUX_NATIVE_HD_PROFILE frame={frame} requested_mask=119 active_mask={119 if ready else 7} admitted_batches=60 declined_batches=0 world_ready={ready}\n'
        _,rows=complete_windows(log,3,24)
        selected,rejected=warm_windows(rows,2,include_uploads=True)
        self.assertEqual(rejected,[182])
        self.assertEqual([r['frame'] for r in selected],[242,302])

    def test_vector_comparison_requires_actual_full_validation_profile(self):
        _,rows=complete_windows(ACK+window(182)+
            'BYTE_COMPARE_PROFILE frame=182 vector=1 full_byte_validation=1\n',3,24)
        self.assertIs(rows[0]['vector_equal'],True)
        self.assertIs(rows[0]['full_byte_validation'],True)

    def test_retired_index_records_actual_lookups_and_bucket_steps(self):
        _,rows=complete_windows(ACK+window(182)+
            'TEMP_INDEX_PROFILE frame=182 enabled=1 lookups=120 bucket_steps=134\n',3,24)
        self.assertIs(rows[0]['temp_index'],True)
        self.assertEqual(rows[0]['temp_index_lookups'],120)
        self.assertEqual(rows[0]['temp_index_steps'],134)

    def test_shader_code_lease_keeps_full_checks_and_guarded_reuse_separate(self):
        _,rows=complete_windows(ACK+window(182)+
            'SHADER_CODE_LEASE_PROFILE frame=182 available=1 enabled=1 full_checks=120 reused_checks=900 untracked=4 gpu_overlap=3 control_full_each_draw=1\n',3,24)
        self.assertIs(rows[0]['code_lease'],True)
        self.assertEqual(rows[0]['code_lease_full'],120)
        self.assertEqual(rows[0]['code_lease_reused'],900)
        self.assertIs(rows[0]['control_full_each_draw'],True)

    def test_control_lease_and_sprite_counts_are_separate_actual_profiles(self):
        _,rows=complete_windows(ACK+window(182)+
            'SHADER_CONTROL_LEASE_PROFILE frame=182 enabled=1 full_checks=120 reused_checks=900 untracked=4 gpu_overlap=3\n'+
            'SPRITE_BATCH_PROFILE frame=182 available=1 enabled=1 input_draws=300 gpu_draws=200 merged_draws=100\n',3,24)
        self.assertIs(rows[0]['control_lease'],True)
        self.assertEqual(rows[0]['control_lease_reused'],900)
        self.assertEqual(rows[0]['sprite_merged_draws'],100)

    def test_record_cache_uses_actual_omitted_commands_and_generation(self):
        _,rows=complete_windows(ACK+window(182)+
            'RECORD_STATE_PROFILE frame=182 available=1 enabled=1 checks=900 omitted_records=700 omitted_bytes=16000 generation=3\n',3,24)
        self.assertIs(rows[0]['record_state'],True)
        self.assertEqual(rows[0]['record_state_omitted'],700)
        self.assertEqual(rows[0]['record_state_generation'],3)


if __name__ == '__main__':
    unittest.main()

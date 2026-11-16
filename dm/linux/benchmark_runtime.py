#!/usr/bin/env python3
# INPUT: Running diagnostic D2R PID/run, explicit performance-control path/flags.
# OUTPUT: Complete post-acknowledgement 60-frame windows and a JSON receipt.
# POS: Host-side runtime A/B; never starts games, injects input or changes clocks.
"""Measure cache flags in a stationary, already loaded scene.

Opt the session into DM_LINUX_PERF_CONTROL and DM_PROFILE_DETAIL first.
Do not capture frames or move the player during measurement. Flags change
only at a completed profile boundary. Finish with a baseline phase to restore
it; interrupted runs explicitly report which flags may remain selected.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time

from benchmark_scale import WINDOW


def complete_windows(log, sequence, flags):
    """Only accept windows after this precise acknowledged configuration."""
    ack = re.search(rf"LINUX_PERF_CONTROL sequence={sequence} after_frame=(\d+) flags={flags}(?:\s|$)", log)
    if not ack:
        return None, []
    boundary = int(ack[1])
    section = log[ack.end():]
    # A second controller must not silently invalidate this measurement.
    if 'LINUX_PERF_CONTROL sequence=' in section:
        raise ValueError('another performance request changed the session')
    windows = []
    captures = [int(frame) for frame in re.findall(r'(?:PRESENT_READBACK|DRAW_CENSUS_FRAME) frame=(\d+)', section)]
    for match in WINDOW.finditer(section):
        frame, fps, elapsed, pipeline, wait, upload, submits, draws = match.groups()
        frame = int(frame)
        if frame - 60 < boundary:
            continue
        windows.append(dict(frame=frame, fps=float(fps), elapsed_ms=float(elapsed),
            pipeline_ms=float(pipeline), gpu_wait_ms=float(wait),
            upload_decode_ms=float(upload), submits=int(submits), draws=int(draws),
            capture_frames=[f for f in captures if frame-60 < f <= frame]))
        cpu=re.search(rf'RENDER_PHASE_CPU_PROFILE frame={frame} frames=60 .*?replay_inclusive_ms=([\d.]+)',section)
        if cpu: windows[-1]['replay_thread_cpu_ms']=float(cpu[1])
        quad_key=re.search(rf'QUAD_KEY_PROFILE frame={frame} direct=(\d+) state_bytes=(\d+) copies=(\d+) state_checks=(\d+) reference_verified=(\d+) validation_only=(\d+)',section)
        if quad_key:
            direct,size,copies,checks,verified,validation=map(int,quad_key.groups())
            windows[-1].update(quad_key_direct=bool(direct),quad_key_state_bytes=size,
                quad_key_copies=copies,quad_key_checks=checks,quad_key_verified=verified,
                quad_key_validation=bool(validation))
        work=re.search(rf'RENDER_WORK_PROBE frame={frame} mask=(\d+) early_dropped_draws=(\d+) gameplay_fps_valid=(\d+)',section)
        if work:
            mask,dropped,valid=map(int,work.groups())
            windows[-1].update(work_probe_mask=mask,work_probe_dropped_draws=dropped,
                               gameplay_fps_valid=bool(valid))
        classic=re.search(rf'CLASSIC_CULL_PROFILE frame={frame} enabled=(\d+) batches=(\d+) dropped_draws=(\d+) dropped_clears=(\d+) plan_ms=([\d.]+)',section)
        if classic:
            enabled,batches,draws,clears,cost=classic.groups()
            windows[-1].update(classic_enabled=bool(int(enabled)),classic_batches=int(batches),
                classic_dropped_draws=int(draws),classic_dropped_clears=int(clears),classic_plan_ms=float(cost))
            details=section[classic.end():].split('\n',1)[0]
            for name in ('copies','dispatches'):
                value=re.search(rf'\bdropped_{name}=(\d+)',details)
                if value: windows[-1]['classic_dropped_'+name]=int(value[1])
            memo=re.search(r'\bmemo=(\d+)',details)
            if memo: windows[-1]['classic_memo']=bool(int(memo[1]))
            effects=re.search(r'\bfreeze_hd_effects=(\d+)',details)
            if effects:windows[-1]['classic_effects']=bool(int(effects[1]))
        hot=re.search(rf'HOT_ENV_PROFILE frame={frame} enabled=(\d+)',section)
        if hot:windows[-1]['hot_env_cache']=bool(int(hot[1]))
        specialized=re.search(rf'CLASSIC_FLAGS_PROFILE frame={frame} enabled=(\d+) specialized_draws=(\d+) fallback_draws=(\d+)',section)
        if specialized:
            enabled,draws,fallbacks=map(int,specialized.groups())
            windows[-1].update(classic_flag_specialization=bool(enabled),classic_specialized_draws=draws,classic_flag_fallbacks=fallbacks)
        code=re.search(rf'CLASSIC_CODE_PROFILE frame={frame} enabled=(\d+) full_checks=(\d+) reused_checks=(\d+)',section)
        if code:
            enabled,checks,hits=map(int,code.groups())
            windows[-1].update(classic_code_cache=bool(enabled),classic_code_checks=checks,classic_code_hits=hits)
        reads=re.search(rf'CLASSIC_READ_PROFILE frame={frame} enabled=(\d+) checks=(\d+) reused=(\d+)',section)
        if reads:
            enabled,checks,hits=map(int,reads.groups())
            windows[-1].update(classic_read_cache=bool(enabled),classic_read_checks=checks,classic_read_hits=hits)
        support=re.search(rf'SUPPORT_SCALE_PROFILE frame={frame} enabled=(\d+) specialized_draws=(\d+) fallback_draws=(\d+)',section)
        if support:
            enabled,draws,fallbacks=map(int,support.groups())
            windows[-1].update(support_scale=bool(enabled),support_scale_draws=draws,support_scale_fallbacks=fallbacks)
        nearest=re.search(rf'SPRITE_NEAREST_PROFILE frame={frame} enabled=(\d+) nearest_draws=(\d+) fallback_draws=(\d+)',section)
        if nearest:
            enabled,draws,fallbacks=map(int,nearest.groups())
            windows[-1].update(sprite_nearest=bool(enabled),sprite_nearest_draws=draws,sprite_nearest_fallbacks=fallbacks)
        shared=re.search(rf'SHARED_ASYNC_PROFILE frame={frame} available=(\d+) enabled=(\d+) submits=(\d+) presents=(\d+) completed_submits=(\d+) completed_presents=(\d+)',section)
        if shared:
            available,enabled,submits,presents,completed_submits,completed_presents=map(int,shared.groups())
            windows[-1].update(shared_async_available=bool(available),shared_async=bool(enabled),shared_async_submits=submits,shared_async_presents=presents,
                shared_async_completed_submits=completed_submits,shared_async_completed_presents=completed_presents)
        alias=re.search(rf'SHARED_ALIAS_WRITEBACK_PROFILE frame={frame} available=(\d+) enabled=(\d+) alias_ranges=(\d+) alias_bytes=(\d+) dirty_prepared_ranges=(\d+) copied_bytes=(\d+)',section)
        if alias:
            available,enabled,ranges,size,prepared,copied=map(int,alias.groups())
            windows[-1].update(shared_alias_available=bool(available),shared_alias=bool(enabled),shared_alias_ranges=ranges,
                shared_alias_bytes=size,shared_alias_prepared=prepared,shared_alias_copied_bytes=copied)
        quads=re.search(rf'QUAD_BATCH_PROFILE frame={frame} available=(\d+) enabled=(\d+) input_draws=(\d+) gpu_draws=(\d+) merged_draws=(\d+)',section)
        if quads:
            available,enabled,inputs,draws,merged=map(int,quads.groups())
            windows[-1].update(quad_batch_available=bool(available),quad_batch=bool(enabled),quad_input_draws=inputs,quad_gpu_draws=draws,quad_merged_draws=merged)
        sprites=re.search(rf'SPRITE_BATCH_PROFILE frame={frame} available=(\d+) enabled=(\d+) input_draws=(\d+) gpu_draws=(\d+) merged_draws=(\d+)',section)
        if sprites:
            available,enabled,inputs,draws,merged=map(int,sprites.groups())
            windows[-1].update(sprite_batch_available=bool(available),sprite_batch=bool(enabled),sprite_input_draws=inputs,sprite_gpu_draws=draws,sprite_merged_draws=merged)
        compare=re.search(rf'BYTE_COMPARE_PROFILE frame={frame} vector=(\d+) full_byte_validation=(\d+)',section)
        if compare:
            enabled,complete=map(int,compare.groups())
            windows[-1].update(vector_equal=bool(enabled),full_byte_validation=bool(complete))
        temps=re.search(rf'TEMP_INDEX_PROFILE frame={frame} enabled=(\d+) lookups=(\d+) bucket_steps=(\d+)',section)
        if temps:
            enabled,lookups,steps=map(int,temps.groups())
            windows[-1].update(temp_index=bool(enabled),temp_index_lookups=lookups,temp_index_steps=steps)
        lease=re.search(rf'SHADER_CODE_LEASE_PROFILE frame={frame} available=(\d+) enabled=(\d+) full_checks=(\d+) reused_checks=(\d+) untracked=(\d+) gpu_overlap=(\d+) control_full_each_draw=(\d+)',section)
        if lease:
            available,enabled,checks,hits,untracked,gpu,control=map(int,lease.groups())
            windows[-1].update(code_lease_available=bool(available),code_lease=bool(enabled),code_lease_full=checks,
                code_lease_reused=hits,code_lease_untracked=untracked,code_lease_gpu_overlap=gpu,control_full_each_draw=bool(control))
        control_lease=re.search(rf'SHADER_CONTROL_LEASE_PROFILE frame={frame} enabled=(\d+) full_checks=(\d+) reused_checks=(\d+) untracked=(\d+) gpu_overlap=(\d+)',section)
        if control_lease:
            enabled,checks,hits,untracked,gpu=map(int,control_lease.groups())
            windows[-1].update(control_lease=bool(enabled),control_lease_full=checks,control_lease_reused=hits,
                control_lease_untracked=untracked,control_lease_gpu_overlap=gpu)
        recording=re.search(rf'RECORD_STATE_PROFILE frame={frame} available=(\d+) enabled=(\d+) checks=(\d+) omitted_records=(\d+) omitted_bytes=(\d+) generation=(\d+)',section)
        if recording:
            available,enabled,checks,omitted,size,generation=map(int,recording.groups())
            windows[-1].update(record_state_available=bool(available),record_state=bool(enabled),record_state_checks=checks,
                record_state_omitted=omitted,record_state_bytes=size,record_state_generation=generation)
        native=re.search(rf'LINUX_NATIVE_HD_PROFILE frame={frame} requested_mask=(\d+) active_mask=(\d+) admitted_batches=(\d+) declined_batches=(\d+)',section)
        if native:
            requested,active,admitted,declined=map(int,native.groups())
            windows[-1].update(native_requested_mask=requested,native_active_mask=active,
                native_admitted_batches=admitted,native_declined_batches=declined)
            world=re.search(rf'LINUX_NATIVE_HD_PROFILE frame={frame} [^\n]*\bworld_ready=(\d+)',section)
            if world:windows[-1]['native_world_ready']=bool(int(world[1]))
    return boundary, windows


def warm_windows(windows, count, include_uploads=False, *, allow_diagnostic=False):
    selected, rejected = [], []
    for window in windows:
        if (window['pipeline_ms'] or (window['upload_decode_ms'] and not include_uploads) or
                window.get('capture_frames') or window.get('quad_key_validation') or
                (not allow_diagnostic and (window.get('work_probe_mask',0) or
                                          not window.get('gameplay_fps_valid',True))) or
                ('native_world_ready' in window and not window['native_world_ready'] and
                 window.get('native_requested_mask',0)&~7) or
                window['elapsed_ms'] <= 0 or window['fps'] <= 0):
            rejected.append(window['frame'])
            selected = []
        else:
            selected.append(window)
            if len(selected) == count:
                return selected, rejected
    return [], rejected


def batch_counts_valid(window, kind, *, allow_diagnostic=False):
    """Work probes deliberately inhibit quad/sprite batching on every draw."""
    counts=[window[kind+suffix] for suffix in ('_input_draws','_gpu_draws','_merged_draws')]
    if not window[kind+'_batch_available']:
        return False
    if allow_diagnostic and window.get('work_probe_mask',0):
        return counts == [0,0,0]
    inputs,gpu,merged=counts
    return inputs>0 and gpu>0 and merged>0 and inputs-gpu==merged


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--run', required=True, help='absolute device run directory')
    parser.add_argument('--control', required=True, help='session DM_LINUX_PERF_CONTROL /run path')
    parser.add_argument('--flags', type=int, nargs='+', default=[8, 24, 8, 24, 8])
    parser.add_argument('--sequence', type=int, default=1, help='first unused sequence')
    parser.add_argument('--windows', type=int, default=2)
    parser.add_argument('--timeout', type=int, default=300, help='seconds per phase')
    parser.add_argument('--settle-frames',type=int,default=0,help='discard complete windows ending within this many frames after acknowledgement; use >=120 for producer-side changes')
    parser.add_argument('--include-steady-uploads', action='store_true',
                        help='include real animated texture work in FPS; still reject compile/readback windows')
    parser.add_argument('--diagnostic-work', action='store_true',
                        help='NON-PLAYABLE work attribution; requires session DM_LINUX_WORK_PROBES=1')
    parser.add_argument('--classic-control', help='opt-in PID-scoped /run classic-quality control')
    parser.add_argument('--classic-modes', type=int, nargs='+', help='0/1 per flags phase; requires classic-control')
    parser.add_argument('--classic-memos', type=int, nargs='+', help='0/1 metadata reuse per classic phase; requires DM_CLASSIC_PLAN_MEMO=1')
    parser.add_argument('--classic-effects', type=int, nargs='+', help='0/1 HD fluid freeze per phase; requires DM_CLASSIC_ALLOW_HD_EFFECT_FREEZE=1')
    parser.add_argument('--native-control', help='opt-in PID-scoped /run native HD quality control')
    parser.add_argument('--native-masks', type=int, nargs='+', help='0..255 per phase; requires matching prepared native-HD modules')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if any(flag&1073741824 for flag in args.flags) and args.settle_frames<120:
        parser.error('producer-side state caching requires --settle-frames at least 120')
    if (args.pid <= 0 or args.sequence < 1 or args.windows < 1 or args.timeout < 10 or args.settle_frames<0 or
            not args.run.startswith('/') or not args.control.startswith('/run/') or
            any(flag<0 or flag & ~((131071 if args.diagnostic_work else 1023)|131072|262144|524288|1048576|2097152|4194304|8388608|16777216|33554432|67108864|134217728|268435456|536870912|1073741824|2147483648) or flag & 32 or (flag&2097152 and not flag&1048576) or (flag&134217728 and not flag&8) or (flag&268435456 and not flag&134217728) or (flag&536870912 and not flag&16777216) or (flag&2147483648 and not flag&4194304) for flag in args.flags)):
        parser.error('invalid PID/paths/flags; diagnostic work bits require --diagnostic-work; bit 5 is forbidden')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if bool(args.classic_control)!=bool(args.classic_modes) or (args.classic_modes and
            (not args.classic_control.startswith('/run/') or len(args.classic_modes)!=len(args.flags) or
             any(m not in (0,1) for m in args.classic_modes))):
        parser.error('classic control requires /run path and one 0/1 mode per flags phase')
    if args.classic_memos and (not args.classic_modes or len(args.classic_memos)!=len(args.flags) or
                              any(m not in (0,1) for m in args.classic_memos)):
        parser.error('classic-memos requires one 0/1 value per classic phase')
    if args.classic_effects and (not args.classic_memos or len(args.classic_effects)!=len(args.flags) or
                               any(m not in (0,1) for m in args.classic_effects)):
        parser.error('classic-effects requires classic modes/memos and one 0/1 per phase')
    if bool(args.native_control)!=bool(args.native_masks) or (args.native_masks and
            (not args.native_control.startswith('/run/') or len(args.native_masks)!=len(args.flags) or
             any(not 0<=m<=255 for m in args.native_masks))):
        parser.error('native control requires /run path and one 0..255 mask per flags phase')
    if args.output.exists():
        parser.error('use a new receipt path')

    def shell(command):
        return subprocess.check_output([args.adb, '-s', args.serial, 'shell', command], text=True, timeout=20)

    def check_process():
        comm = shell(f'cat /proc/{args.pid}/comm').strip()
        run_pid = shell('cat ' + shlex.quote(args.run + '/pid')).strip()
        if comm not in ('d2r-linux', 'd2r-linux-novid') or run_pid != str(args.pid):
            raise ValueError('PID/run does not identify the running D2R session')

    phases = []
    receipt = dict(pid=args.pid, run=args.run, control=args.control,
                   diagnostic_only=args.diagnostic_work, gameplay_fps_valid=not args.diagnostic_work,
                   include_steady_uploads=args.include_steady_uploads,
                   phases=phases, complete=False)
    pending = None
    try:
        for sequence, flags in enumerate(args.flags, args.sequence):
            check_process()
            offset = int(shell('wc -c < ' + shlex.quote(args.run + '/stdout.log'))) + 1
            classic_mode=None;classic_memo=None;classic_effects=None;native_mask=None
            if args.native_masks:
                native_mask=args.native_masks[sequence-args.sequence]
                pending=dict(sequence=sequence,native_mask=native_mask,stage='native_control')
                temporary=shlex.quote(args.native_control+'.next')
                payload=shlex.quote(f'{args.pid} {sequence} {native_mask}\n')
                shell(f'umask 077; printf %s {payload} > {temporary} && mv {temporary} {shlex.quote(args.native_control)}')
                deadline=time.monotonic()+args.timeout
                while time.monotonic()<deadline:
                    check_process()
                    log=shell(f'tail -c +{offset} '+shlex.quote(args.run+'/stdout.log'))
                    if re.search(rf'LINUX_NATIVE_HD_CONTROL sequence={sequence} after_frame=\d+ mask={native_mask}(?:\s|$)',log): break
                    time.sleep(1)
                else: raise TimeoutError('no native HD quality acknowledgement')
                offset=int(shell('wc -c < '+shlex.quote(args.run+'/stdout.log')))+1
            if args.classic_modes:
                classic_mode=args.classic_modes[sequence-args.sequence]
                if args.classic_memos:classic_memo=args.classic_memos[sequence-args.sequence]
                if args.classic_effects:classic_effects=args.classic_effects[sequence-args.sequence]
                pending=dict(sequence=sequence,classic_enabled=classic_mode,classic_effects=classic_effects,native_mask=native_mask,stage='classic_control')
                temporary=shlex.quote(args.classic_control+'.next')
                request=f'{args.pid} {sequence} {classic_mode}'
                if classic_memo is not None:request+=f' {classic_memo}'
                if classic_effects is not None:request+=f' {classic_effects}'
                payload=shlex.quote(request+'\n')
                shell(f'umask 077; printf %s {payload} > {temporary} && mv {temporary} {shlex.quote(args.classic_control)}')
                deadline=time.monotonic()+args.timeout
                while time.monotonic()<deadline:
                    check_process()
                    log=shell(f'tail -c +{offset} '+shlex.quote(args.run+'/stdout.log'))
                    expected=rf'LINUX_CLASSIC_CONTROL sequence={sequence} after_frame=\d+ enabled={classic_mode}'
                    if classic_memo is not None:expected+=rf' memo={classic_memo}'
                    if classic_effects is not None:expected+=rf' freeze_hd_effects={classic_effects}'
                    if re.search(expected+r'(?:\s|$)',log): break
                    time.sleep(1)
                else: raise TimeoutError('no classic-quality acknowledgement')
                offset=int(shell('wc -c < '+shlex.quote(args.run+'/stdout.log')))+1
            temporary = shlex.quote(args.control + '.next')
            payload = shlex.quote(f'{sequence} {flags}\n')
            pending = dict(sequence=sequence, flags=flags, classic_enabled=classic_mode,classic_effects=classic_effects,native_mask=native_mask)
            shell(f'umask 077; printf %s {payload} > {temporary} && mv {temporary} {shlex.quote(args.control)}')
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                check_process()
                log = shell(f'tail -c +{offset} ' + shlex.quote(args.run + '/stdout.log'))
                if 'LINUX_GUEST_SIGNAL' in log or 'LINUX_MEMORY_GUARD_EXIT' in log:
                    raise RuntimeError('session failed; inspect the original log')
                if args.classic_modes and any(int(s)!=sequence for s in re.findall(r'LINUX_CLASSIC_CONTROL sequence=(\d+)',log)):
                    raise ValueError('another classic-quality request changed the session')
                if args.native_masks and any(int(s)!=sequence for s in re.findall(r'LINUX_NATIVE_HD_CONTROL sequence=(\d+)',log)):
                    raise ValueError('another native HD quality request changed the session')
                boundary, windows = complete_windows(log, sequence, flags)
                if boundary is not None and args.settle_frames:windows=[w for w in windows if w['frame']>boundary+args.settle_frames]
                selected, rejected = warm_windows(windows, args.windows, args.include_steady_uploads,
                                                   allow_diagnostic=args.diagnostic_work)
                if (args.diagnostic_work and any('replay_thread_cpu_ms' not in w or 'work_probe_mask' not in w for w in selected) or
                        args.classic_modes and any('classic_enabled' not in w for w in selected) or
                        args.classic_memos and any('classic_memo' not in w for w in selected) or
                        args.native_masks and any('native_requested_mask' not in w for w in selected) or
                        flags&131072 and any('hot_env_cache' not in w for w in selected) or
                        flags&262144 and any('classic_flag_specialization' not in w for w in selected) or
                        flags&524288 and any('classic_code_cache' not in w for w in selected) or
                        flags&8388608 and any('classic_read_cache' not in w for w in selected) or
                        flags&1048576 and any('support_scale' not in w for w in selected) or
                        flags&2097152 and any('sprite_nearest' not in w for w in selected) or
                        flags&4194304 and any('shared_async' not in w for w in selected) or
                        flags&16777216 and any('quad_batch' not in w for w in selected) or
                        flags&33554432 and any('vector_equal' not in w for w in selected) or
                        flags&67108864 and any('temp_index' not in w for w in selected) or
                        flags&134217728 and any('code_lease' not in w for w in selected) or
                        flags&268435456 and any('control_lease' not in w for w in selected) or
                        flags&536870912 and any('sprite_batch' not in w for w in selected) or
                        flags&1073741824 and any('record_state' not in w for w in selected) or
                        flags&2147483648 and any('shared_alias' not in w for w in selected) or
                        args.classic_effects and any('classic_effects' not in w for w in selected)):
                    selected=[]  # Wait for the rest of the same profile block.
                if selected:
                    expected_work=(flags>>10)&127
                    if args.diagnostic_work and any(w['work_probe_mask']!=expected_work or
                            w['gameplay_fps_valid']!=(expected_work==0) for w in selected):
                        raise ValueError('actual work probe differs from acknowledged flags or validity')
                    if args.classic_modes and any(w['classic_enabled']!=bool(classic_mode) for w in selected):
                        raise ValueError('actual classic quality differs from acknowledgement')
                    if args.classic_memos and any(w['classic_memo']!=bool(classic_memo) for w in selected):
                        raise ValueError('actual classic metadata reuse differs from acknowledgement')
                    if args.classic_effects and any(w['classic_effects']!=bool(classic_effects) for w in selected):
                        raise ValueError('actual classic effects policy differs from acknowledgement')
                    if args.native_masks and any(w['native_requested_mask']!=native_mask or
                            w['native_active_mask']!=native_mask or (native_mask and w['native_declined_batches'])
                            for w in selected):
                        raise ValueError('actual native HD quality differs or includes declined batches')
                    if any('hot_env_cache' in w and w['hot_env_cache']!=bool(flags&131072) for w in selected):
                        raise ValueError('actual diagnostic path cache differs from acknowledgement')
                    if any('classic_flag_specialization' in w and w['classic_flag_specialization']!=bool(flags&262144) for w in selected):
                        raise ValueError('actual classic flag specialization differs from acknowledgement')
                    if flags&262144 and any(not w['classic_specialized_draws'] or w['classic_flag_fallbacks'] for w in selected):
                        raise ValueError('classic flag specialization has no admitted draws or includes fallbacks')
                    if any('classic_code_cache' in w and w['classic_code_cache']!=bool(flags&524288) for w in selected):
                        raise ValueError('actual classic code checks cache differs from acknowledgement')
                    if flags&524288 and any(not w['classic_code_checks'] or not w['classic_code_hits'] for w in selected):
                        raise ValueError('classic code checks cache has no admitted checks or reuse')
                    if any('classic_read_cache' in w and w['classic_read_cache']!=bool(flags&8388608) for w in selected):
                        raise ValueError('actual classic resource check cache differs from acknowledgement')
                    if flags&8388608 and any(not w['classic_read_checks'] or not w['classic_read_hits'] for w in selected):
                        raise ValueError('classic resource check cache has no admitted checks or reuse')
                    if any('support_scale' in w and w['support_scale']!=bool(flags&1048576) for w in selected):
                        raise ValueError('actual support scale constants differ from acknowledgement')
                    if flags&1048576 and any(not w['support_scale_draws'] or w['support_scale_fallbacks'] for w in selected):
                        raise ValueError('support scale constants have no admitted draws or include fallbacks')
                    if any('sprite_nearest' in w and w['sprite_nearest']!=bool(flags&2097152) for w in selected):
                        raise ValueError('actual sprite filter differs from acknowledgement')
                    if flags&2097152 and any(not w['sprite_nearest_draws'] or w['sprite_nearest_fallbacks'] for w in selected):
                        raise ValueError('nearest sprite filter has no admitted draws or includes fallbacks')
                    if any('shared_async' in w and w['shared_async']!=bool(flags&4194304) for w in selected):
                        raise ValueError('actual shared-pool async mode differs from acknowledgement')
                    if any('shared_alias' in w and w['shared_alias']!=bool(flags&2147483648) for w in selected):
                        raise ValueError('actual shared alias writeback differs from acknowledgement')
                    if flags&2147483648 and any(not w['shared_alias_available'] or not w['shared_alias_ranges'] or
                            not w['shared_alias_bytes'] or w['shared_alias_prepared']!=w['shared_alias_ranges'] for w in selected):
                        raise ValueError('shared alias writeback lacks completed/dirty-prepared actual ranges')
                    if any('quad_batch' in w and w['quad_batch']!=bool(flags&16777216) for w in selected):
                        raise ValueError('actual quad batching differs from acknowledgement')
                    if any('vector_equal' in w and (w['vector_equal']!=bool(flags&33554432) or not w['full_byte_validation']) for w in selected):
                        raise ValueError('actual shader comparison differs or lacks full byte validation')
                    if any('temp_index' in w and w['temp_index']!=bool(flags&67108864) for w in selected):
                        raise ValueError('actual retired buffer index differs from acknowledgement')
                    if flags&67108864 and any(not w['temp_index_lookups'] for w in selected):
                        raise ValueError('retired buffer index has no actual lookups')
                    if any('code_lease' in w and (w['code_lease']!=bool(flags&134217728) or
                            (not flags&268435456 and not w['control_full_each_draw'])) for w in selected):
                        raise ValueError('actual shader code lease differs or control validation was omitted')
                    if flags&134217728 and any(not w['code_lease_available'] or not w['code_lease_full'] or not w['code_lease_reused'] for w in selected):
                        raise ValueError('shader code lease has no actual full checks/reuse')
                    if any('control_lease' in w and w['control_lease']!=bool(flags&268435456) for w in selected):
                        raise ValueError('actual shader control lease differs from acknowledgement')
                    if flags&268435456 and any(not w['control_lease_full'] or not w['control_lease_reused'] for w in selected):
                        raise ValueError('shader control lease has no actual full checks/reuse')
                    if any('record_state' in w and w['record_state']!=bool(flags&1073741824) for w in selected):
                        raise ValueError('actual recording state cache differs from acknowledgement')
                    if flags&1073741824 and any(not w['record_state_available'] or not w['record_state_checks'] or
                            not w['record_state_omitted'] or not w['record_state_bytes'] or w['record_state_omitted']>w['record_state_checks'] for w in selected):
                        raise ValueError('recording state cache lacks actual redundant-state omissions')
                    if any('sprite_batch' in w and w['sprite_batch']!=bool(flags&536870912) for w in selected):
                        raise ValueError('actual sprite batching differs from acknowledgement')
                    if flags&536870912 and any(not batch_counts_valid(w,'sprite',allow_diagnostic=args.diagnostic_work) for w in selected):
                        raise ValueError('sprite batching has no merges or inconsistent actual draw counts')
                    if flags&16777216 and any(not batch_counts_valid(w,'quad',allow_diagnostic=args.diagnostic_work) for w in selected):
                        raise ValueError('quad batching has no merges or inconsistent actual draw counts')
                    if flags&4194304 and any(not w['shared_async_available'] or not w['shared_async_submits'] or not w['shared_async_presents'] for w in selected):
                        raise ValueError('shared-pool async mode has no admitted submits/presents')
                    if flags&4194304 and any(not w['shared_async_completed_submits'] or not w['shared_async_completed_presents'] or
                            abs(w['shared_async_completed_submits']-w['shared_async_submits'])>2 or
                            abs(w['shared_async_completed_presents']-w['shared_async_presents'])>2 for w in selected):
                        raise ValueError('shared-pool async mode lacks matching actual fence/effect completions')
                    phase = dict(sequence=sequence, flags=flags, after_frame=boundary,
                        classic_enabled=classic_mode,
                        classic_memo=classic_memo,classic_effects=classic_effects,native_mask=native_mask,
                        work_probe_mask=(flags>>10)&127, gameplay_fps_valid=not bool((flags>>10)&127),
                        windows=selected, rejected_warmup_frames=rejected,settle_frames=args.settle_frames,
                        fps=60000 * len(selected) / sum(w['elapsed_ms'] for w in selected))
                    phases.append(phase)
                    receipt['last_acknowledged_flags'] = flags
                    receipt['pending'] = None
                    args.output.write_text(json.dumps(receipt, indent=2) + '\n')
                    pending = None
                    print(json.dumps(phase), flush=True)
                    break
                time.sleep(3)
            else:
                raise TimeoutError('no complete windows after acknowledgement')
        receipt['complete'] = True
    finally:
        receipt['pending'] = pending
        args.output.write_text(json.dumps(receipt, indent=2) + '\n')
        if pending:
            print('Interrupted: requested flags may remain active; see receipt pending field.', flush=True)


if __name__ == '__main__':
    main()

// INPUT:  Explicit Linux performance-control file and completed profile windows.
// OUTPUT: Latched cache/query A/B settings and an exact live texture inventory.
// POS:    Render-thread diagnostics; dropping work requires a second explicit opt-in.
void dm_linux_native_hd_admission(int admitted);
void dm_linux_native_hd_poll(uint64_t frame);
void dm_linux_native_hd_profile(uint64_t frame);
void dm_linux_native_hd_world_frame(int rendered,uint64_t frame);
int dm_linux_record_state_cache_available(void);
void dm_linux_record_state_cache_config(int enabled);
void dm_linux_record_state_cache_stats(uint64_t out[6]);
static void linux_texture_census(uint64_t sequence, uint64_t frame) {
    const char *directory = getenv("DM_FRAME_DIR");
    if (!directory) return;
    char path[1400];
    if (snprintf(path, sizeof path, "%s/textures-%llu.csv", directory,
            (unsigned long long)sequence) >= (int)sizeof path)
        die("Linux texture census path", sequence);
    FILE *out = fopen(path, "w");
    if (!out) die("Linux texture census open", errno);
    fprintf(out, "serial,width,height,depth,levels,layers,nvn_format,vk_format,usage,allocation_bytes,extra_bytes,cpu_baseline_bytes\n");
    uint64_t bytes = 0, extra = 0, baseline = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < LIMIT_OBJECTS; ++i) {
        const VTexture *t = &G.textures[i];
        if (!t->serial || !t->image) continue;
        uint64_t other = t->before_storage_allocation_size +
            t->sample_allocation_size + t->shadow_allocation_size;
        fprintf(out, "%llu,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu\n",
            (unsigned long long)t->serial, t->width, t->height, t->depth,
            t->source.d.levels, t->source.L.layers, t->source.d.format,
            t->format, t->usage, (unsigned long long)t->allocation_size,
            (unsigned long long)other, (unsigned long long)t->cpu_snapshot_size);
        ++count; bytes += t->allocation_size; extra += other;
        baseline += t->cpu_snapshot_size;
    }
    if (fclose(out)) die("Linux texture census close", errno);
    fprintf(stderr, "[nvn-vulkan] LINUX_TEXTURE_CENSUS sequence=%llu frame=%llu textures=%u image_bytes=%llu extra_bytes=%llu cpu_baseline_bytes=%llu path=%s\n",
        (unsigned long long)sequence, (unsigned long long)frame, count,
        (unsigned long long)bytes, (unsigned long long)extra,
        (unsigned long long)baseline, path);
}

#include "nvn_vk_perf_flags.h"
/* Optional request: "increasing_sequence flags", flags 0..4294967295:
 * bit 0 query-reset ranges, bit 1 command-state cache, bit 2 last-PSO cache.
 * bit 3 full-byte shader identity memoization (never pointer-only identity).
 * bit 4 byte-complete immutable driver-table comparisons from CPU mirrors.
 * bit 5 diagnostic ONLY: omit occlusion queries to attribute their GPU cost.
 * Bit 5 does not preserve sample counter semantics; never claim playable FPS.
 * bit 6 preserves counters: omit only draws killed by a later known reset.
 * bit 7 disables optional per-pass diagnostic timestamps (game counters stay).
 * bit 8 omits a proved empty fragment function in depth-only rendering.
 * bit 9 narrows layout transitions to all uses permitted by each layout.
 * Bits 10..16 require DM_LINUX_WORK_PROBES=1; ALL invalidate playable FPS:
 * 10 scissor 1x1, 11 three vertices/indices, 12 drop GPU draws after binding,
 * 13 drop draw replay early, 14 drop square depth-only draws (shadow probe),
 * 15 drop screen-depth draws, 16 drop full-size single-color+depth draws.
 * Bit 17 caches launch-time diagnostic paths; it is not a work-drop probe.
 * Bit 18 specializes two CPU-known classic compositor flag words exactly.
 * Bit 19 reuses complete code/control hazard checks within one immutable batch.
 * Bit 20 folds the immutable Linux support UBO scales to float one.
 * Bit 21 selects nearest classic sprite filtering (quality option; requires 20).
 * Bit 22 opts into uncached shared-pool async commit with per-epoch ownership.
 * Bit 23 reuses fixed-batch resource hazards until a binding changes.
 * Bit 24 batches proved readonly consecutive 20-byte UI quad strips.
 * Bit 25 uses exact vector equality for live shader control/code validation.
 * Bit 26 indexes exact size/usage of fence-retired temporary buffers.
 * Bit 27 reuses full-code identity under protected pages, pool/epoch and GPU-write guards.
 * Bit 28 extends that proof to tracked live pools or watched private guest-heap control pages.
 * Bit 29 batches the reviewed readonly 36-byte classic sprite shader pair.
 * Bit 30 omits exact redundant idempotent states while recording one command buffer.
 * Bit 31 omits DMA alias self-copies only after the matching fence and CPU ownership return.
 * Called only after a complete profile window, while holding the recorder lock.
 * Existing query indices and byte-complete cache comparisons retain semantics.
 * Keeping the file does not reapply it or take a second inventory. */
static void linux_classic_control_poll(uint64_t frame) {
    const char *path=getenv("DM_LINUX_CLASSIC_CONTROL");
    if(!G.classic_available||!path)return;
    if(strncmp(path,"/run/",5))die("classic quality control requires /run path",0);
    FILE *in=fopen(path,"r");if(!in)return;
    char text[96]={0},tail;size_t n=fread(text,1,sizeof text-1,in);
    int extra=fgetc(in),error=ferror(in);fclose(in);
    static uint64_t sequence;unsigned long long pid,request;
    unsigned enabled,memo=(unsigned)G.classic_memo,effects=(unsigned)G.classic_freeze_effects;
    int fields=sscanf(text,"%llu %llu %u %u %u %c",&pid,&request,&enabled,&memo,&effects,&tail);
    if(fields!=5){effects=(unsigned)G.classic_freeze_effects;
        fields=sscanf(text,"%llu %llu %u %u %c",&pid,&request,&enabled,&memo,&tail)==4?4:0;}
    if(!fields){memo=(unsigned)G.classic_memo;
        fields=sscanf(text,"%llu %llu %u %c",&pid,&request,&enabled,&tail)==3?3:0;}
    if(!n||error||extra!=EOF||memchr(text,0,n)||!fields||pid!=(unsigned long long)getpid()||
       !request||request<=sequence||enabled>1||memo>1||effects>1||
       (memo&&!G.classic_memo_available)||(effects&&!G.classic_effects_available))return;
    sequence=request;G.classic_cull=(int)enabled;G.classic_memo=(int)memo;G.classic_freeze_effects=(int)effects;
    if(!enabled)dm_linux_native_hd_admission(0);
    fprintf(stderr,"[nvn-vulkan] LINUX_CLASSIC_CONTROL sequence=%llu after_frame=%llu enabled=%u memo=%u freeze_hd_effects=%u\n",
        request,(unsigned long long)frame,enabled,memo,effects);
}
static void linux_performance_poll(uint64_t frame) {
    /* This additional switch permits same-process indexed-strip A/B checks;
     * it does not consume or reinterpret any of the existing uint32 flags. */
    if(G.indexed_batch_available){
        const char *path=getenv("DM_LINUX_INDEX_BATCH_CONTROL");
        if(path&&!strncmp(path,"/run/",5)){
            FILE *in=fopen(path,"r");
            if(in){
                char text[96]={0},tail;size_t n=fread(text,1,sizeof text-1,in);
                int extra=fgetc(in),error=ferror(in);fclose(in);
                unsigned long long pid,sequence;unsigned enabled;
                if(n&&!error&&extra==EOF&&!memchr(text,0,n)&&
                   sscanf(text,"%llu %llu %u %c",&pid,&sequence,&enabled,&tail)==3&&
                   pid==(unsigned long long)getpid()&&sequence>G.indexed_batch_sequence&&enabled<=1){
                    quad_flush();G.indexed_batch_sequence=sequence;G.indexed_batch_enabled=(int)enabled;
                    fprintf(stderr,"[nvn-vulkan] INDEXED_SPRITE_CONTROL sequence=%llu after_frame=%llu enabled=%u process_only=1\n",sequence,(unsigned long long)frame,enabled);
                }
            }
        }
    }
    dm_linux_native_hd_poll(frame);
    linux_classic_control_poll(frame);
    const char *path = getenv("DM_LINUX_PERF_CONTROL");
    if (!path) return;
    FILE *in = fopen(path, "r");
    if (!in) return;
    char text[96] = {0};
    size_t size = fread(text, 1, sizeof text - 1, in);
    int extra = fgetc(in), error = ferror(in);
    fclose(in);
    unsigned long long requested = 0;
    unsigned flags = 0;
    static uint64_t sequence;
    if (!size || error || extra != EOF || memchr(text, 0, size) ||
        !nvn_vk_perf_flags_parse(text,&requested,&flags) ||
       !requested || requested <= sequence || ((flags&2097152)&&!(flags&1048576)) ||
       ((flags&0x80000000u)&&(!G.linux_shared_async_available||!(flags&4194304))) ||
       ((flags&134217728)&&(!G.code_lease_available||!(flags&8))) ||
       ((flags&268435456)&&!(flags&134217728)) ||
       ((flags&536870912)&&(!G.sprite_batch_available||!(flags&16777216))) ||
       ((flags&1073741824u)&&!dm_linux_record_state_cache_available()) ||
       ((flags&4194304)&&!G.linux_shared_async_available)||((flags&16777216)&&!G.quad_batch_available)) return;
    const char *work=getenv("DM_LINUX_WORK_PROBES");
    if(((flags>>10)&127u)&&(!work||strcmp(work,"1")))return;
    sequence = requested;
    G.enable_query_reset_range = (flags & 1) != 0;
    G.enable_command_cache = (flags & 2) != 0;
    G.enable_pso_last = (flags & 4) != 0;
    G.last_pso_slot = UINT32_MAX;
    G.enable_identity_cache = (flags & 8) != 0;
    G.enable_driver_cpu_mirror = (flags & 16) != 0;
    G.shared_writeback_alias=(flags&0x80000000u)!=0;
    G.diag_no_occlusion = (flags & 32) != 0;
    G.enable_counter_liveness = (flags & 64) != 0;
    G.pass_timing = G.pass_stats || (G.pass_timing_configured && !(flags & 128));
    G.enable_empty_depth_fragment = (flags & 256) != 0;
    G.enable_layout_scopes = (flags & 512) != 0;
    G.enable_hot_env_cache = (flags & 131072) != 0;
    G.enable_classic_flags = (flags & 262144) != 0;
    G.classic_code_cache = (flags & 524288) != 0;
    G.classic_read_cache = (flags & 8388608) != 0;
    G.enable_support_scale = (flags & 1048576) != 0;
    G.enable_sprite_nearest = (flags & 2097152) != 0;
    quad_flush();G.quad_batch_enabled=(flags&16777216)!=0;
    G.sprite_batch_enabled=(flags&536870912)!=0;
    G.vector_equal_enabled=(flags&33554432)!=0;
    G.code_lease_enabled=(flags&134217728)!=0;
    if(G.control_lease_enabled&&!(flags&268435456))nvn_dirty_drop_control_watches(NULL,0);
    G.control_lease_enabled=(flags&268435456)!=0;
    dm_linux_record_state_cache_config((flags&1073741824u)!=0);
    if(!G.temp_index_enabled&&(flags&67108864)){
        memset(&G.temp_index,0,sizeof G.temp_index);
        for(unsigned i=0;i<G.nretired;i++)if(!nvn_retired_add(&G.temp_index,i,G.retired_buffers[i]->size,G.retired_buffers[i]->usage))die("retired buffer index rebuild",i);
    }
    G.temp_index_enabled=(flags&67108864)!=0;
    if(G.linux_shared_async_available&&G.async_enabled!=((flags&4194304)!=0)){
        async_drain_locked();G.async_enabled=(flags&4194304)!=0;
    }
    if(work&&!strcmp(work,"1")){
        G.diagnostic_work=(flags>>10)&127u;
        G.diag_shape=(int)(G.diagnostic_work&3u);
        G.diag_skip_draws=(G.diagnostic_work&4u)!=0;
    }
    if(G.diagnostic_work)fprintf(stderr,"[nvn-vulkan] LINUX_DIAGNOSTIC_WORK mask=%u gameplay_fps_valid=0 sequence=%llu\n",G.diagnostic_work,requested);
    if(G.diag_no_occlusion)fprintf(stderr,"[nvn-vulkan] LINUX_DIAGNOSTIC_NO_OCCLUSION gameplay_fps_valid=0 sequence=%llu\n",requested);
    fprintf(stderr, "[nvn-vulkan] LINUX_PERF_CONTROL sequence=%llu after_frame=%llu flags=%u query_range=%d command_cache=%d pso_last=%d shader_identity_cache=%d mono_ns=%llu\n",
        requested, (unsigned long long)frame, flags, G.enable_query_reset_range,
        G.enable_command_cache, G.enable_pso_last,G.enable_identity_cache,
        (unsigned long long)dm_android_nvn_clock_ns());
    linux_texture_census(sequence, frame);
#if defined(DM_LINUX_RESOURCE_PROBE) && DM_LINUX_RESOURCE_PROBE
    resource_probe_census(sequence);
#endif
}

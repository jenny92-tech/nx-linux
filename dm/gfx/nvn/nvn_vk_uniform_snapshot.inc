/* Opt-in Linux dense-pool UB versions. The canonical pool still receives every
 * update in order, before a non-uniform observation and before final writeback.
 * Only the existing whole-draw readonly proof may substitute immutable bytes.
 * Do not memcpy updates into a backing already referenced by an earlier draw. */
static int uniform_pending_overlap(Range range) {
    for(unsigned i=0;i<S.nuniform_pending;i++){
        Range p=S.uniform_pending[i].range;
        if(overlap(range.addr,range.size,p.addr,p.size))return 1;
    }
    return 0;
}
static int uniform_shader_barrier_reject(unsigned reason) {
    if(G.profile_detail)G.profile_uniform_barrier_rejects[reason]++;
    return 0;
}
/* Shadow evaluation for the no_pending early return below. Candidates are
 * blocked when any guard fails; reasons may overlap, so a candidate can appear
 * in several counts. A candidate that passes everything is only a candidate:
 * these are cheap necessary-condition filters, NOT a safety proof that the
 * INVALIDATE_SHADER barrier could be elided. Reads S/G state only: no guest
 * pointer reads, no allocations, no driver calls. Runs solely under
 * G.profile_detail; when that is 0 the caller returns immediately. */
enum {
    IDLE_SHADOW_PASS_OPEN,IDLE_SHADOW_LAST_DRAW_READABLE,IDLE_SHADOW_PRIOR_WRITER,
    IDLE_SHADOW_FEEDBACK,IDLE_SHADOW_DEPTH_INPUT,IDLE_SHADOW_TRACE,
    IDLE_SHADOW_WRITE_PREFIX,IDLE_SHADOW_UNKNOWN_GPU_WRITE,IDLE_SHADOW_PASS_SIDE_EFFECTS,
    IDLE_SHADOW_PASS_DRAW,IDLE_SHADOW_PENDING_CLEAR,IDLE_SHADOW_HAZARD_DIAG,
    IDLE_SHADOW_DENSE_POOLS,
    IDLE_SHADOW_REASONS
};
/* G declares these slots as IDLE_SHADOW_MAX_REASONS=16 before this helper. */
typedef char idle_shadow_reasons_fit[IDLE_SHADOW_REASONS<=16?1:-1];
#ifdef DM_LINUX_HOST
/* Dense-only Linux pool modes. Live VPool inspection reads registries only. */
static int uniform_idle_shadow_dense_pools(void) {
    if(G.linux_shared_pools||pool_windows.enabled||pool_windows.gpu_writes)return 0;
    for(int i=0;i<G.npool;i++){
        VPool *p=&G.pools[i];
        if(p->windowed||p->gpu_windows||p->v.external_fd_plus_one)return 0;
    }
    return 1;
}
#endif
static void uniform_idle_shadow_note(void) {
    if(!G.profile_detail)return;
    G.profile_idle_shadow_attempts++;
    unsigned blocked=0;
    if(!S.in_pass)blocked|=1u<<IDLE_SHADOW_PASS_OPEN;
    /* The existing whole-draw proof is exactly S.read_snapshots_allowed: it
     * covers both stages, so a readonly last draw has no writable alias. */
    if(!S.read_snapshots_allowed)blocked|=1u<<IDLE_SHADOW_LAST_DRAW_READABLE;
    if(S.uniform_shader_write_seen)blocked|=1u<<IDLE_SHADOW_PRIOR_WRITER;
    if(S.feedback_flags||S.pass_feedback_flags)blocked|=1u<<IDLE_SHADOW_FEEDBACK;
    if(S.depth_input||S.pass_depth_input)blocked|=1u<<IDLE_SHADOW_DEPTH_INPUT;
    if(G.trace_frame||G.capture_feedback)blocked|=1u<<IDLE_SHADOW_TRACE;
    if(S.uniform_gpu_visible_writes>(unsigned)S.nwrites)blocked|=1u<<IDLE_SHADOW_WRITE_PREFIX;
    for(unsigned i=S.uniform_gpu_visible_writes;i<(unsigned)S.nwrites;i++)
        if(!S.write_cpu[i]){blocked|=1u<<IDLE_SHADOW_UNKNOWN_GPU_WRITE;break;}
    if(G.pass_side_effects)blocked|=1u<<IDLE_SHADOW_PASS_SIDE_EFFECTS;
    if(G.draws<=G.pass_draws0)blocked|=1u<<IDLE_SHADOW_PASS_DRAW;
    if(S.pending_clear||S.flushing_clear)blocked|=1u<<IDLE_SHADOW_PENDING_CLEAR;
    /* Pending wide graphics stages would be the real producer for a future
     * binding, and diagnostic barrier edits make counts unreadable. */
    if(G.hz_diag||G.diag_barrier||G.barrier_pass_elide||S.pend[HZ_GV].stages)blocked|=1u<<IDLE_SHADOW_HAZARD_DIAG;
#ifdef DM_LINUX_HOST
    if(!uniform_idle_shadow_dense_pools())blocked|=1u<<IDLE_SHADOW_DENSE_POOLS;
#else
    /* Non-Linux hosts lack the dense-pool proof inputs: mark unavailable
     * without inventing eligibility. */
    blocked|=1u<<IDLE_SHADOW_DENSE_POOLS;
#endif
    if(!blocked)G.profile_idle_shadow_eligible++;
    for(unsigned r=0;r<IDLE_SHADOW_REASONS;r++)if(blocked&(1u<<r))G.profile_idle_shadow_blocked[r]++;
}
static int uniform_shader_barrier_can_defer(int32_t bits) {
    /* Pure shader-cache invalidation carries no NVN primitive/fragment order
     * bits. Immutable CPU versions can satisfy its uniform reads without a
     * canonical transfer. This is NOT permission to omit an ordering barrier.
     * A later non-uniform observation still flushes before its own command.
     * Detail counters preserve the first failed proof so text-triggered pass
     * fragmentation can be attributed without disabling guest work. */
    if(bits!=NVN_BARRIER_INVALIDATE_SHADER_BIT)return uniform_shader_barrier_reject(0);
    if(!G.uniform_shader_barrier||!G.uniform_snapshot||G.render_slots!=1||!G.precise_sync)return uniform_shader_barrier_reject(1);
    if(!S.nuniform_pending){uniform_idle_shadow_note();return uniform_shader_barrier_reject(2);}
    if(!S.in_pass)return uniform_shader_barrier_reject(3);
    if(!S.read_snapshots_allowed)return uniform_shader_barrier_reject(4);
    if(S.uniform_shader_write_seen)return uniform_shader_barrier_reject(5);
    if(S.feedback_flags||S.pass_feedback_flags)return uniform_shader_barrier_reject(6);
    if(S.depth_input||S.pass_depth_input)return uniform_shader_barrier_reject(7);
    if(G.trace_frame||G.capture_feedback)return uniform_shader_barrier_reject(8);
    /* A prior GPU pool writer may feed a different future shader binding.
     * Only a real wide graphics memory dependency covers that unknown future
     * binding. Keep the full history for snapshot provenance and writeback;
     * only its already-visible prefix stops forcing repeated pass breaks.
     * Checking overlap with the last draw alone would be insufficient. */
    for(unsigned i=S.uniform_gpu_visible_writes;i<(unsigned)S.nwrites;i++)
        if(!S.write_cpu[i])return uniform_shader_barrier_reject(9);
    return 1;
}
static void uniform_flush_all(void) {
    if(!S.nuniform_pending)return;
    /* No pool_read/require_gpu_write recursion: these entries only admit dense
     * ordinary pools, whose identity cannot disappear during a submission. */
    all_barrier();
    for(unsigned i=0;i<S.nuniform_pending;i++){
        /* Updates may partially overlap. Preserve the original transfer WAW
         * dependency even though all of them now occur outside the pass. */
        if(i)all_barrier();
        Range r=S.uniform_pending[i].range;uint64_t off=0;
        VPool *p=pool_find(r.addr,r.size,&off);
        if(!p||p->serial!=S.uniform_pending[i].pool_serial||p->windowed||p->v.external_fd_plus_one)
            die("deferred uniform pool identity",r.addr);
        pool_materialize(p);
        vkCmdUpdateBuffer(S.command,p->v.b,off,r.size,S.uniform_pending[i].payload);
    }
    /* A consumer may have issued its old barrier before pool_read triggered
     * this flush. Publish the final transfer write here as well: an immediate
     * copy/read must not race the last update materialized after that barrier. */
    all_barrier();
    S.nuniform_pending=0;G.profile_uniform_flushes++;
}
static void uniform_flush_range(uint64_t address,uint64_t bytes) {
    if(S.nuniform_pending&&uniform_pending_overlap((Range){address,bytes})){G.profile_uniform_read_flushes++;uniform_flush_all();}
}
static int uniform_defer(uint64_t address,uint64_t bytes,const uint8_t *payload) {
    if(!G.uniform_snapshot||G.render_slots!=1||!bytes||bytes>65536||bytes%4||G.trace_frame||G.capture_feedback)return 0;
#ifdef DM_LINUX_HOST
    if(G.linux_shared_pools)return 0;
#endif
    uint64_t off=0;VPool *p=pool_find(address,bytes,&off);
    if(!p||off%4||p->windowed||p->gpu_windows||p->v.external_fd_plus_one||
       (p->flags&(NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE|NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS))||
       ghost_pool_override(address,bytes,&off))return 0;
    if(S.nuniform_pending==LIMIT_WRITES)uniform_flush_all();
    unsigned i=S.nuniform_pending++;
    S.uniform_pending[i].range=(Range){address,bytes};S.uniform_pending[i].payload=payload;S.uniform_pending[i].pool_serial=p->serial;
    mark_write_payload(address,bytes,payload);G.profile_uniform_deferred++;return 1;
}
static int uniform_snapshot_binding(Range range,VkDescriptorBufferInfo *binding) {
    if(!G.uniform_snapshot||!S.read_snapshots_allowed||!range.size||range.size>65536||
       G.trace_frame||G.capture_feedback||!uniform_pending_overlap(range))return 0;
    /* A current submission's GPU producer cannot be reconstructed on the CPU.
     * Even a later covering CPU update conservatively retains the old path. */
    for(int i=0;i<S.nwrites;i++)if(!S.write_cpu[i]&&overlap(range.addr,range.size,S.writes[i].addr,S.writes[i].size))return 0;
    uint64_t off=0;VPool *p=pool_find(range.addr,range.size,&off);
    if(!p||p->windowed||p->gpu_windows||p->v.external_fd_plus_one||ghost_pool_override(range.addr,range.size,&off))return 0;
    pool_output_wait(range.addr,range.size);
    VBuffer *snapshot=NULL;int cached=cached_snapshot(range,&snapshot);
    if(!snapshot){
        /* The ordinary uploader prepares uncovered guest bytes but omits
         * queued writes. Overlay immutable CPU payloads in their original
         * order, exactly as the pre-existing unaligned readonly snapshot. */
        p=pool_read_resolved(p,range.addr,range.size,&off);
        snapshot=temporary_named(range.size,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,TEMP_ALIGNED);
        memcpy(snapshot->map,p->v.map+off,range.size);
        for(int i=0;i<S.nwrites;i++)if(S.write_cpu[i]&&overlap(range.addr,range.size,S.writes[i].addr,S.writes[i].size)){
            Range w=S.writes[i];uint64_t lo=range.addr>w.addr?range.addr:w.addr;
            uint64_t hi=range.addr+range.size<w.addr+w.size?range.addr+range.size:w.addr+w.size;
            memcpy(snapshot->map+lo-range.addr,S.write_cpu[i]+lo-w.addr,hi-lo);
        }
        if(cached>=0){S.read_snapshots[cached].buffer=snapshot;S.read_snapshots[cached].write_count=S.nwrites;}
        G.profile_uniform_snapshots++;G.profile_uniform_snapshot_bytes+=range.size;
    }
    *binding=(VkDescriptorBufferInfo){snapshot->b,0,range.size};return 1;
}

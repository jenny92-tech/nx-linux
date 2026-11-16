// INPUT:  Explicit DM_LINUX_RESOURCE_PROBE build and runtime opt-ins.
// OUTPUT: Upload/pass-break counters, shader stage timings and copy provenance.
// POS:    Diagnostic-only observation; never changes resource contents or lifetime.
#if defined(DM_LINUX_HOST) && defined(DM_LINUX_RESOURCE_PROBE) && DM_LINUX_RESOURCE_PROBE
static struct {
    int enabled;
    uint64_t uploads,first_uploads,reuploads,refresh_transfers,pass_breaks;
    uint64_t source_bytes,staged_bytes,upload_wall_ns;
    uint64_t copies,expanded_copies,copy_pass_breaks;
} resource_probe;
static void resource_probe_init(void) {
    const char *value=getenv("DM_LINUX_RESOURCE_PROBE");
    resource_probe.enabled=value&&!strcmp(value,"1");
    if(resource_probe.enabled)fprintf(stderr,"[nvn-vulkan] RESOURCE_PROBE enabled=1 observational=1\n");
}
static uint64_t resource_probe_now(void) {
    return resource_probe.enabled?dm_android_nvn_clock_ns():0;
}
static void resource_probe_transfer(void) {
    if(resource_probe.enabled)resource_probe.pass_breaks+=S.in_pass!=0;
}
static void resource_probe_upload(VTexture *t,const tex_t *source,uint64_t started,uint64_t staged) {
    if(!resource_probe.enabled)return;
    resource_probe.uploads++;
    if(t->upload_stamp)resource_probe.reuploads++;else resource_probe.first_uploads++;
    resource_probe.source_bytes+=source->L.size;
    resource_probe.staged_bytes+=staged;
    resource_probe.upload_wall_ns+=dm_android_nvn_clock_ns()-started;
}
static void resource_probe_refresh(uint64_t staged) {
    if(!resource_probe.enabled)return;
    resource_probe.refresh_transfers++;
    resource_probe.staged_bytes+=staged;
}
static void resource_probe_copy(VTexture *source,VTexture *destination,int expanded) {
    if(!resource_probe.enabled)return;
    resource_probe.copies++;resource_probe.copy_pass_breaks+=S.in_pass!=0;
    if(!expanded)return;
    resource_probe.expanded_copies++;
    destination->probe_expanded_copies++;
    destination->probe_astc_source_format=source->source.d.format;
    if(destination->probe_expanded_copies==1)
        fprintf(stderr,"[nvn-vulkan] RESOURCE_EXPANDED_TARGET frame=%llu source=%llu destination=%llu source_format=%u destination_format=%u vk_format=%u width=%u height=%u levels=%u layers=%u allocation_bytes=%llu\n",
            (unsigned long long)G.present_count+1,(unsigned long long)source->serial,
            (unsigned long long)destination->serial,source->source.d.format,destination->source.d.format,
            destination->format,destination->width,destination->height,destination->source.d.levels,
            destination->source.L.layers,(unsigned long long)destination->allocation_size);
}
static void resource_probe_shader(const char *key,uint32_t stage,int warm,uint64_t started,
        uint64_t read_end,uint64_t adapt_end) {
    if(!resource_probe.enabled)return;
    /* Compiler workers also emit this single line. They do not touch the
     * render-thread counters. Parse key+stage+warm to find repeated work. */
    uint64_t ended=dm_android_nvn_clock_ns();
    fprintf(stderr,"[nvn-vulkan] RESOURCE_SHADER_LOAD key=%s stage=%u warm=%d read_meta_ns=%llu adapt_ns=%llu finalize_ns=%llu total_ns=%llu\n",
        key,stage,warm,(unsigned long long)(read_end-started),(unsigned long long)(adapt_end-read_end),
        (unsigned long long)(ended-adapt_end),(unsigned long long)(ended-started));
}
static void resource_probe_window(uint64_t frame,uint64_t frames) {
    if(!resource_probe.enabled)return;
    uint64_t expanded_bytes=0;unsigned expanded_images=0;
    for(unsigned i=0;i<G.texture_used;i++){
        const VTexture *t=&G.textures[i];
        if(t->serial&&t->image&&t->probe_expanded_copies){expanded_images++;expanded_bytes+=t->allocation_size;}
    }
    fprintf(stderr,"[nvn-vulkan] RESOURCE_PATH_PROFILE frame=%llu frames=%llu uploads=%llu first_uploads=%llu reuploads=%llu refresh_transfers=%llu upload_pass_breaks=%llu source_bytes=%llu staged_bytes=%llu upload_wall_ns=%llu copies=%llu expanded_copies=%llu copy_pass_breaks=%llu expanded_live_images=%u expanded_live_bytes=%llu\n",
        (unsigned long long)frame,(unsigned long long)frames,(unsigned long long)resource_probe.uploads,
        (unsigned long long)resource_probe.first_uploads,(unsigned long long)resource_probe.reuploads,
        (unsigned long long)resource_probe.refresh_transfers,(unsigned long long)resource_probe.pass_breaks,
        (unsigned long long)resource_probe.source_bytes,(unsigned long long)resource_probe.staged_bytes,
        (unsigned long long)resource_probe.upload_wall_ns,(unsigned long long)resource_probe.copies,
        (unsigned long long)resource_probe.expanded_copies,(unsigned long long)resource_probe.copy_pass_breaks,
        expanded_images,(unsigned long long)expanded_bytes);
    /* enabled is immutable after worker creation. Do not rewrite it while
     * compiler workers read it to emit their own shader timing lines. */
    resource_probe.uploads=resource_probe.first_uploads=resource_probe.reuploads=0;
    resource_probe.refresh_transfers=resource_probe.pass_breaks=0;
    resource_probe.source_bytes=resource_probe.staged_bytes=resource_probe.upload_wall_ns=0;
    resource_probe.copies=resource_probe.expanded_copies=resource_probe.copy_pass_breaks=0;
}
static void resource_probe_census(uint64_t sequence) {
    if(!resource_probe.enabled)return;
    const char *root=getenv("DM_FRAME_DIR");if(!root)return;
    char path[1400];
    if(snprintf(path,sizeof path,"%s/resource-textures-%llu.csv",root,(unsigned long long)sequence)>=(int)sizeof path)
        die("resource probe census path",sequence);
    FILE *out=fopen(path,"w");if(!out)die("resource probe census open",errno);
    fprintf(out,"serial,width,height,depth,levels,layers,nvn_format,vk_format,usage,cpu_owned,allocation_bytes,extra_bytes,cpu_baseline_bytes,astc_expanded_copies,astc_source_format\n");
    for(unsigned i=0;i<G.texture_used;i++){
        const VTexture *t=&G.textures[i];if(!t->serial||!t->image)continue;
        fprintf(out,"%llu,%u,%u,%u,%u,%u,%u,%u,%u,%d,%llu,%llu,%llu,%llu,%u\n",
            (unsigned long long)t->serial,t->width,t->height,t->depth,t->source.d.levels,t->source.L.layers,
            t->source.d.format,t->format,t->usage,t->cpu_owned,(unsigned long long)t->allocation_size,
            (unsigned long long)(t->before_storage_allocation_size+t->sample_allocation_size+t->shadow_allocation_size),
            (unsigned long long)t->cpu_snapshot_size,(unsigned long long)t->probe_expanded_copies,t->probe_astc_source_format);
    }
    if(fclose(out))die("resource probe census close",errno);
    fprintf(stderr,"[nvn-vulkan] RESOURCE_TEXTURE_CENSUS sequence=%llu path=%s\n",(unsigned long long)sequence,path);
}
#else
#define resource_probe_init() ((void)0)
#define resource_probe_now() UINT64_C(0)
#define resource_probe_transfer() ((void)0)
#define resource_probe_upload(t,source,started,staged) ((void)(started),(void)(staged))
#define resource_probe_refresh(staged) ((void)0)
#define resource_probe_copy(source,destination,expanded) ((void)0)
#define resource_probe_shader(key,stage,warm,started,read_end,adapt_end) ((void)(started),(void)(read_end),(void)(adapt_end))
#define resource_probe_window(frame,frames) ((void)0)
#define resource_probe_census(sequence) ((void)0)
#endif

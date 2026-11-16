// INPUT: Explicit /run PID/sequence request on the original guest allocation thread.
// OUTPUT: SDK allocator census and optional original SDK cache/management cleanup.
// POS: Linux diagnostic only; no external thread calls or arbitrary page discards.
typedef struct {uintptr_t address;size_t size;} HeapProbeBlock;
typedef struct {uint64_t count,bytes;HeapProbeBlock largest[48];} HeapProbeCensus;
static const char *heap_probe_path;
static int heap_probe_trace;
static pthread_once_t heap_probe_once=PTHREAD_ONCE_INIT;
static _Atomic uint64_t heap_probe_next;
static atomic_flag heap_probe_busy=ATOMIC_FLAG_INIT;
static uint64_t heap_probe_sequence;
static void heap_probe_config(void){
    heap_probe_path=getenv("DM_LINUX_HEAP_PROBE_CONTROL");
    if(heap_probe_path&&strncmp(heap_probe_path,"/run/",5))die("heap probe requires private /run control");
    const char *trace=getenv("DM_LINUX_HEAP_ALLOC_TRACE");heap_probe_trace=trace&&!strcmp(trace,"1");
}
static int heap_probe_block(void *pointer,size_t size,void *opaque){
    HeapProbeCensus *c=opaque;c->count++;c->bytes+=size;
    unsigned i=0;while(i<48&&c->largest[i].size>=size)i++;
    if(i<48){memmove(c->largest+i+1,c->largest+i,(47-i)*sizeof c->largest[0]);c->largest[i]=(HeapProbeBlock){(uintptr_t)pointer,size};}
    // SDK TlsHeapCentral::WalkAllocatedPtrs_ continues only for nonzero callbacks.
    // Its iterator releases the heap lock around callbacks, so this is a census,
    // not an atomic snapshot of allocations made by every game thread.
    return 1;
}
static void heap_probe_after_allocate(void *self,void *allocated,size_t requested,void *caller){
    pthread_once(&heap_probe_once,heap_probe_config);
    if(heap_probe_trace&&requested>=1024*1024){
        static _Atomic unsigned lines;unsigned i=atomic_fetch_add(&lines,1);
        if(i<256)fprintf(stderr,"LINUX_HEAP_LARGE index=%u allocator=%p pointer=%p requested=%zu caller=%p\n",i,self,allocated,requested,caller);
    }
    if(!heap_probe_path)return;
    uint64_t now=now_ns(),before=atomic_load(&heap_probe_next);
    if(now<before||!atomic_compare_exchange_strong(&heap_probe_next,&before,now+200000000))return;
    if(atomic_flag_test_and_set(&heap_probe_busy))return;
    FILE *in=fopen(heap_probe_path,"r");
    if(!in){atomic_flag_clear(&heap_probe_busy);return;}
    char text[96]={0},tail;size_t n=fread(text,1,sizeof text-1,in);int extra=fgetc(in),error=ferror(in);fclose(in);
    unsigned long long pid,sequence;unsigned cleanup;
    if(!n||error||extra!=EOF||memchr(text,0,n)||sscanf(text,"%llu %llu %u %c",&pid,&sequence,&cleanup,&tail)!=3||
       pid!=(unsigned long long)getpid()||!sequence||sequence<=heap_probe_sequence||cleanup>1){atomic_flag_clear(&heap_probe_busy);return;}
    heap_probe_sequence=sequence;
    module_t *sdk=mod_named("sdk");
    typedef int (*Walk)(const void *,int (*)(void *,size_t,void *),void *);
    Walk walk=sdk?(Walk)module_sym(sdk,"_ZNK2nn3mem17StandardAllocator19WalkAllocatedBlocksEPFiPvmS2_ES2_"):NULL;
    uint64_t (*free_size)(const void *)=sdk?(void *)module_sym(sdk,"_ZNK2nn3mem17StandardAllocator16GetTotalFreeSizeEv"):NULL;
    uint64_t (*max_size)(const void *)=sdk?(void *)module_sym(sdk,"_ZNK2nn3mem17StandardAllocator18GetAllocatableSizeEv"):NULL;
    void (*clear)(const void *)=sdk?(void *)module_sym(sdk,"_ZNK2nn3mem17StandardAllocator16ClearThreadCacheEv"):NULL;
    void (*clean)(const void *)=sdk?(void *)module_sym(sdk,"_ZNK2nn3mem17StandardAllocator21CleanUpManagementAreaEv"):NULL;
    if(!walk||!free_size||!max_size||!clear||!clean){
        fprintf(stderr,"LINUX_HEAP_PROBE sequence=%llu unsupported=1\n",sequence);atomic_flag_clear(&heap_probe_busy);return;
    }
    uint64_t started=now_ns(),free_before=free_size(self);
    if(cleanup){clear(self);clean(self);}
    HeapProbeCensus census={0};int result=walk(self,heap_probe_block,&census);
    fprintf(stderr,"LINUX_HEAP_PROBE sequence=%llu allocator=%p cleanup=%u walk_result=%d blocks=%llu allocated_bytes=%llu free_before=%llu free_after=%llu largest_free=%llu elapsed_ns=%llu original_sdk=1 current_thread_cache_flushed=1\n",
        sequence,self,cleanup,result,(unsigned long long)census.count,(unsigned long long)census.bytes,
        (unsigned long long)free_before,(unsigned long long)free_size(self),(unsigned long long)max_size(self),(unsigned long long)(now_ns()-started));
    for(unsigned i=0;i<48&&census.largest[i].size;i++)fprintf(stderr,"LINUX_HEAP_BLOCK sequence=%llu rank=%u address=%#llx bytes=%zu\n",sequence,i,(unsigned long long)census.largest[i].address,census.largest[i].size);
    atomic_flag_clear(&heap_probe_busy);
}

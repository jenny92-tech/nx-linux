// INPUT: Original SDK allocation return on its own guest thread, exact 94012 CPU pool.
// OUTPUT: Optional reclamation of free interiors under the original SDK mutex.
// POS: Linux 94012 allocator extension; preserves capacity and SDK operations.
#include "cpu_pool_trim.h"
#include <sys/mman.h>
#include <errno.h>
static struct {
    pthread_once_t once;int enabled,active;const char *control;
    _Atomic uint64_t next;atomic_flag busy;
    uint64_t sequence,stable_since,attempts,advised_bytes;int context;
    module_t *main;int (*trylock)(void *);void (*unlock)(void *);int (*owned)(const void *);
    DmCpuTrimRange ranges[8192];unsigned char residency[250*256];
} cpu_trim={.once=PTHREAD_ONCE_INIT,.busy=ATOMIC_FLAG_INIT};
static void cpu_trim_config(void){
    const char *e=getenv("DM_LINUX_CPU_POOL_TRIM");cpu_trim.enabled=e&&!strcmp(e,"1");cpu_trim.active=cpu_trim.enabled;
    cpu_trim.control=getenv("DM_LINUX_CPU_POOL_TRIM_CONTROL");
    if(cpu_trim.control){if(strncmp(cpu_trim.control,"/run/",5))die("CPU pool trim requires private /run control");cpu_trim.active=0;}
}
static int cpu_trim_init(void){
    if(cpu_trim.main)return 1;
    module_t *m=mod_named("main"),*sdk=mod_named("sdk");
    static const uint32_t config[]={0x52880008,0x52a1f40a,0x5280c009,0xa9012be8,0x52810008,0xa90223e9};
    static const uint32_t allocation[]={0x9100227a,0xaa1a03e0,0x9435cd93,0xf9400260,0x52800201,0xaa1503e2,0x94358087,0xaa0003f9,0xaa1a03e0,0x9435cd94};
    static const uint32_t release[]={0xb0011896,0x910be2d6,0x910022d5,0xaa1503e0,0x9435c728,0xf94002c0,0xaa1403e1,0x94357aa5,0xaa1503e0,0x9435c72b};
    static const uint32_t unlock[]={0xb9400808,0x71000508,0xb9000808,0x54000040,0xd65f03c0,0xf900081f,0x91006000,0x1408e685};
    if(!m||!sdk||strcmp(m->build_id,"db0507f36b84200ac05f75d76ed5026f")||strcmp(sdk->build_id,"2d369d9999e3e10e665106f840ffff9b")||
       m->text_end!=0x1b786a0||m->size<0x3116320||sdk->size<0x347fa0||
       memcmp(m->base+0x1348c8,config,sizeof config)||memcmp(m->base+0xe036cc,allocation,sizeof allocation)||
       memcmp(m->base+0xe05070,release,sizeof release)||memcmp(sdk->base+0x347f60,unlock,sizeof unlock))die("CPU pool trim requires reviewed original 94012 allocator and SDK");
    cpu_trim.trylock=(void *)module_sym(sdk,"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE");
    cpu_trim.unlock=(void *)module_sym(sdk,"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE");
    cpu_trim.owned=(void *)module_sym(sdk,"_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE");
    if((void *)cpu_trim.trylock!=sdk->base+0x347ef0||(void *)cpu_trim.unlock!=sdk->base+0x347f60||(void *)cpu_trim.owned!=sdk->base+0x347f80)die("CPU pool trim SDK mutex ABI");
    cpu_trim.main=m;return 1;
}
static void cpu_trim_control(void){
    if(!cpu_trim.control)return;
    FILE *f=fopen(cpu_trim.control,"r");if(!f)return;
    char text[96]={0},tail;size_t n=fread(text,1,sizeof text-1,f);int extra=fgetc(f),error=ferror(f);fclose(f);
    unsigned long long pid,sequence;unsigned active;
    if(!n||error||extra!=EOF||memchr(text,0,n)||sscanf(text,"%llu %llu %u %c",&pid,&sequence,&active,&tail)!=3||
       pid!=(unsigned long long)getpid()||!sequence||sequence<=cpu_trim.sequence||active>1)return;
    cpu_trim.sequence=sequence;cpu_trim.active=(int)active;cpu_trim.stable_since=0;
    fprintf(stderr,"LINUX_CPU_POOL_TRIM_CONTROL sequence=%llu active=%u\n",sequence,active);
}
static void cpu_pool_trim_tick(void){
    pthread_once(&cpu_trim.once,cpu_trim_config);if(!cpu_trim.enabled)return;
    uint64_t now=now_ns(),next=atomic_load(&cpu_trim.next);if(now<next)return;
    if(atomic_flag_test_and_set(&cpu_trim.busy))return;
    atomic_store(&cpu_trim.next,now+1000000000);int saved_errno=errno;
    cpu_trim_control();if(!cpu_trim.active||!cpu_trim_init())goto done;
    int dm_linux_menu_scene_context(void),dm_linux_classic_gameplay_context(void);
    int context=dm_linux_classic_gameplay_context()?1:dm_linux_menu_scene_context();
    if(context!=cpu_trim.context){cpu_trim.context=context;cpu_trim.stable_since=now;}
    if(!context||now-cpu_trim.stable_since<5000000000)goto done;
    uintptr_t vars=(uintptr_t)cpu_trim.main->base+0x31162e0,mutex=vars+32;
    if(*(const uint8_t *)mutex!=1||!(cpu_trim.trylock((void *)mutex)&1))goto done;
    // A recursive acquisition indicates that the allocation callback was made
    // inside a pool operation. Release just our level and leave it untouched.
    if(dm_cpu_trim_u32(mutex+8)!=1||!(cpu_trim.owned((void *)mutex)&1))goto unlock;
    uintptr_t base=dm_cpu_trim_u64(vars+8),control=dm_cpu_trim_u64(vars+24);size_t bytes=dm_cpu_trim_u64(vars+16);
    if(base!=control||bytes!=250*1024*1024||!dm_linux_guest_heap_contains(base,bytes))goto unlock;
    DmCpuTrimPlan plan;size_t page=(size_t)sysconf(_SC_PAGESIZE);
    if(!dm_cpu_trim_plan(base,bytes,page,cpu_trim.ranges,8192,&plan)){
        fprintf(stderr,"LINUX_CPU_POOL_TRIM declined=invalid_metadata\n");goto unlock;
    }
    uint64_t advised=0;unsigned calls=0,failed=0;
    for(size_t i=0;i<plan.ranges;i++){
        DmCpuTrimRange r=cpu_trim.ranges[i];if(r.bytes<65536||r.bytes/page>sizeof cpu_trim.residency)continue;
        if(mincore((void *)r.address,r.bytes,cpu_trim.residency))continue;
        size_t resident=0;for(size_t j=0;j<r.bytes/page;j++)resident+=!!(cpu_trim.residency[j]&1);
        if(resident*page<65536)continue;
        if(madvise((void *)r.address,r.bytes,MADV_DONTNEED)){failed++;continue;}
        calls++;advised+=r.bytes;
    }
    cpu_trim.attempts++;cpu_trim.advised_bytes+=advised;
    if(calls||failed||cpu_trim.attempts==1)fprintf(stderr,"LINUX_CPU_POOL_TRIM context=%d original_sdk_lock=1 recursive_depth=1 free_blocks=%zu free_bytes=%zu advised_bytes=%llu calls=%u failed=%u cumulative_advised_bytes=%llu\n",context,plan.blocks,plan.free_bytes,(unsigned long long)advised,calls,failed,(unsigned long long)cpu_trim.advised_bytes);
    atomic_store(&cpu_trim.next,now_ns()+5000000000);
unlock:
    cpu_trim.unlock((void *)mutex);
done:
    errno=saved_errno;atomic_flag_clear(&cpu_trim.busy);
}

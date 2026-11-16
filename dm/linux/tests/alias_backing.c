// INPUT: Owned alias reservation and an unlinked temporary backing file.
// OUTPUT: Reclaim/reload preserves live bytes; decommit zeros only its range.
// POS: Linux virtual-memory/storage regression without game data or a GPU.
#include "../alias_private_pools.h"
#include <assert.h>


static int allow_ram=1;
static uint64_t asked_ram;
static int headroom(uint64_t bytes){asked_ram=bytes;return allow_ram;}
static uint8_t *new_store(DmLinuxAliasBacking *store,const char *root,size_t size) {
    uint8_t *base=mmap(NULL,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(base!=MAP_FAILED);
    assert(!dm_linux_alias_backing_init(store,root,base,size));return base;
}
static void finish_store(DmLinuxAliasBacking *store,DmLinuxAliasPools *pools) {
    assert(!munmap(store->base,store->size)&&!close(store->fd));dm_linux_alias_pools_destroy(pools);
}
static void private_bytes_and_budget(const char *root) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE),total=32*page;
    DmLinuxAliasBacking store;uint8_t *base=new_store(&store,root,total);DmLinuxAliasPools pools;
    assert(!dm_linux_alias_pools_init(&pools,3*page,headroom));
    assert(!dm_linux_alias_backing_map(&store,base,8*page));memset(base,0x19,8*page);
    DmLinuxAliasPoolResult result;
    assert(dm_linux_alias_pools_promote(&pools,&store,base+page,2*page,0,&result)==1);
    assert(result.charged==2*page&&pools.bytes==2*page&&base[page+13]==0x19);
    base[page+13]=0x77;base[2*page+9]=0x66;
    uint8_t original;assert(pread(store.fd,&original,1,page+13)==1&&original==0x19);
    assert(!mprotect(base+page,page,PROT_READ));
    assert(!dm_linux_alias_pools_map(&pools,&store,base+page,2*page)&&base[page+13]==0x77);
    *(volatile uint8_t *)(base+page+13)=0x77;
    assert(!dm_linux_alias_pools_map(&pools,&store,base,4*page)&&base[page+13]==0x77&&base[2*page+9]==0x66);
    assert(dm_linux_alias_pools_promote(&pools,&store,base,3*page,0,&result)==1);
    assert(result.charged==page&&pools.bytes==3*page&&pools.n==1&&asked_ram==3*page);base[11]=0x55;
    assert(dm_linux_alias_pools_promote(&pools,&store,base,3*page,0,&result)==0&&!strcmp(result.skip,"already_private"));
    assert(dm_linux_alias_pools_promote(&pools,&store,base+4*page,page,0,&result)==0&&!strcmp(result.skip,"over_budget"));
    assert(!dm_linux_alias_pools_unmap(&pools,&store,base+page,page)&&pools.bytes==2*page&&pools.n==2);
    assert(!dm_linux_alias_pools_map(&pools,&store,base+page,page));
    for(size_t i=page;i<2*page;i++)assert(!base[i]);
    assert(base[11]==0x55&&base[2*page+9]==0x66);
    allow_ram=0;
    assert(dm_linux_alias_pools_promote(&pools,&store,base+page,page,0,&result)==0&&!strcmp(result.skip,"ram_guard"));
    allow_ram=1;
    assert(dm_linux_alias_pools_promote(&pools,&store,base+page,page,0,&result)==1&&pools.bytes==3*page&&pools.n==1);
    char fdpath[96];snprintf(fdpath,sizeof fdpath,"/proc/self/fd/%d",store.fd);
    int readonly=open(fdpath,O_RDONLY|O_CLOEXEC);assert(readonly>=0);
    DmLinuxAliasBacking bad=store;bad.fd=readonly;
    assert(dm_linux_alias_pools_unmap(&pools,&bad,base,page)==-1);
    assert(base[11]==0x55&&pools.bytes==3*page&&!close(readonly));
    finish_store(&store,&pools);
}
static void private_eligibility(const char *root) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE),total=8*page;
    DmLinuxAliasBacking store;uint8_t *base=new_store(&store,root,total);DmLinuxAliasPools pools;
    assert(!dm_linux_alias_backing_map(&store,base,4*page));memset(base,0x3c,4*page);
    assert(!dm_linux_alias_pools_init(&pools,0,headroom));DmLinuxAliasPoolResult result;
    assert(!dm_linux_alias_pools_promote(&pools,&store,base,page,0,&result)&&!pools.bytes);
    dm_linux_alias_pools_destroy(&pools);assert(!dm_linux_alias_pools_init(&pools,4*page,headroom));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base,page,DM_ALIAS_POOL_CPU_NO_ACCESS,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base,page,DM_ALIAS_POOL_COMPRESSIBLE,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base+1,page,0,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base+total,page,0,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base,0,0,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base,SIZE_MAX,0,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,(void *)(UINTPTR_MAX-page+1),page,0,&result));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base+6*page,page,0,&result)&&!strcmp(result.skip,"not_committed"));
    assert(!dm_linux_alias_pools_promote(&pools,&store,base+3*page,2*page,0,&result)&&!strcmp(result.skip,"not_committed"));
    assert(!pools.bytes&&base[17]==0x3c);
    assert(mmap(base,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED,store.fd,0)==base);base[17]=0x82;
    assert(!dm_linux_alias_pools_promote(&pools,&store,base,page,0,&result)&&!strcmp(result.skip,"untracked_private"));
    assert(!pools.bytes&&base[17]==0x82);
    finish_store(&store,&pools);
    assert(!dm_linux_alias_pools_init(&pools,DM_LINUX_ALIAS_POOL_BUDGET_MAXIMUM+page,headroom));
    assert(pools.budget==DM_LINUX_ALIAS_POOL_BUDGET_MAXIMUM);dm_linux_alias_pools_destroy(&pools);
}
static void private_fragmentation(const char *root) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE),pages=512,total=pages*page;
    DmLinuxAliasBacking store;uint8_t *base=new_store(&store,root,total);DmLinuxAliasPools pools;
    assert(!dm_linux_alias_pools_init(&pools,total,headroom));assert(!dm_linux_alias_backing_map(&store,base,total));
    memset(base,0x3d,total);DmLinuxAliasPoolResult result;
    for(size_t at=0;at<total;at+=DM_LINUX_ALIAS_POOL_MAXIMUM){
        size_t n=total-at;if(n>DM_LINUX_ALIAS_POOL_MAXIMUM)n=DM_LINUX_ALIAS_POOL_MAXIMUM;
        assert(dm_linux_alias_pools_promote(&pools,&store,base+at,n,0,&result)==1);
    }
    for(size_t i=0;i<pages;i+=2)base[i*page]=(uint8_t)(0x80+i%64);
    size_t holes=0;
    for(size_t i=1;i<pages-1;i+=2){assert(!dm_linux_alias_pools_unmap(&pools,&store,base+i*page,page));holes++;}
    assert(pools.n>128&&pools.bytes==(pages-holes)*page);
    assert(!dm_linux_alias_pools_map(&pools,&store,base,total));
    for(size_t i=0;i<pages;i++) {
        if(!(i%2))assert(base[i*page]==(uint8_t)(0x80+i%64));
        else if(i<pages-1)for(size_t j=0;j<page;j++)assert(!base[i*page+j]);
    }
    assert(!dm_linux_alias_pools_unmap(&pools,&store,base,total)&&!pools.bytes&&!pools.n);
    assert(!dm_linux_alias_pools_map(&pools,&store,base,total));
    for(size_t i=0;i<total;i++)assert(!base[i]);
    for(size_t at=0;at<total;at+=DM_LINUX_ALIAS_POOL_MAXIMUM){
        size_t n=total-at;if(n>DM_LINUX_ALIAS_POOL_MAXIMUM)n=DM_LINUX_ALIAS_POOL_MAXIMUM;
        assert(dm_linux_alias_pools_promote(&pools,&store,base+at,n,0,&result)==1);
    }
    assert(pools.bytes==total);
    finish_store(&store,&pools);
}

static void deferred_zero(const char *root){
    size_t page=(size_t)sysconf(_SC_PAGESIZE),total=16*page;DmLinuxAliasBacking s;uint8_t *base=new_store(&s,root,total);
    assert(!dm_linux_alias_backing_map(&s,base,total));memset(base,0x49,total);
    assert(!dm_linux_alias_backing_configure_defer(&s,4*page));
    assert(!dm_linux_alias_backing_unmap(&s,base+page,3*page)&&s.deferred_bytes==3*page);
    assert(!dm_linux_alias_backing_unmap(&s,base+page,3*page)&&s.deferred_bytes==3*page); /* idempotent */
    uint8_t *alias=mmap(NULL,total,PROT_READ,MAP_SHARED,s.fd,0);assert(alias!=MAP_FAILED&&alias[page]==0x49);
    assert(!dm_linux_alias_backing_map(&s,base+2*page,page)&&s.deferred_bytes==2*page&&s.deferred_count==2);
    for(size_t i=0;i<page;i++)assert(!base[2*page+i]&&!alias[2*page+i]);
    assert(alias[page]==0x49&&base[17]==0x49&&base[4*page+11]==0x49);
    assert(!dm_linux_alias_backing_unmap(&s,base+5*page,4*page)&&s.deferred_bytes==2*page&&s.synchronous_unmaps==1);
    assert(!dm_linux_alias_backing_map(&s,base+5*page,4*page));
    for(size_t i=5*page;i<9*page;i++)assert(!base[i]);
    assert(!dm_linux_alias_backing_map(&s,base,total)&&!s.deferred_bytes&&!s.deferred_count);
    assert(base[17]==0x49&&base[4*page+11]==0x49);
    char fdpath[96];snprintf(fdpath,sizeof fdpath,"/proc/self/fd/%d",s.fd);int readonly=open(fdpath,O_RDONLY|O_CLOEXEC);assert(readonly>=0);
    DmLinuxAliasBacking bad=s;bad.fd=readonly;
    assert(dm_linux_alias_backing_unmap(&bad,base,page)==-1&&base[17]==0x49&&!bad.deferred_bytes);assert(!close(readonly));
    assert(!dm_linux_alias_backing_unmap(&s,base+page,page));
    assert(dm_linux_alias_backing_configure_defer(&s,0)==-1); /* cannot discard pending state */
    assert(!dm_linux_alias_dead_flush(&s)&&!s.deferred_bytes);
    assert(!dm_linux_alias_backing_map(&s,base+page,page)&&!base[page]);
    assert(!munmap(alias,total)&&!munmap(base,total)&&!close(s.fd));
}
static void deferred_fragmentation(const char *root){
    size_t page=(size_t)sysconf(_SC_PAGESIZE),pages=1024,total=pages*page;DmLinuxAliasBacking s;uint8_t *base=new_store(&s,root,total);
    assert(!dm_linux_alias_backing_map(&s,base,total)&&!dm_linux_alias_backing_configure_defer(&s,total));memset(base,0x5a,total);
    for(size_t i=1;i<pages;i+=2){assert(!dm_linux_alias_backing_unmap(&s,base+i*page,page));assert(s.deferred_count<=255&&s.deferred_bytes<=s.defer_limit);}
    char fdpath[96];snprintf(fdpath,sizeof fdpath,"/proc/self/fd/%d",s.fd);int readonly=open(fdpath,O_RDONLY|O_CLOEXEC);assert(readonly>=0);
    DmLinuxAliasBacking bad=s;bad.fd=readonly;size_t pending=bad.deferred_bytes;
    assert(dm_linux_alias_backing_map(&bad,base+page,page)==-1&&bad.deferred_bytes==pending);
    assert(dm_linux_alias_dead_flush(&bad)==-1&&bad.deferred_bytes==pending);assert(!close(readonly));
    assert(!dm_linux_alias_backing_map(&s,base,total)&&!s.deferred_count&&!s.deferred_bytes);
    for(size_t i=0;i<pages;i++)for(size_t j=0;j<page;j++)assert(base[i*page+j]==(i%2?0:0x5a));
    assert(!munmap(base,total)&&!close(s.fd));
}

int main(int argc,char **argv) {
    assert(argc==2);size_t total=2u*1024*1024,block=65536;
    uint8_t *base=mmap(NULL,total,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(base!=MAP_FAILED);
    DmLinuxAliasBacking store={.fd=-1};assert(!dm_linux_alias_backing_init(&store,argv[1],base,total));
    assert(!dm_linux_alias_backing_map(&store,base,3*block));
    for(size_t i=0;i<block;i++)base[i]=(uint8_t)(i*37+11);
    base[block+33]=0x83;base[2*block+17]=0x97;
    assert(!dm_linux_alias_backing_map(&store,base,block)&&base[123]==(uint8_t)(123*37+11));
    assert(!msync(base,block,MS_SYNC)&&!madvise(base,block,MADV_DONTNEED));
    for(size_t i=0;i<block;i++)assert(base[i]==(uint8_t)(i*37+11));
    assert(!dm_linux_alias_backing_unmap(&store,base+block,block));
    assert(!dm_linux_alias_backing_unmap(&store,base+block,block));
    assert(!dm_linux_alias_backing_map(&store,base+block,block));
    for(size_t i=block;i<2*block;i++)assert(base[i]==0);
    assert(base[2*block+17]==0x97&&base[123]==(uint8_t)(123*37+11));
    assert(!dm_linux_alias_backing_unmap(&store,base,block));
    assert(!dm_linux_alias_backing_map(&store,base,block));
    for(size_t i=0;i<block;i++)assert(base[i]==0);
    assert(base[2*block+17]==0x97);
    errno=0;assert(dm_linux_alias_backing_map(&store,base+total,block)==-1&&errno==EINVAL);
    errno=0;assert(dm_linux_alias_backing_unmap(&store,base+1,block)==-1&&errno==EINVAL);
    errno=0;assert(dm_linux_alias_backing_map(&store,base,SIZE_MAX)==-1&&errno==EINVAL);
    errno=0;assert(dm_linux_alias_backing_map(&store,(void *)(UINTPTR_MAX-4095),8192)==-1&&errno==EINVAL);
    assert(base[2*block+17]==0x97);
    assert(!munmap(base,total)&&!close(store.fd));
    private_bytes_and_budget(argv[1]);private_eligibility(argv[1]);private_fragmentation(argv[1]);
    deferred_zero(argv[1]);deferred_fragmentation(argv[1]);
    puts("alias file backing and bounded COW pool regressions pass; preserves reloads, zeroes decommits, retains neighbours and rejects invalid ranges");
    return 0;
}

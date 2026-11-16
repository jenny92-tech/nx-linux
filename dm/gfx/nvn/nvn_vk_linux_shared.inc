// INPUT:  Opt-in Linux DMA-heap import and freshly initialized, owned guest pools.
// OUTPUT: One physical backing shared by original guest addresses and Vulkan.
// POS:    Experimental memory containment; real fences and per-submission ownership.
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include "../../linux/snapshot_store.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
static void pass_end_at(int);
static void async_drain_locked(void);
static struct { uint64_t begin_ns,end_ns,begin_calls,end_calls,begin_bytes,end_bytes; } shared_timing;
static void shared_sync(VPool *pool,uint64_t flags) {
    uint64_t started=prof_now();
    struct dma_buf_sync sync={.flags=flags};
    int result;
    do { result=ioctl(pool->v.external_fd_plus_one-1,DMA_BUF_IOCTL_SYNC,&sync); }
    while(result && (errno==EINTR || errno==EAGAIN));
    if(result)die("Linux shared pool DMA sync",errno);
    if(flags&DMA_BUF_SYNC_END) {
        shared_timing.end_ns+=prof_now()-started;shared_timing.end_calls++;shared_timing.end_bytes+=pool->size;
    } else {
        shared_timing.begin_ns+=prof_now()-started;shared_timing.begin_calls++;shared_timing.begin_bytes+=pool->size;
    }
}
static void shared_profile(uint64_t frame) {
    DmLinuxSnapshotStats snapshots;dm_linux_snapshot_stats(&snapshots);
    if(snapshots.limit_bytes)fprintf(stderr,"[nvn-vulkan] SNAPSHOT_RAM_PROFILE frame=%llu limit_bytes=%llu resident_bytes=%llu peak_bytes=%llu ram_stores=%llu file_stores=%llu entries=%llu\n",
        (unsigned long long)frame,(unsigned long long)snapshots.limit_bytes,(unsigned long long)snapshots.resident_bytes,
        (unsigned long long)snapshots.peak_bytes,(unsigned long long)snapshots.ram_stores,(unsigned long long)snapshots.file_stores,
        (unsigned long long)snapshots.resident_entries);
    if(!G.linux_shared_pools)return;
    fprintf(stderr,"[nvn-vulkan] LINUX_SHARED_PROFILE frame=%llu begin_ms=%.3f end_ms=%.3f begin_calls=%llu end_calls=%llu begin_bytes=%llu end_bytes=%llu synchronous=%d epoch_ownership=1\n",
        (unsigned long long)frame,shared_timing.begin_ns/1e6,shared_timing.end_ns/1e6,
        (unsigned long long)shared_timing.begin_calls,(unsigned long long)shared_timing.end_calls,
        (unsigned long long)shared_timing.begin_bytes,(unsigned long long)shared_timing.end_bytes,!G.async_enabled);
    fprintf(stderr,"[nvn-vulkan] SHARED_ASYNC_PROFILE frame=%llu available=%d enabled=%d submits=%llu presents=%llu completed_submits=%llu completed_presents=%llu\n",
        (unsigned long long)frame,G.linux_shared_async_available,G.async_enabled,
        (unsigned long long)G.profile_shared_async_submits,(unsigned long long)G.profile_shared_async_presents,
        (unsigned long long)G.profile_shared_async_completed_submits,(unsigned long long)G.profile_shared_async_completed_presents);
    G.profile_shared_async_submits=G.profile_shared_async_presents=0;
    G.profile_shared_async_completed_submits=G.profile_shared_async_completed_presents=0;
    memset(&shared_timing,0,sizeof shared_timing);
}
void dm_linux_shared_mapping_free(VBuffer *buffer) {
    if(buffer->map&&munmap(buffer->map,buffer->allocation_size))die("Linux shared pool CPU alias unmap",errno);
    close(buffer->external_fd_plus_one-1);
    buffer->map=NULL;buffer->external_fd_plus_one=0;
    /* The guest mapping remains valid CPU heap storage until its allocator
     * unmaps/reuses it. It independently retains the DMA-BUF backing. */
}
static VkBufferUsageFlags shared_pool_usage(void) {
    return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
}
static void shared_pool_create(VPool *pool) {
    extern int dm_guest_heap_readable(uint64_t,size_t);
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0||(uintptr_t)pool->guest%(unsigned long)page||pool->size%(unsigned long)page||
        !dm_guest_heap_readable((uint64_t)(uintptr_t)pool->guest,pool->size))die("Linux shared pool must be page-aligned owned guest heap",pool->size);
    /* Migration happens synchronously during MemoryPoolInitialize, before the
     * caller can publish the initialized pool. Never migrate a live lazy pool. */
    void dm_linux_memory_guard_allocation(uint64_t,uint64_t,unsigned);
    pthread_mutex_lock(&memory_mu);uint64_t live=memory_stats.total_bytes;pthread_mutex_unlock(&memory_mu);
    dm_linux_memory_guard_allocation(pool->size,live,DM_VK_MEMORY_POOL);
    VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=pool->size,
        .usage=shared_pool_usage(),.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VBuffer buffer={.size=pool->size,.usage=info.usage,.memory_class=DM_VK_MEMORY_POOL};
    CHECK(vkCreateBuffer(G.device,&info,NULL,&buffer.b));VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(G.device,buffer.b,&requirements);
    const char *selected=getenv("DM_LINUX_SHARED_POOL_HEAP");
    if(selected && strcmp(selected,"system") && strcmp(selected,"system-uncached"))
        die("Linux shared heap must be system or system-uncached",0);
    const char *heap_path=selected&&!strcmp(selected,"system")?"/dev/dma_heap/system":"/dev/dma_heap/system-uncached";
    int heap=open(heap_path,O_RDWR|O_CLOEXEC);if(heap<0)die("Linux DMA heap open",errno);
    struct dma_heap_allocation_data data={.len=requirements.size,.fd_flags=O_RDWR|O_CLOEXEC};
    int result=ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&data),error=errno;close(heap);if(result)die("Linux DMA heap allocation",error);
    int fd=(int)data.fd;
    PFN_vkGetDeviceProcAddr get_device=(PFN_vkGetDeviceProcAddr)dm_vk_get(G.instance,"vkGetDeviceProcAddr");
    PFN_vkGetMemoryFdPropertiesKHR properties=get_device?(PFN_vkGetMemoryFdPropertiesKHR)get_device(G.device,"vkGetMemoryFdPropertiesKHR"):NULL;
    if(!properties)die("Linux DMA import properties unavailable",0);
    VkMemoryFdPropertiesKHR fd_properties={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};CHECK(properties(G.device,external.handleTypes,fd,&fd_properties));
    uint32_t mask=requirements.memoryTypeBits&fd_properties.memoryTypeBits;
    uint32_t type=0;while(type<G.memory.memoryTypeCount&&!(mask&(1u<<type)))type++;
    if(type==G.memory.memoryTypeCount)die("Linux DMA import memory type",mask);
    int imported=dup(fd);if(imported<0)die("Linux DMA fd duplicate",errno);
    VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.buffer=buffer.b};
    VkImportMemoryFdInfoKHR import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.pNext=&dedicated,.handleType=external.handleTypes,.fd=imported};
    VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,.allocationSize=requirements.size,.memoryTypeIndex=type};
    buffer.m=memory_allocate(&allocation,DM_VK_MEMORY_POOL);buffer.allocation_size=requirements.size;
    CHECK(vkBindBufferMemory(G.device,buffer.b,buffer.m,0));
    buffer.map=mmap(NULL,requirements.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(buffer.map==MAP_FAILED)die("Linux shared pool CPU alias map",errno);
    buffer.external_fd_plus_one=fd+1;pool->v=buffer;
    shared_sync(pool,DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW);
    memcpy(buffer.map,pool->guest,pool->size);
    void *guest=mmap((void *)pool->guest,pool->size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0);
    if(guest!=pool->guest)die("Linux shared pool guest migration",errno);
    fprintf(stderr,"[nvn-vulkan] LINUX_SHARED_POOL bytes=%llu allocation=%llu type=%u flags=%#x address=%p heap=%s ownership_sync=1\n",
        (unsigned long long)pool->size,(unsigned long long)requirements.size,type,G.memory.memoryTypes[type].propertyFlags,(const void *)pool->guest,heap_path);
}
static void shared_pool_acquire(VPool *pool) {
    if(!pool->v.external_fd_plus_one)return;
    if(pool->shared.owned){if(pool->shared.epoch!=S.epoch)die("Linux shared pool recorder ownership",pool->serial);return;}
    /* The condition wait releases G.lock so actual fence completion and CPU
     * effects can publish. Never reuse a pending pool based on a software ticket. */
    if(pool->shared.pending)async_drain_locked();
    if(!G.active||S.phase!=SUBMISSION_RECORDING||!nvn_vk_shared_epoch_acquire(&pool->shared,S.epoch))die("Linux shared pool acquire state",pool->serial);
    pass_end_at(__LINE__);
    shared_sync(pool,DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW);
    VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL,.dstQueueFamilyIndex=G.family,.buffer=pool->v.b,.offset=0,.size=VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(S.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,1,&barrier,0,NULL);
}
static void shared_pools_release(void) {
    for(int i=0;i<G.npool;i++) {
        VPool *pool=&G.pools[i];if(!pool->shared.owned)continue;
        if(pool->shared.epoch!=S.epoch)die("Linux shared pool release ownership",pool->serial);
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
            .srcQueueFamilyIndex=G.family,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL,.buffer=pool->v.b,.offset=0,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(S.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,1,&barrier,0,NULL);
        if(!nvn_vk_shared_epoch_release(&pool->shared,S.epoch))die("Linux shared pool release state",pool->serial);
    }
}
static void shared_pools_cpu_begin(uint64_t epoch) {
    for(int i=0;i<G.npool;i++)if(nvn_vk_shared_epoch_pending(&G.pools[i].shared,epoch)) {
        shared_sync(&G.pools[i],DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW);
        if(!nvn_vk_shared_epoch_complete(&G.pools[i].shared,epoch))die("Linux shared pool completion ownership",G.pools[i].serial);
    }
}

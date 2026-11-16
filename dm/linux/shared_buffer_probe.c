// INPUT:  The actual Vulkan DMA-BUF export and coherent host memory support.
// OUTPUT: CPU-alias / GPU copy/fill evidence for a small shared buffer only.
// POS:    Zero-copy feasibility probe; never remaps guest addresses or starts a game.
#include <vulkan/vulkan.h>
#include "memory_guard.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#define CHECK(call) do {VkResult result=(call);if(result!=VK_SUCCESS){fprintf(stderr,"shared probe %s=%d\n",#call,result);exit(1);}}while(0)
static void sync_fd(int fd,uint64_t flags) {
    struct dma_buf_sync sync={.flags=flags};
    int result;do { result=ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync); } while(result && (errno==EINTR || errno==EAGAIN));
    if(result){perror("DMA_BUF_IOCTL_SYNC");exit(1);}
}
static uint64_t monotonic_ns(void) {
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now)){perror("clock_gettime");exit(1);}
    return (uint64_t)now.tv_sec*1000000000+(uint64_t)now.tv_nsec;
}
int main(int argc,char **argv) {
    int importing=argc==2;
    if(argc>2||(importing&&strncmp(argv[1],"/dev/dma_heap/",14)))return 2;
    dm_linux_memory_guard_init();
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="D2R shared-buffer probe",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;
    CHECK(vkCreateInstance(&ci,NULL,&instance));uint32_t count=0;CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));
    if(count!=1)return 2;
    VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&count,&gpu));
    CHECK(vkEnumerateDeviceExtensionProperties(gpu,NULL,&count,NULL));VkExtensionProperties *ext=calloc(count,sizeof *ext);
    if(!ext)return 1;
    CHECK(vkEnumerateDeviceExtensionProperties(gpu,NULL,&count,ext));unsigned found=0;
    for(unsigned i=0;i<count;i++){if(!strcmp(ext[i].extensionName,VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))found|=1;
        if(!strcmp(ext[i].extensionName,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME))found|=2;}
    free(ext);if(found!=3){puts("{\"passed\":false,\"reason\":\"required external memory extensions absent\"}");return 2;}
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    VkPhysicalDeviceExternalBufferInfo query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,.usage=usage,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkExternalBufferProperties properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
    vkGetPhysicalDeviceExternalBufferProperties(gpu,&query,&properties);
    if(!(properties.externalMemoryProperties.externalMemoryFeatures&(importing?VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT:VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT))) {
        printf("{\"passed\":false,\"reason\":\"DMA-BUF buffer export unavailable\",\"external_features\":%u}\n",properties.externalMemoryProperties.externalMemoryFeatures);return 2;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,NULL);VkQueueFamilyProperties *queues=calloc(count,sizeof *queues);
    if(!queues)return 1;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,queues);uint32_t family=0;
    while(family<count&&!(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;
    free(queues);if(family==count)return 2;
    float priority=1;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extensions[]={VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    VkDevice device;CHECK(vkCreateDevice(gpu,&device_info,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=query.handleType};
    VkBufferCreateInfo buffer_info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=65536,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;CHECK(vkCreateBuffer(device,&buffer_info,NULL,&buffer));VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);
    int fd=-1,import_fd=-1;uint32_t compatible=requirements.memoryTypeBits;
    if(importing) {
        int heap=open(argv[1],O_RDWR|O_CLOEXEC);
        if(heap<0){perror("DMA heap open");return 2;}
        struct dma_heap_allocation_data data={.len=requirements.size,.fd_flags=O_RDWR|O_CLOEXEC};
        int result=ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&data);close(heap);
        if(result){perror("DMA heap allocation");return 2;}
        fd=(int)data.fd;
        PFN_vkGetMemoryFdPropertiesKHR fd_properties=(PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(device,"vkGetMemoryFdPropertiesKHR");
        if(!fd_properties)return 2;
        VkMemoryFdPropertiesKHR fp={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
        CHECK(fd_properties(device,query.handleType,fd,&fp));compatible&=fp.memoryTypeBits;
        import_fd=dup(fd);if(import_fd<0)return 1;
    }
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(gpu,&memory);unsigned type=0;
    VkMemoryPropertyFlags wanted=importing?0:VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while(type<memory.memoryTypeCount&&(!(compatible&(1u<<type))||(memory.memoryTypes[type].propertyFlags&wanted)!=wanted))type++;
    if(type==memory.memoryTypeCount){puts("{\"passed\":false,\"reason\":\"coherent host memory unavailable\"}");return 2;}
    VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.buffer=buffer};
    VkExportMemoryAllocateInfo export_info={.sType=VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,.pNext=&dedicated,.handleTypes=query.handleType};
    VkImportMemoryFdInfoKHR import_info={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.pNext=&dedicated,.handleType=query.handleType,.fd=import_fd};
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=importing?(void *)&import_info:(void *)&export_info,.allocationSize=requirements.size,.memoryTypeIndex=type};
    VkDeviceMemory allocation;CHECK(vkAllocateMemory(device,&allocate,NULL,&allocation));CHECK(vkBindBufferMemory(device,buffer,allocation,0));
    int vulkan_mapping=(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)!=0;
    void *mapped=NULL;
    if(vulkan_mapping){CHECK(vkMapMemory(device,allocation,0,requirements.size,0,&mapped));}
    PFN_vkGetMemoryFdKHR get_fd=(PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(device,"vkGetMemoryFdKHR");if(!get_fd)return 2;
    VkMemoryGetFdInfoKHR fd_info={.sType=VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,.memory=allocation,.handleType=query.handleType};
    if(!importing){CHECK(get_fd(device,&fd_info,&fd));}
    void *alias=mmap(NULL,requirements.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(alias==MAP_FAILED){printf("{\"passed\":false,\"reason\":\"DMA-BUF direct CPU mmap unavailable\",\"errno\":%d}\n",errno);return 2;}
    if(!vulkan_mapping) {
        mapped=mmap(NULL,requirements.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if(mapped==MAP_FAILED){perror("second DMA mapping");return 2;}
    }
    sync_fd(fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW);
    uint32_t *words=alias,*vk_words=mapped;
    for(unsigned i=0;i<16384;i++)words[i]=i<4096?(0x13579bdfu^i):i>=12288?0xd00dc0deu:0;
    for(unsigned i=0;i<16384;i++)if(vk_words[i]!=words[i])return 1;
    sync_fd(fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW);
    VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool pool;
    CHECK(vkCreateCommandPool(device,&pool_info,NULL,&pool));VkCommandBufferAllocateInfo command_info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device,&command_info,&command));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(command,&begin));
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
    VkBufferMemoryBarrier ownership={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL,.dstQueueFamilyIndex=family,.buffer=buffer,.offset=0,.size=VK_WHOLE_SIZE};
    if(importing)vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&ownership,0,NULL);
    else vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
    VkBufferCopy copy={0,16384,16384};vkCmdCopyBuffer(command,buffer,buffer,1,&copy);vkCmdFillBuffer(command,buffer,32768,16384,0xa55a77ee);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    if(importing) {
        ownership.srcQueueFamilyIndex=family;ownership.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;
        ownership.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ownership.dstAccessMask=0;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,1,&ownership,0,NULL);
    }else vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fence_info,NULL,&fence));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,2000000000ull));
    sync_fd(fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ);
    for(unsigned i=0;i<16384;i++) {
        uint32_t expected=i<8192?(0x13579bdfu^(i%4096)):i<12288?0xa55a77eeu:0xd00dc0deu;
        if(words[i]!=expected||vk_words[i]!=expected){fprintf(stderr,"shared buffer mismatch word=%u\n",i);return 1;}
    }
    sync_fd(fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ);
    // Read actual mmap storage repeatedly, including cache-line separated
    // shader/descriptor-like reads. Volatile prevents constant folding.
    sync_fd(fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ);
    volatile const uint32_t *cpu_words=alias;uint64_t checksum=0,start=monotonic_ns();
    for(unsigned round=0;round<256;round++)for(unsigned i=0;i<16384;i+=16)checksum+=cpu_words[i];
    double read_ms=(monotonic_ns()-start)/1e6;
    sync_fd(fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ);
    uint64_t expected_sum=0;
    for(unsigned i=0;i<16384;i+=16)expected_sum+=i<8192?(0x13579bdfu^(i%4096)):i<12288?0xa55a77eeu:0xd00dc0deu;
    if(checksum!=256*expected_sum){fprintf(stderr,"shared CPU benchmark checksum mismatch\n");return 1;}
    printf("{\"heap\":\"%s\",\"cpu_cacheline_reads\":262144,\"cpu_read_ms\":%.3f,\"checksum\":%llu,\"checksum_verified\":true}\n",importing?argv[1]:"vulkan-export",read_ms,(unsigned long long)checksum);
    printf("{\"passed\":true,\"imported_dma_heap\":%s,\"vulkan_host_mapping\":%s,\"bytes_checked\":65536,\"cpu_alias_verified\":true,\"gpu_copy_and_fill_verified\":true,\"guest_memory_remapped\":false,\"game_frames\":0}\n",importing?"true":"false",vulkan_mapping?"true":"false");
    munmap(alias,requirements.size);close(fd);
    if(vulkan_mapping)vkUnmapMemory(device,allocation);else munmap(mapped,requirements.size);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);
    vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,allocation,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);return 0;
}

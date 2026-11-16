// INPUT:  Vulkan format, width, height, mip count and array-layer count.
// OUTPUT: Actual driver memory requirements across image flags and usage masks.
// POS:    Standalone diagnostic; creates unbound images and allocates no GPU memory.
#include <vulkan/vulkan.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void check(VkResult result,const char *operation) {
    if(result!=VK_SUCCESS){fprintf(stderr,"%s: VkResult %d\n",operation,result);exit(1);}
}
static uint32_t number(const char *text) {
    char *end=NULL;errno=0;unsigned long value=strtoul(text,&end,0);
    if(errno||!text[0]||*end||!value||value>UINT32_MAX){fprintf(stderr,"Invalid positive integer\n");exit(2);}
    return (uint32_t)value;
}
int main(int argc,char **argv) {
    if(argc!=6){fprintf(stderr,"Usage: image-allocation-probe VK_FORMAT WIDTH HEIGHT LEVELS LAYERS\n");return 2;}
    VkFormat format=(VkFormat)number(argv[1]);uint32_t width=number(argv[2]),height=number(argv[3]);
    uint32_t levels=number(argv[4]),layers=number(argv[5]);
    if(width>16384||height>16384||levels>15||layers>2048){fprintf(stderr,"Probe extent limit exceeded\n");return 2;}
    uint32_t maximum=width>height?width:height,max_levels=1;
    while(maximum>1){maximum>>=1;max_levels++;}
    if(levels>max_levels){fprintf(stderr,"Mip count exceeds the image extent\n");return 2;}
    /* Proprietary drivers may print version lines to stdout. Keep the CSV on
     * its original descriptor and route all driver stdout to stderr. */
    int output_fd=dup(STDOUT_FILENO);
    if(output_fd<0||dup2(STDERR_FILENO,STDOUT_FILENO)<0){perror("CSV descriptor");return 1;}
    FILE *output=fdopen(output_fd,"w");if(!output){perror("CSV output");return 1;}
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="d2r-image-allocation-probe",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance;check(vkCreateInstance(&instance_info,NULL,&instance),"vkCreateInstance");
    uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,NULL),"vkEnumeratePhysicalDevices");
    if(!count||count>16){fprintf(stderr,"Expected 1..16 physical devices\n");return 1;}
    VkPhysicalDevice physical[16];check(vkEnumeratePhysicalDevices(instance,&count,physical),"vkEnumeratePhysicalDevices");
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical[0],&properties);
    fprintf(stderr,"device=%s driver=%u allocations=0 submissions=0\n",properties.deviceName,properties.driverVersion);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&families,NULL);
    if(!families||families>64){fprintf(stderr,"Invalid queue family count\n");return 1;}
    VkQueueFamilyProperties queues[64];vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&families,queues);
    uint32_t family=0;while(family<families&&!(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;
    if(family==families){fprintf(stderr,"No graphics queue\n");return 1;}
    float priority=1;VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceFeatures available,enabled={0};vkGetPhysicalDeviceFeatures(physical[0],&available);
    enabled.textureCompressionASTC_LDR=available.textureCompressionASTC_LDR;
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue,.pEnabledFeatures=&enabled};
    VkDevice device;check(vkCreateDevice(physical[0],&device_info,NULL,&device),"vkCreateDevice");
    const VkImageUsageFlags usages[]={7,23,15,31};
    fprintf(output,"format,width,height,levels,layers,flags,usage,status,allocation_bytes,alignment,memory_types\n");
    for(unsigned cube=0;cube<2;cube++)for(unsigned i=0;i<sizeof usages/sizeof usages[0];i++){
        if(cube&&(width!=height||layers<6))continue;
        VkImageCreateFlags flags=cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0;
        VkImageFormatProperties supported;
        VkResult result=vkGetPhysicalDeviceImageFormatProperties(physical[0],format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usages[i],flags,&supported);
        VkMemoryRequirements requirements={0};
        if(result==VK_SUCCESS&&(width>supported.maxExtent.width||height>supported.maxExtent.height||levels>supported.maxMipLevels||layers>supported.maxArrayLayers))result=VK_ERROR_FORMAT_NOT_SUPPORTED;
        if(result==VK_SUCCESS){
            VkImageCreateInfo info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=flags,.imageType=VK_IMAGE_TYPE_2D,.format=format,
                .extent={width,height,1},.mipLevels=levels,.arrayLayers=layers,.samples=VK_SAMPLE_COUNT_1_BIT,
                .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usages[i],.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
            VkImage image;result=vkCreateImage(device,&info,NULL,&image);
            if(result==VK_SUCCESS){vkGetImageMemoryRequirements(device,image,&requirements);vkDestroyImage(device,image,NULL);}
        }
        fprintf(output,"%u,%u,%u,%u,%u,%u,%u,%d,%llu,%llu,%u\n",format,width,height,levels,layers,flags,usages[i],result,
            (unsigned long long)requirements.size,(unsigned long long)requirements.alignment,requirements.memoryTypeBits);
    }
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    if(fclose(output)){perror("CSV write");return 1;}return 0;
}

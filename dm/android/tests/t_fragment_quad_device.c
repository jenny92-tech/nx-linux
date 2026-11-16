/* GPU readback for the actual production quad emitter. No game/APK needed.
 * Includes NaN/Inf/signed-zero payloads, reversed viewport and odd scissors. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define VK_OK(call) do {VkResult result=(call);if(result!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#call,result);return 3;}}while(0)
static uint32_t *read_spv(const char *path,size_t *words) {
    FILE *file=fopen(path,"rb");if(!file)return NULL;
    if(fseek(file,0,SEEK_END)){fclose(file);return NULL;}
    long bytes=ftell(file);rewind(file);
    if(bytes<20||bytes%4){fclose(file);return NULL;}
    uint32_t *code=malloc((size_t)bytes);if(!code){fclose(file);return NULL;}
    if(fread(code,1,(size_t)bytes,file)!=(size_t)bytes){free(code);fclose(file);return NULL;}
    fclose(file);*words=(size_t)bytes/4;return code;
}
static uint32_t memory_type(VkPhysicalDevice gpu,uint32_t bits) {
    VkPhysicalDeviceMemoryProperties properties;vkGetPhysicalDeviceMemoryProperties(gpu,&properties);
    for(uint32_t i=0;i<properties.memoryTypeCount;i++)if((bits&(1u<<i))&&
        (properties.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))return i;
    return UINT32_MAX;
}
int main(int argc,char **argv) {
    int driver_push=argc==4&&!strcmp(argv[3],"driver-push");
    if(argc!=3&&!driver_push)return 2;
    size_t words=0;uint32_t *code=read_spv(argv[1],&words);if(!code)return 2;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Exact fragment quad bit exchange",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;VK_OK(vkCreateInstance(&ii,NULL,&instance));
    uint32_t count=1;VkPhysicalDevice gpu;VK_OK(vkEnumeratePhysicalDevices(instance,&count,&gpu));if(count!=1)return 5;
    VkPhysicalDeviceFeatures features;vkGetPhysicalDeviceFeatures(gpu,&features);
    if(!features.fragmentStoresAndAtomics){fputs("fragmentStoresAndAtomics unavailable\n",stderr);return 6;}
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(gpu,&properties);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&families,NULL);
    VkQueueFamilyProperties *qprops=calloc(families,sizeof *qprops);if(!qprops)return 6;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&families,qprops);uint32_t family=UINT32_MAX;
    for(uint32_t i=0;i<families;i++)if(qprops[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=i;break;}
    free(qprops);if(family==UINT32_MAX)return 6;
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    /* No subgroup features or subgroup extensions are enabled. */
    VkPhysicalDeviceFeatures enabled={.fragmentStoresAndAtomics=VK_TRUE};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&enabled};
    VkDevice device;VK_OK(vkCreateDevice(gpu,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=65536,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};VkBuffer buffer;VK_OK(vkCreateBuffer(device,&bi,NULL,&buffer));
    VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);uint32_t mt=memory_type(gpu,requirements.memoryTypeBits);if(mt==UINT32_MAX)return 6;
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=mt};VkDeviceMemory memory;VK_OK(vkAllocateMemory(device,&ma,NULL,&memory));VK_OK(vkBindBufferMemory(device,buffer,memory,0));
    void *mapped;VK_OK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped));memset(mapped,0xcd,1024);
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout layout;VK_OK(vkCreateDescriptorSetLayout(device,&li,NULL,&layout));
    VkDescriptorSetLayout layouts[2]={layout,VK_NULL_HANDLE};
    if(driver_push){binding.binding=3;binding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;VK_OK(vkCreateDescriptorSetLayout(device,&li,NULL,&layouts[1]));}
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
    VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=driver_push?2:1,.pPoolSizes=sizes};VkDescriptorPool pool;VK_OK(vkCreateDescriptorPool(device,&dpi,NULL,&pool));
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=driver_push?2:1,.pSetLayouts=layouts};VkDescriptorSet sets[2];VK_OK(vkAllocateDescriptorSets(device,&dai,sets));
    VkDescriptorBufferInfo db={buffer,0,1024},ub={buffer,0,65536};
    VkWriteDescriptorSet writes[]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[0],.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&db},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=driver_push?sets[1]:VK_NULL_HANDLE,.dstBinding=3,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&ub}};
    vkUpdateDescriptorSets(device,driver_push?2:1,writes,0,NULL);
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,64,64};
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=driver_push?2:1,.pSetLayouts=layouts,.pushConstantRangeCount=driver_push?1:0,.pPushConstantRanges=&push};VkPipelineLayout pipeline_layout;VK_OK(vkCreatePipelineLayout(device,&pli,NULL,&pipeline_layout));
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&subpass};VkRenderPass render_pass;VK_OK(vkCreateRenderPass(device,&ri,NULL,&render_pass));
    VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=render_pass,.width=8,.height=8,.layers=1};VkFramebuffer framebuffer;VK_OK(vkCreateFramebuffer(device,&fi,NULL,&framebuffer));
    VkShaderModuleCreateInfo si={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=words*4,.pCode=code};VkShaderModule vertex;VK_OK(vkCreateShaderModule(device,&si,NULL,&vertex));free(code);
    code=read_spv(argv[2],&words);if(!code)return 2;
    si.codeSize=words*4;si.pCode=code;VkShaderModule fragment;VK_OK(vkCreateShaderModule(device,&si,NULL,&fragment));free(code);
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.depthClampEnable=VK_FALSE,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};
    VkViewport viewport={0,0,8,8,0,1};VkRect2D scissor={{0,0},{8,8}};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo gi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,.pColorBlendState=&cb,.pDynamicState=&dynamic,.layout=pipeline_layout,.renderPass=render_pass};
    VkPipeline pipeline;VK_OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gi,NULL,&pipeline));
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};VkCommandPool command_pool;VK_OK(vkCreateCommandPool(device,&cpi,NULL,&command_pool));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;VK_OK(vkAllocateCommandBuffers(device,&cai,&command));
    const uint32_t cases[]={0,0x80000000,0x7f800000,0xff800000,0x7fc01234,0x7f801234,0xffffffff,0x00000001,
        0xffff0000,0x0000ffff,0x80008000,0x7fff7fff,0x1234abcd,0xabcd1234,0x3f800000,0xbf800000};
    uint32_t input[64];for(unsigned i=0;i<64;i++)input[i]=i<16?cases[i]:0x9e3779b9u*(i+1);
    unsigned checked=0;
    unsigned passes=driver_push?16:4;
    for(unsigned pass=0;pass<passes;pass++){
    memset(mapped,0xcd,1024);memcpy(mapped,input,sizeof input);
    if(pass)VK_OK(vkResetCommandBuffer(command,0));
    viewport=(VkViewport){0,(pass&1)?8:0,8,(pass&1)?-8:8,0,1};
    scissor=(pass&2)?(VkRect2D){{1,1},{6,6}}:(VkRect2D){{0,0},{8,8}};
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_OK(vkBeginCommandBuffer(command,&begin));
    VkRenderPassBeginInfo render={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=render_pass,.framebuffer=framebuffer,.renderArea={{0,0},{8,8}}};vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,driver_push?2:1,sets,0,NULL);
    if(driver_push)vkCmdPushConstants(command,pipeline_layout,VK_SHADER_STAGE_FRAGMENT_BIT,64,4,cases+pass);
    vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    VK_OK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};VK_OK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));VK_OK(vkQueueWaitIdle(queue));
    const uint32_t *actual=mapped;
    for(unsigned i=0;i<256;i++){
        uint32_t want=0xcdcdcdcdu;
        if(i<64)want=input[i];
        if(i>=128){unsigned pixel=(i-128)%64,x=pixel%8,y=pixel/8,axis=i<192?1:0;
            int covered=!(pass&2)||(x>=1&&x<7&&y>=1&&y<7);
            /* Subgroup shuffle from an inactive helper has an undefined result.
             * Check clipped destinations as guards, and covered source pairs
             * exactly; do not invent a requirement for the undefined edges. */
            unsigned neighbor=axis?(y^1):(x^1);
            if(driver_push){if(covered&&i<192)want=cases[pass];}
            else{
                if(covered&&(pass&2)&&(neighbor<1||neighbor>=7))continue;
                if(covered)want=input[axis?x+8*(y^1):(x^1)+8*y];
            }
        }
        if(actual[i]!=want){fprintf(stderr,"quad pass=%u slot=%u got=%08x expected=%08x\n",pass,i,actual[i],want);return 7;}
        checked++;
    }
    }
    printf("{\"passed\":true,\"gpu\":\"%s\",\"passes\":%u,\"checked_words\":%u,\"bit_exact\":true,\"driver_push\":%s,\"negative_viewport\":true,\"odd_scissor\":true,\"nan_inf_signed_zero\":true}\n",properties.deviceName,passes,checked,driver_push?"true":"false");
    vkDestroyCommandPool(device,command_pool,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,fragment,NULL);vkDestroyShaderModule(device,vertex,NULL);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,render_pass,NULL);vkDestroyPipelineLayout(device,pipeline_layout,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyDescriptorSetLayout(device,layout,NULL);if(driver_push)vkDestroyDescriptorSetLayout(device,layouts[1],NULL);vkUnmapMemory(device,memory);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,memory,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);return 0;
}

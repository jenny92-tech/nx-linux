/* INPUT: The actual old/new Vulkan image layout, without assumed frame order.
 * OUTPUT: Complete stage/access scopes for layouts with restricted uses.
 * POS: Optional layout barrier narrowing; GENERAL/feedback/unknown stay broad. */
#ifndef NVN_VK_LAYOUT_SCOPE_H
#define NVN_VK_LAYOUT_SCOPE_H
typedef struct {
    VkPipelineStageFlags stages;
    VkAccessFlags access;
    int restricted;
} NvnVkLayoutScope;
static inline NvnVkLayoutScope nvn_vk_layout_scope(VkImageLayout layout) {
    const VkPipelineStageFlags shaders=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    const VkPipelineStageFlags depth=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    switch(layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return (NvnVkLayoutScope){VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,0,1};
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return (NvnVkLayoutScope){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,1};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return (NvnVkLayoutScope){depth,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,1};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        /* Input attachments and sampled images; all supported shader stages
         * remain covered, including a later compute or vertex use. */
        return (NvnVkLayoutScope){shaders,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,1};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return (NvnVkLayoutScope){shaders|depth,VK_ACCESS_SHADER_READ_BIT|
            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,1};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return (NvnVkLayoutScope){VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,1};
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return (NvnVkLayoutScope){VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1};
    default:
        return (NvnVkLayoutScope){VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0};
    }
}
#endif

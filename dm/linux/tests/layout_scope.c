/* INPUT: Restricted and unrestricted Vulkan image layouts.
 * OUTPUT: Coverage checks for color/depth, WAR readers and transfer hazards.
 * POS: Host-only regression; no device, game files or Vulkan loader needed. */
#include <vulkan/vulkan.h>
#include "nvn_vk_layout_scope.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    NvnVkLayoutScope s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    assert(s.restricted&&s.stages==VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    assert(s.access&VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    assert(s.restricted&&(s.stages&VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT));
    assert(s.stages&VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    assert(s.access&VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    assert((s.stages&(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))==(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
    assert(s.access&VK_ACCESS_SHADER_READ_BIT);assert(s.access&VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);
    s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    assert(s.access&VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    assert(s.stages&VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    assert(s.stages==VK_PIPELINE_STAGE_TRANSFER_BIT&&s.access==VK_ACCESS_TRANSFER_WRITE_BIT);
    s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    assert(s.stages==VK_PIPELINE_STAGE_TRANSFER_BIT&&s.access==VK_ACCESS_TRANSFER_READ_BIT);
    s=nvn_vk_layout_scope(VK_IMAGE_LAYOUT_UNDEFINED);
    assert(s.restricted&&!s.access&&s.stages==VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    VkImageLayout broad[]={VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT,(VkImageLayout)0x7ffffffe};
    for(unsigned i=0;i<sizeof broad/sizeof *broad;i++){
        s=nvn_vk_layout_scope(broad[i]);assert(!s.restricted);
        assert(s.stages==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        assert(s.access==(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT));
    }
    puts("layout scope coverage: passed");return 0;
}

/* Image usages influence tile/compression choices on mobile drivers. Images
 * without the guest IMAGE hint can start without STORAGE; the first actual
 * storage binding upgrades them before either shader stage gets descriptors. */
/* Some Mali layouts reserve extra space for small arrays or wide UI textures
 * without STORAGE. Query both legal layouts before opting in. Added usage is a
 * capability only: no shader/storage write or synchronization is removed. */
static VkImageUsageFlags texture_compact_usage(const VTexture *t,VkImageUsageFlags usage) {
#ifdef DM_LINUX_HOST
    const tex_t *source=&t->source;
    uint32_t decoded_format;int decoded_bpp;
    int array=G.linux_compact_array_images&&source->d.target==NVN_TEXTURE_TARGET_2D_ARRAY&&
        source->L.layers>=2&&source->d.levels>=2&&t->width<=256&&t->height<=256&&
        nvn_bc_unpacked(source->d.format,&decoded_format,&decoded_bpp);
    const char *ui=getenv("DM_LINUX_COMPACT_2D_IMAGES");
    int wide_ui=ui&&!strcmp(ui,"1")&&source->d.target==NVN_TEXTURE_TARGET_2D&&
        !source->d.flags&&source->L.layers==1&&source->d.levels==1&&
        t->width>=4096&&t->height<=256&&t->format==VK_FORMAT_R8G8B8A8_UNORM;
    if((!array&&!wide_ui)||G.properties.vendorID!=0x13b5||t->depth!=1||(usage&VK_IMAGE_USAGE_STORAGE_BIT)||
       !(format_properties(t->format).optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))return usage;
    VkImageCreateInfo info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=t->format,
        .flags=(t->width==t->height&&source->L.layers>=6&&t->width<=G.properties.limits.maxImageDimensionCube)?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,
        .extent={t->width,t->height,1},.mipLevels=source->d.levels,.arrayLayers=source->L.layers,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkMemoryRequirements requirements[2]={0};
    for(unsigned i=0;i<2;i++){
        info.usage=usage|(i?VK_IMAGE_USAGE_STORAGE_BIT:0);
        VkImageFormatProperties properties;
        if(vkGetPhysicalDeviceImageFormatProperties(G.physical,info.format,info.imageType,info.tiling,info.usage,info.flags,&properties)!=VK_SUCCESS||
           info.extent.width>properties.maxExtent.width||info.extent.height>properties.maxExtent.height||
           info.mipLevels>properties.maxMipLevels||info.arrayLayers>properties.maxArrayLayers)return usage;
        VkImage image;
        if(vkCreateImage(G.device,&info,NULL,&image)!=VK_SUCCESS)return usage;
        vkGetImageMemoryRequirements(G.device,image,&requirements[i]);vkDestroyImage(G.device,image,NULL);
    }
    /* Keep the original allocation unless a substantial measured reduction
     * exists. There is no memory allocation or in-flight object in this probe. */
    if(requirements[0].size<1024*1024||!requirements[1].size||
       requirements[1].size>requirements[0].size-requirements[0].size/4)return usage;
    fprintf(stderr,"[nvn-vulkan] %s serial=%llu source_format=%u vk_format=%u width=%u height=%u levels=%u layers=%u original_requirement=%llu compact_requirement=%llu usage_before=%u usage_after=%u content_unchanged=1\n",
        wide_ui?"IMAGE_COMPACT_UI":"IMAGE_COMPACT_ARRAY",(unsigned long long)t->serial,source->d.format,t->format,t->width,t->height,source->d.levels,source->L.layers,
        (unsigned long long)requirements[0].size,(unsigned long long)requirements[1].size,usage,usage|VK_IMAGE_USAGE_STORAGE_BIT);
    return usage|VK_IMAGE_USAGE_STORAGE_BIT;
#else
    (void)t;
#endif
    return usage;
}
static void texture_image_allocate(VTexture *t,VkImageUsageFlags usage,
    VkImage *image,VkDeviceMemory *memory,VkImageView *view,uint64_t *allocation_size) {
    const tex_t *source=&t->source;
    int one=source->d.target==NVN_TEXTURE_TARGET_1D||source->d.target==NVN_TEXTURE_TARGET_1D_ARRAY;
    int native_one=one&&!G.one_d_as_two;
    int layered=source->d.target==NVN_TEXTURE_TARGET_1D_ARRAY||source->d.target==NVN_TEXTURE_TARGET_2D_ARRAY;
    int volume=source->d.target==NVN_TEXTURE_TARGET_3D;
    int cube=source->d.target==NVN_TEXTURE_TARGET_CUBEMAP||source->d.target==NVN_TEXTURE_TARGET_CUBEMAP_ARRAY;
    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=volume?VK_IMAGE_TYPE_3D:native_one?VK_IMAGE_TYPE_1D:VK_IMAGE_TYPE_2D,
        .format=t->format,.flags=(cube||(layered&&!one&&t->width==t->height&&source->L.layers>=6&&t->width<=G.properties.limits.maxImageDimensionCube))?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,
        .extent={t->width,t->height,t->depth},.mipLevels=source->d.levels,.arrayLayers=source->L.layers,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkImageViewType view_type=volume?VK_IMAGE_VIEW_TYPE_3D:native_one?(layered?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D):
        cube?(source->L.layers==6?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_CUBE_ARRAY):layered?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
    t->allocation_key=(NvnTextureAllocationKey){ii.flags,ii.imageType,ii.format,ii.extent.width,ii.extent.height,ii.extent.depth,
        ii.mipLevels,ii.arrayLayers,ii.samples,ii.tiling,ii.usage,ii.sharingMode,view_type};
    /* Reuse a complete, fence-retired, exact image allocation. Its first
     * transition uses UNDEFINED, discarding prior contents just like new. */
    if(texture_allocation_take(&t->allocation_key,image,memory,view,allocation_size))return;
    CHECK(vkCreateImage(G.device,&ii,NULL,image));VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(G.device,*image,&requirements);
    VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,
        .memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    texture_allocation_pressure(requirements.size);
    *allocation_size=requirements.size;*memory=memory_allocate(&allocation,DM_VK_MEMORY_TEXTURE);CHECK(vkBindImageMemory(G.device,*image,*memory,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=*image,.viewType=view_type,.format=t->format,
        .subresourceRange={aspect(t->format)&VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,
            0,source->d.levels,0,source->L.layers}};
    CHECK(vkCreateImageView(G.device,&vi,NULL,view));
    if(one&&G.one_d_as_two)fprintf(stderr,"[nvn-vulkan] IMAGE_1D_AS_2D serial=%llu extent=%u,%u layers=%u levels=%u shader_contract=Sample1DAs2D\n",
        (unsigned long long)t->serial,t->width,t->height,source->L.layers,source->d.levels);
}
static void texture_require_storage(VTexture *t) {
    if(t->usage&VK_IMAGE_USAGE_STORAGE_BIT)return;
    VkFormatProperties properties=format_properties(t->format);
    if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)||aspect(t->format)!=VK_IMAGE_ASPECT_COLOR_BIT)
        die("storage image format unsupported",t->format);
    if(t->before_storage)die("repeated image storage promotion",t->serial);
    VTexture upgraded=*t;upgraded.usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    texture_image_allocate(&upgraded,upgraded.usage,&upgraded.image,&upgraded.memory,&upgraded.view,&upgraded.allocation_size);
    upgraded.layout=VK_IMAGE_LAYOUT_UNDEFINED;upgraded.mixed=0;memset(upgraded.lvl,0,sizeof upgraded.lvl);
    all_barrier();texture_transition(t,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    texture_transition(&upgraded,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    for(unsigned level=0;level<t->source.d.levels;level++){
        unsigned w=t->width>>level,h=t->height>>level,d=t->depth>>level;if(!w)w=1;if(!h)h=1;if(!d)d=1;
        VkImageCopy copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,t->source.L.layers},
            .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,t->source.L.layers},.extent={w,h,d}};
        vkCmdCopyImage(S.command,t->image,t->layout,upgraded.image,upgraded.layout,1,&copy);
    }
    texture_transition(&upgraded,VK_IMAGE_LAYOUT_GENERAL);
    /* Old views may occur in already-recorded descriptors and framebuffers.
     * Keep their allocation with this texture. Finalize transfers both versions
     * into the same real-fence retirement and completed-slot invalidation.
     * Promotion is one-way, so this is bounded to one old image per texture. */
    t->before_storage_allocation_size=t->allocation_size;t->allocation_size=upgraded.allocation_size;
    t->before_storage=t->image;t->before_storage_memory=t->memory;t->before_storage_view=t->view;
    t->image=upgraded.image;t->memory=upgraded.memory;t->view=upgraded.view;t->usage=upgraded.usage;t->allocation_key=upgraded.allocation_key;
    t->layout=upgraded.layout;t->mixed=upgraded.mixed;memcpy(t->lvl,upgraded.lvl,sizeof t->lvl);
    t->incarnation=++G.texture_incarnation;G.registry_revision++;G.image_promotions++;G.image_retired_live++;
    fprintf(stderr,"[nvn-vulkan] IMAGE_STORAGE_PROMOTION serial=%llu flags=%#x usage=%#x extent=%u,%u,%u levels=%u layers=%u retained_old_images=%llu\n",
        (unsigned long long)t->serial,t->source.d.flags,t->usage,t->width,t->height,t->depth,t->source.d.levels,t->source.L.layers,
        (unsigned long long)G.image_retired_live);
}
#ifdef DM_VK_IMAGE_USAGE_TEST
void dm_vk_image_usage_stats(uint64_t out[5]) {
    pthread_mutex_lock(&G.lock);out[0]=G.image_usage_omitted;out[1]=G.image_usage_storage;
    out[2]=G.image_promotions;out[3]=G.image_retired_live;out[4]=G.image_usage_by_flags;
    pthread_mutex_unlock(&G.lock);
}
#endif

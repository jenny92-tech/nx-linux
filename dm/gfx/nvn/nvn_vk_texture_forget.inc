/* Only completed slots can forget objects. Other slots keep their own views
 * until their original real fence/publication, without stalling the guest. */
static void resource_cache_forget_texture(VkSubmission *ctx,const VTexture *t) {
    VkImage images[]={t->image,t->before_storage,t->sample_copy,t->shadow};
    VkImageView views[]={t->view,t->before_storage_view,t->sample_view,t->shadow_view};
    for(int i=0;i<ctx->nview;i++)for(unsigned k=0;k<4;k++)if(images[k]&&ctx->view_keys[i].image==images[k]){resource_cache_clear(ctx);return;}
    for(int i=0;i<ctx->npass;i++)for(unsigned j=0;j<9;j++)for(unsigned k=0;k<4;k++)
        if(views[k]&&ctx->framebuffer_keys[i].views[j]==views[k]){resource_cache_clear(ctx);return;}
}

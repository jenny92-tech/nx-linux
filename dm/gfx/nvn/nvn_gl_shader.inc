// INPUT: Verified original shader identities, GLSL ES and NVN binding metadata.
// OUTPUT: Cached shader stages, linked graphics pairs and qualified bindings.
// POS: Shader and binding half of the optional GLES verification backend.
static uint32_t gl_u32(const void *p){uint32_t x;memcpy(&x,p,4);return x;}
static uint8_t *gl_index_mapping(size_t *size){
    char path[1200];if(snprintf(path,sizeof path,"%s/shader_index.bin",L.shader_root)>=(int)sizeof path)gl_stop("shader index path",0);
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)gl_stop("shader index open",errno);
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<12||st.st_size>256L*1024*1024)gl_stop("shader index file",0);
    void *p=mmap(NULL,(size_t)st.st_size,PROT_READ,MAP_PRIVATE,fd,0);int error=errno;close(fd);
    if(p==MAP_FAILED)gl_stop("shader index mapping",error);
    /* Entries retain pointers for process lifetime. The verified bank must
     * remain immutable until exit, as for Linux's Vulkan index mapping. */
    *size=(size_t)st.st_size;return p;
}
static void gl_index_load(void){
    if(L.index)return;L.index_bytes=gl_index_mapping(&L.index_size);
    if(L.index_size<12||gl_u32(L.index_bytes)!=0x494b5644||gl_u32(L.index_bytes+4)!=2)gl_stop("full-code shader index v2 required",0);
    L.nindex=gl_u32(L.index_bytes+8);if(!L.nindex||L.nindex>1000000)gl_stop("shader index count",L.nindex);
    L.index=calloc(L.nindex,sizeof *L.index);if(!L.index)gl_stop("shader index allocation",L.nindex);size_t at=12;
    for(unsigned i=0;i<L.nindex;i++){
        nvn_shader_index_record r;if(!nvn_shader_index_read(L.index_bytes,L.index_size,2,&at,&r))gl_stop("shader index entry",i);
        GlIndex *e=&L.index[i];memcpy(e->key,r.entry,16);e->stage=r.stage;e->size=r.code_size;e->control_bytes=r.control_bytes;
        e->control=r.control;e->code=r.code;e->hash=bytes_hash(e->control,e->control_bytes,14695981039346656037ull^e->stage);e->hash=bytes_hash(e->code,e->size,e->hash);
        unsigned bucket=e->hash&16383;e->next=L.index_hash[bucket];L.index_hash[bucket]=i+1;
    }
    if(at!=L.index_size)gl_stop("shader index trailing bytes",at);
    /* The full verification scan should not pin the entire index in RSS.
     * Exact identity comparisons fault in only their selected file pages. */
    if(madvise(L.index_bytes,L.index_size,MADV_DONTNEED))gl_stop("shader index page release",errno);
    fprintf(stderr,"[nvn-gles] SHADER_INDEX entries=%u full_code_verified=1 file_backed_bytes=%zu scan_pages_released=1\n",L.nindex,L.index_size);
}
static const char *gl_image_qualifier(GLenum internal){
#define Q(n,s) case GL_##n:return s
    switch(internal){
    Q(R8,"r8");Q(RG8,"rg8");Q(RGBA8,"rgba8");Q(RGB10_A2,"rgb10_a2");
    Q(R16F,"r16f");Q(RG16F,"rg16f");Q(RGBA16F,"rgba16f");Q(R32F,"r32f");Q(RG32F,"rg32f");Q(RGBA32F,"rgba32f");Q(R11F_G11F_B10F,"r11f_g11f_b10f");
    Q(R8UI,"r8ui");Q(RG8UI,"rg8ui");Q(RGBA8UI,"rgba8ui");Q(R32UI,"r32ui");Q(RG32UI,"rg32ui");Q(RGBA32UI,"rgba32ui");
    Q(R32I,"r32i");Q(RG32I,"rg32i");Q(RGBA32I,"rgba32i");default:return NULL;
    }
#undef Q
}
static char *gl_image_shader_source(GlShader *s,char *code){
    for(unsigned i=0;i<s->h.nb;i++)if(s->b[i].kind==15){
        const GlBinding *b=&s->b[i];unsigned stage=s->h.stage;
        if(b->slot<0||b->slot>=32)gl_stop("runtime image metadata",i);
        unsigned id=L.state.img[stage][b->slot]&0xfffff;
        if(id>=L.nslot||!L.slots[id].live){s->bad=1;gl_filter("runtime_storage_image_unbound",id);return code;}
        const tex_t *source=&L.slots[id].source;unsigned dim=(unsigned)b->flags>>4&15,array=(unsigned)b->flags>>8&1,ms=(unsigned)b->flags>>9&1;
        int expected=dim==1?(array?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D):dim==2&&!array?NVN_TEXTURE_TARGET_3D:-1;
        if(ms||expected<0||source->d.target!=expected){s->bad=1;gl_filter("runtime_storage_image_target",source->d.target);return code;}
        GLenum internal,format,type;unsigned bpp;const char *qualifier=NULL;
        if(gl_format(source->d.format,&internal,&format,&type,&bpp))qualifier=gl_image_qualifier(internal);
        int scalar=b->flags&15,integer=qualifier&&(format==GL_RED_INTEGER||format==GL_RG_INTEGER||format==GL_RGB_INTEGER||format==GL_RGBA_INTEGER);
        if(!qualifier||scalar>2||(scalar==0&&integer)||(scalar!=0&&!integer)||(scalar==1&&type!=GL_INT)||(scalar==2&&type!=GL_UNSIGNED_INT&&type!=GL_UNSIGNED_BYTE)){
            s->bad=1;gl_filter("runtime_storage_image_format",source->d.format);return code;
        }
        const char *placeholder=scalar==0?"rgba32f":scalar==1?"rgba32i":"rgba32ui";char before[96],after[96];
        snprintf(before,sizeof before,"layout(binding = %d, %s)",b->unit,placeholder);snprintf(after,sizeof after,"layout(binding = %d, %s)",b->unit,qualifier);
        char *found=strstr(code,before);if(!found){s->bad=1;gl_filter("runtime_storage_image_declaration",b->unit);return code;}
        size_t prefix=found-code,old=strlen(before),replacement=strlen(after),total=strlen(code);char *next=malloc(total-old+replacement+1);if(!next)gl_stop("runtime image shader allocation",total);
        memcpy(next,code,prefix);memcpy(next+prefix,after,replacement);memcpy(next+prefix+replacement,found+old,total-prefix-old+1);free(code);code=next;s->image_formats[i]=internal;
        fprintf(stderr,"[nvn-gles] IMAGE_FORMAT_SPECIALIZED key=%s binding=%d nvn_format=%u qualifier=%s\n",s->key,b->unit,source->d.format,qualifier);
    }
    return code;
}
static GlShader *gl_shader_load(const char *key){
    for(unsigned i=0;i<L.nshader;i++)if(!strcmp(L.shaders[i].key,key))return &L.shaders[i];
    if(L.nshader==GL_OBJECTS)gl_stop("shader count",L.nshader);GlShader *s=&L.shaders[L.nshader++];strcpy(s->key,key);s->loaded=1;
    char name[64];snprintf(name,sizeof name,"%s.glmeta",key);size_t size;uint8_t *meta=gl_file(name,&size,1);
    if(!meta){s->bad=1;gl_filter("offline_unsupported_shader",L.nshader);fprintf(stderr,"[nvn-gles] SHADER_UNSUPPORTED key=%s\n",key);return s;}
    if(size<sizeof s->h)gl_stop("GL metadata header",size);memcpy(&s->h,meta,sizeof s->h);
    if(s->h.magic!=0x4d4c4744||(s->h.version!=7&&s->h.version!=8)||s->h.nb>GL_BINDINGS||s->h.na>32||s->h.ncap>256||s->h.c1n>65536||s->h.reserved>>(s->h.version==8?16:15)||size!=48+s->h.nb*32+s->h.na*8+s->h.ncap*4+s->h.c1n)gl_stop("GL metadata schema",size);
    if(s->h.version==8&&(s->h.reserved&(1u<<15))){gl_filter("prepared_low_quality_variant",s->h.stage);fprintf(stderr,"[nvn-gles] LOW_QUALITY_SHADER key=%s shadow_volume_or_quad_approximation=1\n",key);}
    memcpy(s->b,meta+48,s->h.nb*32);memcpy(s->attrs,meta+48+s->h.nb*32,s->h.na*8);
    if(s->h.c1n){glGenBuffers(1,&s->c1);glBindBuffer(GL_UNIFORM_BUFFER,s->c1);uint8_t *constants=calloc(1,65536);if(!constants)gl_stop("shader constants",0);memcpy(constants,meta+size-s->h.c1n,s->h.c1n);glBufferData(GL_UNIFORM_BUFFER,65536,constants,GL_STATIC_DRAW);free(constants);}free(meta);
    snprintf(name,sizeof name,"%s.glsl",key);char *code=gl_image_shader_source(s,(char *)gl_file(name,&size,0));if(s->bad){free(code);return s;}GLenum type=s->h.stage==0?GL_VERTEX_SHADER:s->h.stage==1?GL_FRAGMENT_SHADER:GL_COMPUTE_SHADER;
    s->buffer_sampler=strstr(code,"samplerBuffer")!=NULL;
    GLuint shader=glCreateShader(type);const char *text=code;glShaderSource(shader,1,&text,NULL);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(ok){s->program=glCreateProgram();if(s->h.stage!=5)glProgramParameteri(s->program,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(s->program,shader);glLinkProgram(s->program);glGetProgramiv(s->program,GL_LINK_STATUS,&ok);}
    if(!ok){char log[4096]={0};if(s->program)glGetProgramInfoLog(s->program,sizeof log,NULL,log);else glGetShaderInfoLog(shader,sizeof log,NULL,log);
        fprintf(stderr,"[nvn-gles] SHADER_REJECT key=%s stage=%u log=%s\n",key,s->h.stage,log);s->bad=1;gl_filter("shader_compile_or_link",s->h.stage);}
    else {s->object=shader;fprintf(stderr,"[nvn-gles] SHADER_READY key=%s stage=%u bindings=%u\n",key,s->h.stage,s->h.nb);}
    if(!ok)glDeleteShader(shader);free(code);L.compiles++;return s;
}
static GlGraphics *gl_graphics(GlShader *vs,GlShader *fs){
    for(unsigned i=0;i<L.ngraphics;i++)if(L.graphics[i].vs==vs&&L.graphics[i].fs==fs)return &L.graphics[i];
    if(L.ngraphics==GL_OBJECTS)gl_stop("graphics program pair count",L.ngraphics);GlGraphics *p=&L.graphics[L.ngraphics++];p->vs=vs;p->fs=fs;
    p->program=glCreateProgram();glAttachShader(p->program,vs->object);glAttachShader(p->program,fs->object);glLinkProgram(p->program);GLint ok=0;glGetProgramiv(p->program,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096]={0};glGetProgramInfoLog(p->program,sizeof log,NULL,log);fprintf(stderr,"[nvn-gles] GRAPHICS_REJECT vs=%s fs=%s log=%s\n",vs->key,fs->key,log);p->bad=1;gl_filter("graphics_pair_link",L.ngraphics);}
    else {const char *names[]={"dm_BaseVertex","dm_BaseInstance","dm_DrawID","dm_OriginY","dm_DepthZero"};for(unsigned i=0;i<5;i++)p->uniforms[i]=glGetUniformLocation(p->program,names[i]);}
    return p;
}
static GlIndex *gl_index_find(const NVNshaderData *data){
    const uint8_t *control=data->control;size_t length=nvn_shader_control_identity_bytes(control);
    if(!length)gl_stop("shader control layout",0);
    uint32_t stage=gl_u32(control+0x714),size=gl_u32(control+0x704);uint64_t off;
    GlPool *pool=gl_pool(data->data,size,&off);if(!pool||!size||size>16*1024*1024)gl_stop("shader identity range",size);
    const uint8_t *code=pool->guest+off;uint64_t hash=bytes_hash(control,length,14695981039346656037ull^stage);hash=bytes_hash(code,size,hash);
    for(int j=L.index_hash[hash&16383];j;j=L.index[j-1].next){GlIndex *e=&L.index[j-1];
        if(e->hash==hash&&e->stage==stage&&e->size==size&&e->control_bytes==length&&!memcmp(e->control,control,length)&&!memcmp(e->code,code,size))return e;}
    return NULL;
}
/* Unity frees temporary NVN 14 controls after ProgramSetShaders. Retain the
 * immutable index's control only after validating every declared control byte
 * and all GPU code. Never read the padded 0x880 slot length from a live 0x848
 * control; the padding exists only in the offline index. */
void dm_vk_program_freeze_controls(NVNshaderData *shaders,int count){
    mtl_init();gl_enter();gl_index_load();
    for(int i=0;i<count;i++){GlIndex *entry=gl_index_find(&shaders[i]);if(!entry)gl_stop("program creation missing verified shader identity",i);shaders[i].control=entry->control;}
    gl_leave();
}
static GlProgram *gl_program(const prog_t *p,unsigned stage){
    if(!p)return NULL;
    for(unsigned i=0;i<L.nprogram;i++)if(L.programs[i].serial==p->serial&&L.programs[i].stage==(int)stage)return &L.programs[i];
    gl_index_load();const char *found=NULL;
    for(unsigned i=0;i<p->nstage;i++){
        const uint8_t *control=p->sd[i].control;if(!control)gl_stop("null shader control",i);
        if(gl_u32(control+0x714)!=stage)continue;GlIndex *entry=gl_index_find(&p->sd[i]);if(entry)found=entry->key;
        if(found)break;
    }
    if(!found)gl_filter("shader_identity_not_in_bank",stage);
    if(L.nprogram==GL_OBJECTS)gl_stop("program identity cache count",L.nprogram);GlProgram *program=&L.programs[L.nprogram++];*program=(GlProgram){.serial=p->serial,.stage=(int)stage};if(found)strcpy(program->key,found);return program;
}
static GlShader *gl_stage(unsigned stage){
    GlProgram *program=gl_program(L.state.program[stage],stage);if(!program||!program->key[0])return NULL;
    if(!program->shader)program->shader=gl_shader_load(program->key);return program->shader;
}
static void gl_driver_bytes(unsigned stage,unsigned bank,uint8_t bytes[65536]){
    memset(bytes,0,65536);
    if(bank==0){if(stage==5)memcpy(bytes+0x200,L.state.dispatch,8);unsigned base=stage==5?0x310:stage==1?0x510:0x110;
        for(unsigned i=0;i<16;i++){GlRange r=L.state.ss[stage][i];uint32_t size=r.size;memcpy(bytes+base+i*16,&r.addr,8);memcpy(bytes+base+i*16+8,&size,4);
            if(stage==5){r=L.state.ub[stage][i];size=r.size;memcpy(bytes+0x210+i*16,&r.addr,8);memcpy(bytes+0x218+i*16,&size,4);}}}
    else if(bank==2){for(unsigned i=0;i<128;i++){uint32_t t=L.state.tex[stage][i],s=L.state.samp[stage][i];memcpy(bytes+0x168+i*8,&t,4);memcpy(bytes+0x568+i*8,&s,4);}for(unsigned i=0;i<9;i++){uint32_t t=L.state.img[stage][i];memcpy(bytes+0x120+i*8,&t,4);}}
    else gl_stop("driver table bank",bank);
}
static int gl_bind_shader(GlShader *s,GLuint program){
    if(!s||s->bad)return 0;unsigned stage=s->h.stage;
    for(unsigned i=0;i<s->h.nb;i++){
        const GlBinding *b=&s->b[i];if(b->unit<0||b->count!=1)gl_stop("binding metadata",i);
        if(b->kind==16){if(!gl_bind_buffer_sampler(s,b,program))return 0;}
        else if(b->kind==12||b->kind==13){
            if(stage||b->type!=4||b->flags||b->slot<0||b->slot>=16)gl_stop("vertex storage texture metadata",i);
            GlRange r=b->kind==12?L.state.ss[stage][b->slot]:L.state.ub[stage][b->slot];uint64_t offset;
            uint64_t texels=r.size/4,height=(texels+2047)/2048;unsigned width=texels<2048?texels:2048;
            if(!r.addr||!r.size||r.size%4||height>(uint64_t)L.max_texture2d||b->unit>=L.max_tex)return gl_filter("vertex_storage_texture_range",r.size);
            if(L.resource_reuse){GLuint texture=gl_resource_snapshot(r,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);if(!texture)return 0;glActiveTexture(GL_TEXTURE0+b->unit);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(b->unit,0);continue;}
            GlPool *p=gl_upload(r,&offset);if(offset%4)return gl_filter("vertex_storage_texture_alignment",offset);
            if(L.ntemp==GL_WRITES)gl_stop("temporary storage textures",L.ntemp);GlTemp *t=&L.temps[L.ntemp++];*t=(GlTemp){0};glGenTextures(1,&t->texture);
            glActiveTexture(GL_TEXTURE0+b->unit);glBindTexture(GL_TEXTURE_2D,t->texture);glTexStorage2D(GL_TEXTURE_2D,1,GL_R32UI,width,height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,p->backing.name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            unsigned rows=texels/width,tail=texels%width;
            if(rows)glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,rows,GL_RED_INTEGER,GL_UNSIGNED_INT,(void *)(uintptr_t)offset);
            if(tail)glTexSubImage2D(GL_TEXTURE_2D,0,0,rows,tail,1,GL_RED_INTEGER,GL_UNSIGNED_INT,(void *)(uintptr_t)(offset+(uint64_t)rows*width*4));
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindSampler(b->unit,0);L.snapshot_calls[0]++;L.snapshot_bytes[0]+=r.size;
        }else if(b->kind==14){
            if(b->slot<0||b->slot>=128||b->unit>=L.max_tex)gl_stop("buffer sampler metadata",i);
            unsigned id=L.state.tex[stage][b->slot]&0xfffff;if(id>=L.nslot||!L.slots[id].live)return gl_filter("null_buffer_sampler_binding",id);
            GlTextureSlot *slot=&L.slots[id];unsigned texels;GLuint texture=gl_buffer_sampled_view(slot,b,&texels);if(!texture)return 0;
            glActiveTexture(GL_TEXTURE0+b->unit);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(b->unit,0);
            char uniform[48];snprintf(uniform,sizeof uniform,"dm_BufferTexels_%d",b->unit);GLint location=glGetUniformLocation(program,uniform);if(location>=0)glProgramUniform1i(program,location,texels);
        }else if(b->type==6||b->type==7){
            int storage=b->type==7;if(b->unit>=(storage?L.max_ssbo:L.max_ubo))return gl_filter("buffer_binding_limit",b->unit);
            GLenum target=storage?GL_SHADER_STORAGE_BUFFER:GL_UNIFORM_BUFFER;GLuint buffer=0;uint64_t offset=0,size=65536;GlRange r={0};
            if(b->kind==7)buffer=s->c1;
            else if(b->kind==1){size=1392;if(!L.support_buffer){uint8_t bytes[1392]={0};for(unsigned j=0;j<73;j++){float one=1;memcpy(bytes+192+16*j,&one,4);}glGenBuffers(1,&L.support_buffer);glBindBuffer(GL_UNIFORM_BUFFER,L.support_buffer);glBufferData(GL_UNIFORM_BUFFER,size,bytes,GL_STATIC_DRAW);}buffer=L.support_buffer;}
            else if(b->kind==8){uint8_t bytes[65536];gl_driver_bytes(stage,b->slot,bytes);buffer=gl_temp(sizeof bytes,bytes);}
            else if(b->kind==2||b->kind==3||b->kind==9){
                if(b->slot<0||b->slot>=16)gl_stop("buffer binding slot",b->slot);r=b->kind==3?L.state.ss[stage][b->slot]:L.state.ub[stage][b->slot];size=r.size;
                if(!r.addr||!r.size)return gl_filter("unbound_buffer",i);
                if(storage&&b->flags){GlPool *p=gl_upload(r,&offset);buffer=p->backing.name;}else buffer=gl_read_buffer(r,&offset);
                unsigned alignment=storage?L.ssbo_align:L.ubo_align;
                if(offset%alignment){if(storage)return gl_filter("unaligned_storage_binding",offset);GLuint copy=gl_temp(size,NULL);glBindBuffer(GL_COPY_READ_BUFFER,buffer);glBindBuffer(GL_COPY_WRITE_BUFFER,copy);glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,offset,0,size);buffer=copy;offset=0;}
                if(storage&&b->flags)gl_mark(r);
            }else return gl_filter("buffer_resource_kind",b->kind);
            if(!buffer)return gl_filter("missing_constant_buffer",i);glBindBufferRange(target,b->unit,buffer,offset,size);
        }else if(b->kind==4||b->kind==6||b->kind==10||b->kind==11||b->kind==15){
            int image=b->kind==6||b->kind==11||b->kind==15||(b->kind==10&&b->type==5);
            if(b->slot<0||b->slot>=(image?32:128))gl_stop("image slot",b->slot);uint64_t handle=image?L.state.img[stage][b->slot]:L.state.tex[stage][b->slot];unsigned id=handle&0xfffff;
            if(id>=L.nslot||!L.slots[id].live)return gl_filter("null_texture_binding",id);GlTextureSlot *slot=&L.slots[id];
            if(!gl_view_supported(slot->has_view?&slot->view:NULL,&slot->source))return 0;GlTexture *t=gl_texture(&slot->source,1);if(!t)return 0;
            if(image&&t->unorm16_float)gl_stop("R16 storage image requires native norm16",slot->source.serial);
            unsigned level=image&&slot->has_view&&slot->view.set_levels?slot->view.base_level:0;
            if(b->kind==6||b->kind==15||(b->kind==10&&b->type==5)){if(b->unit>=L.max_img)return gl_filter("image_binding_limit",b->unit);
                if(b->kind==15&&s->image_formats[i]!=t->internal)return gl_filter("runtime_storage_image_format_changed",t->internal);
                int layered=gl_storage_3d(t->target)||t->target==GL_TEXTURE_CUBE_MAP;
                glBindImageTexture(b->unit,t->name,level,layered,0,GL_READ_WRITE,t->internal);if(t->target==GL_TEXTURE_BUFFER)gl_mark((GlRange){(uint64_t)(uintptr_t)t->source.mem,t->source.L.size});else{t->gpu_owned=1;t->version++;}}
            else {if(b->unit>=L.max_tex)return gl_filter("sampler_binding_limit",b->unit);GLuint name=gl_sampled_view(slot,t);if(!name)return 0;glActiveTexture(GL_TEXTURE0+b->unit);glBindTexture(t->target,name);
                uint64_t sampler_handle=b->sampler>=0?L.state.samp[stage][b->sampler]:handle;unsigned sid=(sampler_handle>>20)&0xfff;
                GLuint sampler=t->target!=GL_TEXTURE_BUFFER&&sid<GL_OBJECTS?L.samplers[sid]:0;
                /* The implemented NVN setter carries floating-point colors.
                 * Integer texture borders require typed sampler variants;
                 * never bind a float border to an integer lookup (undefined). */
                if(sampler&&!gl_sampler_border_supported(sid,t->format))return gl_filter("integer_sampler_border_color",t->source.serial);
                if(t->unorm16_float&&!L.float_linear){
                    GLint min=GL_NEAREST,mag=GL_NEAREST;
                    if(sampler){glGetSamplerParameteriv(sampler,GL_TEXTURE_MIN_FILTER,&min);glGetSamplerParameteriv(sampler,GL_TEXTURE_MAG_FILTER,&mag);}
                    if(mag==GL_LINEAR||min==GL_LINEAR||min==GL_LINEAR_MIPMAP_NEAREST||min==GL_LINEAR_MIPMAP_LINEAR||min==GL_NEAREST_MIPMAP_LINEAR){
                        fprintf(stderr,"[nvn-gles] R16_LINEAR_UNSUPPORTED serial=%llu extent=%ux%ux%u levels=%u key=%s\n",(unsigned long long)t->source.serial,t->source.d.width,t->source.d.height,t->source.d.depth,t->source.d.levels,s->key);
                        gl_stop("R16 sampler requires float linear filtering",t->source.serial);
                    }
                }
                glBindSampler(b->unit,sampler);}
        }else return gl_filter("resource_kind",b->kind);
        GLenum error=glGetError();if(error){fprintf(stderr,"[nvn-gles] RESOURCE_REJECT key=%s binding=%u kind=%d error=%u\n",s->key,i,b->kind,error);return gl_filter("resource_binding_driver_reject",error);}
    }
    return 1;
}

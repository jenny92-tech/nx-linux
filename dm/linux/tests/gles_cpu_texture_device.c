// INPUT: Synthetic NVN bytes and the actual optional GLES backend on a device.
// OUTPUT: CPU/GPU pixel checks for strided writes and GPU-owned neighbours.
// POS: No game resources, guest execution, or production entry changes.
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_gl.c"
static SDL_Window *probe_window;
void *dm_android_native_window(void){return probe_window;}
void dm_android_front_presented(uint64_t frame){(void)frame;}
double front_display_refresh(char *name,size_t size){snprintf(name,size,"GLES CPU texture fixture");return 60;}
void dm_host_shutdown(const char *why){fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2);}
void dm_shot_written(const char *path,int ok){(void)path;(void)ok;}
uint64_t soul_present_count(void){return 0;}
void dm_ui_text_entered(const char *text){(void)text;}
int front_text_input(const char *title,const char *sub,const char *guide,const char *initial,int max_chars,char *out,int out_cap){
    (void)title;(void)sub;(void)guide;(void)initial;(void)max_chars;(void)out;(void)out_cap;abort();
}
static unsigned checks;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"GLES_CPU_TEXTURE_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)
static void linear_sample(GlTexture *t){
    const char *vs="#version 320 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}";
    const char *fs="#version 320 es\nprecision highp float;uniform highp sampler2D src;layout(location=0) out vec4 c;void main(){c=vec4(texture(src,vec2(1.0/256.0,0.5/256.0)).rrr,1);}";
    GLuint v=glCreateShader(GL_VERTEX_SHADER),f=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
    glShaderSource(v,1,&vs,NULL);glCompileShader(v);glShaderSource(f,1,&fs,NULL);glCompileShader(f);glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);REQUIRE(linked);
    GLuint out;glGenTextures(1,&out);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,out);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,1,1);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out,0);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glBindTexture(GL_TEXTURE_2D,t->name);glBindSampler(0,0);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"src"),0);glViewport(0,0,1,1);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDrawArrays(GL_TRIANGLES,0,3);
    uint8_t pixel[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);gl_check("R16 linear sampling");
    REQUIRE(pixel[0]>=127&&pixel[0]<=128&&pixel[1]==pixel[0]&&pixel[2]==pixel[0]&&pixel[3]==255);
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(v);glDeleteShader(f);glDeleteTextures(1,&out);
}
static tex_t texture(uint8_t *memory,unsigned format,unsigned width,unsigned height,uint64_t serial){
    tex_t t={.d={.target=NVN_TEXTURE_TARGET_2D,.format=format,.width=width,.height=height,.depth=1,.levels=1},.mem=memory,.serial=serial};
    nvn_tex_layout(&t.d,&t.L);REQUIRE(t.L.size&&t.L.size<=262144);return t;
}
static void read_rgba(tex_t *source,uint8_t *pixels){
    gl_enter();GlTexture *t=gl_texture(source,0);REQUIRE(t);
    glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,source->d.width,source->d.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);gl_check("fixture read");gl_leave();
}
int main(void){
    setenv("DM_GLES_FILTER_UNSUPPORTED","0",1);REQUIRE(!SDL_Init(SDL_INIT_VIDEO));REQUIRE(!dm_linux_gles_configure());
    probe_window=SDL_CreateWindow("GLES texture bytes",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    REQUIRE(probe_window&&dm_linux_gles_bind_window(probe_window));dm_vk_set_window(probe_window);mtl_init();
    uint8_t *memory=calloc(1,524288);REQUIRE(memory);mtl_pool_add(memory,524288);
    tex_t a=texture(memory,NVN_FORMAT_RGBA8,8,8,1);uint8_t red[256],actual[256],cpu[256];
    for(unsigned i=0;i<64;i++)memcpy(red+i*4,(uint8_t[]){255,0,0,255},4);
    NVNcopyRegion all={.width=8,.height=8,.depth=1};dm_vk_write_texels(&a,NULL,&all,red,0,0);read_rgba(&a,actual);REQUIRE(!memcmp(red,actual,256));
    /* GPU changes do not update guest RAM. A partial CPU write must preserve
     * those pixels, and an equal-to-old-RAM write must still overwrite GPU. */
    gl_enter();GlTexture *t=gl_texture(&a,0);REQUIRE(!t->gpu_owned);glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);
    float green[]={0,1,0,1};glClearBufferfv(GL_COLOR,0,green);t->gpu_owned=1;t->version++;glFinish();gl_leave();
    uint8_t blue[24];memset(blue,0xab,sizeof blue);for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)memcpy(blue+y*12+x*4,(uint8_t[]){0,0,255,255},4);
    NVNcopyRegion part={.xoffset=1,.yoffset=2,.width=2,.height=2,.depth=1};dm_vk_write_texels(&a,NULL,&part,blue,12,24);
    read_rgba(&a,actual);REQUIRE(!nvn_deswizzle(&a.d,0,0,a.mem,cpu,32));
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
        unsigned i=(y*8+x)*4;int inside=x>=1&&x<3&&y>=2&&y<4;
        REQUIRE(!memcmp(actual+i,inside?(uint8_t[]){0,0,255,255}:(uint8_t[]){0,255,0,255},4));
        REQUIRE(!memcmp(cpu+i,inside?(uint8_t[]){0,0,255,255}:(uint8_t[]){255,0,0,255},4));
    }
    NVNcopyRegion pixel={.width=1,.height=1,.depth=1};dm_vk_write_texels(&a,NULL,&pixel,red,0,0);read_rgba(&a,actual);REQUIRE(!memcmp(actual,red,4));REQUIRE(actual[5]==255);
    tex_t bc=texture(memory+262144,NVN_FORMAT_RGB_DXT1,8,8,2);uint8_t blocks[32]={0};
    for(unsigned i=0;i<4;i++){blocks[i*8]=0;blocks[i*8+1]=0xf8;}
    dm_vk_write_texels(&bc,NULL,&all,blocks,0,0);read_rgba(&bc,actual);REQUIRE(!memcmp(actual,red,256));
    tex_t norm=texture(memory+327680,NVN_FORMAT_R16,256,256,3);uint16_t *values=malloc(131072);REQUIRE(values);
    for(unsigned i=0;i<65536;i++)values[i]=65535-i;
    REQUIRE(!nvn_swizzle(&norm.d,0,0,values,512,norm.mem));
    gl_enter();t=gl_texture(&norm,1);REQUIRE(t);glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    float *initial=malloc(65536*4*sizeof(float));REQUIRE(initial);glReadPixels(0,0,256,256,GL_RGBA,GL_FLOAT,initial);gl_check("R16 initial pool upload");
    for(unsigned i=0;i<65536;i++)REQUIRE(fabsf(initial[i*4]-(float)(65535-i)/65535.0f)<0.0000001f);
    free(initial);gl_leave();for(unsigned i=0;i<65536;i++)values[i]=i;
    NVNcopyRegion norm_all={.width=256,.height=256,.depth=1};dm_vk_write_texels(&norm,NULL,&norm_all,values,0,0);
    gl_enter();t=gl_texture(&norm,0);REQUIRE(t);glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);float *floats=malloc(65536*4*sizeof(float));REQUIRE(floats);
    glReadPixels(0,0,256,256,GL_RGBA,GL_FLOAT,floats);gl_check("fixture R16 read");
    for(unsigned i=0;i<65536;i++)REQUIRE(fabsf(floats[i*4]-(float)i/65535.0f)<0.0000001f);
    gl_leave();free(floats);free(values);
    uint16_t endpoints[]={0,65535};NVNcopyRegion two={.width=2,.height=1,.depth=1};dm_vk_write_texels(&norm,NULL,&two,endpoints,0,0);
    gl_enter();if(L.float_linear)linear_sample(gl_texture(&norm,0));gl_leave();
    uint16_t strided[]={0,65535,0xabcd,0xabcd,32768,16384,0xabcd,0xabcd};memcpy(memory+300032,strided,sizeof strided);
    p_copy_b2t copy={.src=(uintptr_t)(memory+300032),.tex=&norm,.r={.width=2,.height=2,.depth=1},.flags=NVN_COPY_FLAGS_ENGINE_2D_BIT};
    mtl_sub_begin();L.state.copy_stride=8;L.state.copy_image_stride=16;gl_copy_buffer_texture(&copy);mtl_sub_commit();
    gl_enter();t=gl_texture(&norm,0);glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    float patch[16];glReadPixels(0,0,2,2,GL_RGBA,GL_FLOAT,patch);gl_check("R16 strided buffer copy");
    for(unsigned i=0;i<4;i++)REQUIRE(fabsf(patch[i*4]-(float)strided[(i/2)*4+i%2]/65535.0f)<0.0000001f);
    gl_leave();
    tex_t cube={.d={.target=NVN_TEXTURE_TARGET_CUBEMAP_ARRAY,.format=NVN_FORMAT_RGBA8,.width=1,.height=1,.depth=2,.levels=1},.mem=memory+491520,.serial=4};
    nvn_tex_layout(&cube.d,&cube.L);REQUIRE(cube.L.layers==12&&cube.L.size<=32768);
    view_t face={.set_layers=1,.min_layer=7,.nlayers=1};dm_vk_write_texels(&cube,&face,&pixel,red,0,0);
    gl_enter();t=gl_texture(&cube,1);REQUIRE(t);glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,t->name,0,7);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);gl_check("cube array read");REQUIRE(!memcmp(actual,red,4));
    glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,t->name,0,6);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);REQUIRE(!memcmp(actual,(uint8_t[4]){0},4));gl_leave();
    mtl_pool_del(memory);free(memory);
    fprintf(stdout,"{\"passed\":true,\"checks\":%u,\"partial_cpu_gpu_coherence\":true,\"bc1_decode\":true,\"r16_all_values\":true,\"r16_strided_buffer_copy\":true,\"r16_linear_filter_supported\":%s,\"cube_array_layer_isolation\":true,\"game_executed\":false}\n",checks,L.float_linear?"true":"false");
    SDL_DestroyWindow(probe_window);SDL_Quit();return 0;
}

// INPUT: Immutable NVN command records and a separately prepared GLSL ES bank.
// OUTPUT: Native GLES rendering and an explicit unsupported-feature ledger.
// POS: Optional Linux verification backend; never selected by Vulkan builds.
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <SDL2/SDL.h>
#include "nvn_obj.h"
#include "nvn_mtl.h"
#include "nvn_dirty.h"
#include "nvn_translation_sync.h"
#include "nvn_copy_pitch.h"
#include "nvn_vk_memory.h"
#include "nvn_gl_buffer.h"
#include "nvn_gl_astc_copy.h"
#include "nvn_vk_snapshot.h"
#include "nvn_vk_shader_index.h"
#include "nvn_vk_zcull.h"
#include "nvn_transfer.h"
#include "nvn_vk_counter_plan.h"
#include "nvn_vk_classic_plan.h"
#include "nvn_vk_menu_scene.h"
#include "nvn_vk_writeback_plan.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "../../linux/window_gles.h"
#include "../../linux/status_hud.h"
#include "../../android/nvn_platform_backend.h"
#include "../tex/nvn_deswizzle.h"
#include "../tex/nvn_bcdec.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

void dm_guest_write_prepare(void *,size_t);
void dm_android_front_presented(uint64_t);
int dm_linux_menu_scene_context(void);
void dm_linux_native_hd_admission(int);
void dm_linux_native_hd_world_frame(int,uint64_t);
#define GL_OBJECTS 8192
#define GL_POOLS 512
#define GL_BINDINGS 64
#define GL_WRITES 4096
typedef NvnTransferRange GlRange;
typedef struct {const uint8_t *guest;uint64_t size,serial,used_end;NvnGlBuffer backing;uint32_t flags;} GlPool;
typedef struct {tex_t source;GLuint name;GLenum target,internal,format,type;unsigned bpp;uint64_t stamp,version;uint8_t *cpu_snapshot;size_t cpu_snapshot_size;int unsupported,gpu_owned,unorm16_float;} GlTexture;
typedef struct {int live;tex_t source;view_t view;int has_view;GLuint sampled_view;uint64_t sampled_view_bytes,sampled_version;} GlTextureSlot;
typedef struct {uint32_t magic,version,stage,nb,na,c1n,ncap,local[3],fout,reserved;} GlHeader;
typedef struct {int32_t unit,binding,type,kind,slot,sampler,count,flags;} GlBinding;
typedef struct {char key[17];GlHeader h;GlBinding b[GL_BINDINGS];uint32_t attrs[32][2];GLenum image_formats[GL_BINDINGS];GLuint program,object,c1;int loaded,bad,buffer_sampler;} GlShader;
typedef struct {GlShader *vs,*fs;GLuint program;GLint uniforms[5];int bad;} GlGraphics;
typedef struct {const uint8_t *control,*code;uint32_t stage,size;size_t control_bytes;char key[17];uint64_t hash;int next;} GlIndex;
typedef struct {uint64_t serial;int stage;GlShader *shader;char key[17];} GlProgram;
typedef struct {char key[17];NvnClassicShader facts;} GlFacts;
typedef struct {GLuint buffer,texture;} GlTemp;
typedef struct {GlRange range;uint64_t pool_serial,stamp;} GlUpload;
typedef struct {GlUpload source;GLuint buffer;} GlReadSnapshot;
#define GL_RESOURCE_ENTRIES 64
#define GL_RESOURCE_BUDGET (64ull*1024*1024)
typedef struct {GlUpload source;GLuint texture;GLenum internal,format,type;unsigned bpp,width,height;uint64_t bytes,age;int pinned;} GlResource;
static void gl_resource_forget(uint64_t serial);
static struct {
    NvnGlAstcCopy astc_copy;uint64_t astc_copies,astc_texels,astc_refreshes,astc_refresh_blocks,astc_baseline_bytes,diag_present,diag_arm;int diag_main;unsigned diag_meshes;
    SDL_Window *window;char shader_root[1024];pthread_mutex_t mutex;int initialized,filter,live,origin,depth_mode,mali_g52_g13,norm16,float_linear,trace_pixels;int vi_crop[4];
    uint64_t queue_wait_dependencies,queue_wait_barriers;int queue_wait_pending;
    GLint max_ubo,max_ssbo,max_tex,max_img,ubo_align,ssbo_align,texbuf_align,max_texbuf,max_color,max_draw,max_texture2d,max_groups[3];GLuint fbo,read_fbo,vao,pipeline;
    GlPool pools[GL_POOLS];unsigned npool;uint64_t pool_serial;
    GlUpload upload_cache[4096];GlReadSnapshot read_cache[256];
    int resource_reuse,stream_updates,mapped_updates,submission_active;GlResource resources[GL_RESOURCE_ENTRIES];
    uint64_t resource_bytes,resource_age,resource_hits,resource_partial,resource_evictions;
    GlTexture textures[GL_OBJECTS];unsigned ntexture;
    GlTextureSlot *slots;unsigned nslot;GLuint samplers[GL_OBJECTS];uint8_t sampler_border[GL_OBJECTS];
    GlShader shaders[GL_OBJECTS];unsigned nshader;GlProgram programs[GL_OBJECTS];unsigned nprogram;
    GlGraphics graphics[GL_OBJECTS];unsigned ngraphics;
    GlFacts facts[GL_OBJECTS];unsigned nfacts;
    const uint8_t **plan_ops;uint8_t *counter_live,*classic_omit;size_t plan_count,plan_capacity,plan_position;
    int counter_enabled,classic_enabled,menu_enabled,menu_valid,menu_previous,plan_valid,plan_omitted,classic_valid,draw_counter_live,classic_world_frame;
    uint8_t *menu_omit;NvnMenuSceneCache menu_cache;uint64_t menu_epoch,menu_reuses,menu_warm,menu_fallbacks;_Atomic uint64_t menu_input_epoch;
    GLuint support_buffer;uint64_t staged_submit_bytes,cpu_equal_copies,cpu_equal_copy_bytes;
    NvnClassicTargets classic_history;NvnClassicMemo classic_memo;
    uint64_t counter_omitted,classic_draws,classic_clears,classic_copies,classic_dispatches,plan_fallbacks;
    uint8_t *index_bytes;size_t index_size;GlIndex *index;unsigned nindex;int index_hash[16384];
    struct {const prog_t *program[6];p_set_rt rt;va_st attr[16];vs_st stream[16];NVNbufferRange vb[16];
        GlRange ub[6][16],ss[6][16];uint64_t tex[6][128],samp[6][128],img[6][32];
        blend_st blend[8];color_st color;chmask_st mask;ds_st ds;poly_st poly;ms_st ms;
        p_scissor sc;int sc_set;float viewport[4],near,far,blend_color[4];
        uint32_t stencil_compare[2],stencil_write[2],stencil_ref[2];p_poly_offset offset;int dispatch[3];int64_t copy_stride,copy_image_stride;} state;
    p_set_rt cached_target;int target_valid;uint64_t target_hits,target_misses;
    GlRange writes[GL_WRITES];unsigned nwrites;GlTemp temps[GL_WRITES];unsigned ntemp;
    int uniform_snapshot,readonly_draw,coherent_reads;const uint8_t *write_cpu[GL_WRITES];
    unsigned uniform_pending[GL_WRITES],nuniform_pending;uint64_t uniform_pending_bytes;
    NvnVkWritebackSpan uniform_plan[GL_WRITES];
    uint64_t uniform_deferred,uniform_flushes,uniform_updates,uniform_snapshots,uniform_snapshot_bytes,uniform_cache_hits;
    uint64_t coherent_api_barriers,coherent_draw_barriers,coherent_guest_barriers,coherent_writer_barriers;
    NvnVkWritebackSpan writeback_plan[GL_WRITES];size_t writeback_count;
    GLuint download_buffer;uint64_t download_total,download_calls,download_bytes,download_maps,direct_readback_maps;int download_staged;
    struct {volatile uint32_t *addr;uint32_t mode,value;} events[GL_WRITES];unsigned nevent;
    struct {uint64_t addr,host_tick;uint32_t type;GLuint query,timer;} reports[GL_WRITES];unsigned nreport;
    PFNGLQUERYCOUNTEREXTPROC query_counter;PFNGLGETQUERYOBJECTUI64VEXTPROC query_u64;int host_timestamps;
    GLuint sample_query,queries[GL_WRITES];unsigned nquery;uint64_t samples;int query_active;
    uint64_t fence,frame,presents,draws,dispatches,passes,skipped_draws,skipped_dispatches,filtered,uploads,compiles,driver_errors,sampled_view_bytes,view_copies;
    uint64_t readonly_snapshot_calls,readonly_snapshot_bytes,readonly_gpu_snapshots,readonly_cache_hits;
    uint64_t pool_update_ns,readback_ns,gpu_wait_ns;
    uint64_t staged_update_bytes,staged_update_calls,direct_sampler_bytes,direct_sampler_calls;
    uint64_t buffer_upload_bytes,buffer_upload_calls,buffer_upload_hits,buffer_untracked_bytes,writeback_bytes,snapshot_bytes[2],snapshot_calls[2];
    uint64_t profile_start,profile_draws,profile_skip,profile_dispatches,profile_dispatch_skip;
    struct {const char *reason;uint64_t count,last_value;} filters[64];unsigned nfilter;
    _Atomic uint64_t completed;
} L;
static _Thread_local unsigned gl_depth;
static _Noreturn void gl_stop(const char *where,uint64_t value){fprintf(stderr,"[nvn-gles] STOP %s (%llu)\n",where,(unsigned long long)value);fflush(stderr);abort();}
static uint64_t gl_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int gl_filter(const char *why,uint64_t value){
    if(!L.filter)gl_stop(why,value);
    unsigned i=0;while(i<L.nfilter&&strcmp(L.filters[i].reason,why))i++;
    if(i==L.nfilter){if(i==64)gl_stop("filter reason capacity",i);L.filters[L.nfilter++].reason=why;}
    L.filters[i].count++;L.filters[i].last_value=value;
    uint64_t n=++L.filtered;if(n<=64 || !(n%4096))fprintf(stderr,"[nvn-gles] FILTER reason=%s value=%llu count=%llu fidelity=degraded\n",why,(unsigned long long)value,(unsigned long long)n);
    return 0;
}
static void gl_check(const char *where){GLenum e=glGetError();if(e)gl_stop(where,e);}
/* ES 3.2 §7.11.2: API buffer updates/copies are implicitly coherent. This
 * helper is ONLY for those API producers. Every shader/compute writer keeps
 * its explicit all-barrier before a later binding can reach these reads.
 * https://registry.khronos.org/OpenGL/specs/es/3.2/es_spec_3.2.pdf */
static void gl_api_memory_barrier(GLbitfield bits){if(L.coherent_reads){L.coherent_api_barriers++;return;}glMemoryBarrier(bits);}
static void gl_draw_memory_barrier(void){
    if(L.coherent_reads&&L.readonly_draw){L.coherent_draw_barriers++;return;}
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}
static void gl_shader_write_barrier(void){
    /* A later incoherent shader store must also wait for earlier reads (WAR).
     * Keep this immediately before the writer, after any binding snapshots. */
    if(L.coherent_reads&&!L.readonly_draw){glMemoryBarrier(GL_ALL_BARRIER_BITS);L.coherent_writer_barriers++;}
}
static void gl_enter(void){pthread_mutex_lock(&L.mutex);if(!gl_depth++&&SDL_GL_MakeCurrent(L.window,dm_linux_gles_context()))gl_stop(SDL_GetError(),0);}
static void gl_leave(void){if(!--gl_depth&&SDL_GL_MakeCurrent(L.window,NULL))gl_stop(SDL_GetError(),0);pthread_mutex_unlock(&L.mutex);}
static uint64_t bytes_hash(const uint8_t *p,size_t n,uint64_t h){for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ull;}return h;}
static void gl_filter_profile(void){char line[8192];size_t at=0;for(unsigned i=0;i<L.nfilter;i++){int n=snprintf(line+at,sizeof line-at," %s=%llu",L.filters[i].reason,(unsigned long long)L.filters[i].count);if(n<0||(size_t)n>=sizeof line-at)gl_stop("filter profile length",at);at+=n;}fprintf(stderr,"[nvn-gles] GLES_FILTER_PROFILE%s\n",at?line:"");}
static uint8_t *gl_file(const char *name,size_t *size,int optional){
    char path[1200];if(snprintf(path,sizeof path,"%s/%s",L.shader_root,name)>=(int)sizeof path)gl_stop("shader path",0);
    FILE *f=fopen(path,"rb");if(!f){if(optional)return NULL;gl_stop(path,errno);}
    if(fseek(f,0,SEEK_END))gl_stop("shader seek",0);long n=ftell(f);
    if(n<0||n>256*1024*1024||fseek(f,0,SEEK_SET))gl_stop("shader file length",n);
    uint8_t *p=malloc((size_t)n+1);if(!p||fread(p,1,n,f)!=(size_t)n)gl_stop("shader read",n);fclose(f);p[n]=0;*size=n;return p;
}
void dm_vk_set_shader_root(const char *root){if(!root||strlen(root)>=sizeof L.shader_root)gl_stop("shader root",0);strcpy(L.shader_root,root);}
void dm_vk_set_window(void *window){L.window=window;}
static pthread_once_t gl_once=PTHREAD_ONCE_INIT;
static int gl_has_extension(const char *name){GLint n=0;glGetIntegerv(GL_NUM_EXTENSIONS,&n);for(GLint i=0;i<n;i++)if(!strcmp((const char *)glGetStringi(GL_EXTENSIONS,i),name))return 1;return 0;}
static void GL_APIENTRY gl_driver_message(GLenum source,GLenum type,GLuint id,GLenum severity,GLsizei length,const GLchar *message,const void *user){
    (void)source;(void)severity;(void)length;(void)user;
    if(type==GL_DEBUG_TYPE_ERROR||type==GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR){uint64_t n=++L.driver_errors;if(n<=64||!(n%4096))fprintf(stderr,"[nvn-gles] GL_DRIVER_ERROR id=%u count=%llu message=%s\n",id,(unsigned long long)n,message);}
}
static void gl_initialize(void){
    pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&L.mutex,&a);pthread_mutexattr_destroy(&a);
    const char *filter=getenv("DM_GLES_FILTER_UNSUPPORTED");if(filter&&strcmp(filter,"0")&&strcmp(filter,"1"))gl_stop("DM_GLES_FILTER_UNSUPPORTED expects 0 or 1",0);L.filter=filter&&!strcmp(filter,"1");
    const char *trace_pixels=getenv("DM_GLES_TRACE_PIXELS");L.trace_pixels=trace_pixels&&!strcmp(trace_pixels,"1");
    const char *diag=getenv("DM_GLES_DIAG_MAIN");L.diag_main=diag&&!strcmp(diag,"1");
    const char *host_clock=getenv("DM_GLES_HOST_TIMESTAMPS");if(host_clock&&strcmp(host_clock,"0")&&strcmp(host_clock,"1"))gl_stop("DM_GLES_HOST_TIMESTAMPS expects 0 or 1",0);L.host_timestamps=host_clock&&!strcmp(host_clock,"1");
    if(L.host_timestamps)gl_filter("diagnostic_host_timestamp_approximation",1);
    const char *reuse=getenv("DM_NVN_RESOURCE_REUSE");if(reuse&&strcmp(reuse,"0")&&strcmp(reuse,"1"))gl_stop("DM_NVN_RESOURCE_REUSE expects 0 or 1",0);L.resource_reuse=reuse&&!strcmp(reuse,"1");
    const char *stream=getenv("DM_GLES_STREAM_UPDATES");if(stream&&strcmp(stream,"0")&&strcmp(stream,"1"))gl_stop("DM_GLES_STREAM_UPDATES expects 0 or 1",0);L.stream_updates=stream&&!strcmp(stream,"1");
    const char *mapped=getenv("DM_GLES_MAPPED_UPDATES");if(mapped&&strcmp(mapped,"0")&&strcmp(mapped,"1"))gl_stop("DM_GLES_MAPPED_UPDATES expects 0 or 1",0);L.mapped_updates=mapped&&!strcmp(mapped,"1");
    const char *uniform=getenv("DM_UNIFORM_SNAPSHOT");if(uniform&&strcmp(uniform,"0")&&strcmp(uniform,"1"))gl_stop("DM_UNIFORM_SNAPSHOT expects 0 or 1",0);L.uniform_snapshot=uniform&&!strcmp(uniform,"1");
    const char *coherent=getenv("DM_GLES_COHERENT_READS");if(coherent&&strcmp(coherent,"0")&&strcmp(coherent,"1"))gl_stop("DM_GLES_COHERENT_READS expects 0 or 1",0);L.coherent_reads=coherent&&!strcmp(coherent,"1");
    if(L.coherent_reads&&!L.uniform_snapshot)gl_stop("coherent reads require whole-draw uniform snapshot guards",0);
    gl_enter();GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    fprintf(stderr,"[nvn-gles] CAPS vendor=%s renderer=%s version=%s shader=%s filter=%d\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION),glGetString(GL_SHADING_LANGUAGE_VERSION),L.filter);
    char hud_api[32];snprintf(hud_api,sizeof hud_api,"OpenGL ES %d.%d",major,minor);
    dm_linux_status_hud_init(hud_api);
    fprintf(stderr,"[nvn-gles] RESOURCE_REUSE enabled=%d texture_budget_bytes=%llu\n",L.resource_reuse,(unsigned long long)GL_RESOURCE_BUDGET);
    L.mali_g52_g13=!strcmp((const char *)glGetString(GL_RENDERER),"Mali-G52")&&strstr((const char *)glGetString(GL_VERSION),"v1.g13p0");
    if(major<3||(major==3&&minor<2))gl_stop("GLES 3.2 required by prepared shader bank",major*10+minor);
    PFNGLDEBUGMESSAGECALLBACKPROC debug_callback=(PFNGLDEBUGMESSAGECALLBACKPROC)SDL_GL_GetProcAddress("glDebugMessageCallback");
    if(debug_callback){glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);debug_callback(gl_driver_message,NULL);}
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS,&L.max_ubo);glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&L.max_ssbo);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&L.max_tex);glGetIntegerv(GL_MAX_IMAGE_UNITS,&L.max_img);
    if(L.max_tex<=32)gl_stop("no spare texture upload unit",L.max_tex);
    L.norm16=gl_has_extension("GL_EXT_texture_norm16");
    L.float_linear=gl_has_extension("GL_OES_texture_float_linear");
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&L.ubo_align);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&L.ssbo_align);
    glGetIntegerv(GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT,&L.texbuf_align);glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&L.max_texbuf);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&L.max_color);glGetIntegerv(GL_MAX_DRAW_BUFFERS,&L.max_draw);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&L.max_texture2d);
    L.query_counter=(PFNGLQUERYCOUNTEREXTPROC)SDL_GL_GetProcAddress("glQueryCounterEXT");L.query_u64=(PFNGLGETQUERYOBJECTUI64VEXTPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT");
    if(!gl_has_extension("GL_EXT_disjoint_timer_query"))L.query_counter=NULL;
    if(L.query_counter){GLint prior=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&prior);fprintf(stderr,"[nvn-gles] TIMER_QUERY initial_disjoint=%d cleared_before_guest_queries=1\n",prior);}
    fprintf(stderr,"[nvn-gles] LIMITS ubo=%d ssbo=%d textures=%d images=%d ubo_alignment=%d ssbo_alignment=%d\n",L.max_ubo,L.max_ssbo,L.max_tex,L.max_img,L.ubo_align,L.ssbo_align);
    fprintf(stderr,"[nvn-gles] TARGET_LIMITS color_attachments=%d draw_buffers=%d texture_buffer_texels=%d texture_buffer_alignment=%d\n",L.max_color,L.max_draw,L.max_texbuf,L.texbuf_align);
    fprintf(stderr,"[nvn-gles] FORMAT_CAPS texture_norm16=%d texture_view_ext=%d texture_view_oes=%d\n",L.norm16,gl_has_extension("GL_EXT_texture_view"),gl_has_extension("GL_OES_texture_view"));
    const GLenum stage_limits[]={GL_MAX_VERTEX_UNIFORM_BLOCKS,GL_MAX_FRAGMENT_UNIFORM_BLOCKS,GL_MAX_COMPUTE_UNIFORM_BLOCKS,GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS,GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS,GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS};
    for(unsigned i=0;i<3;i++)glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,i,&L.max_groups[i]);
    GLint caps[6];for(unsigned i=0;i<6;i++)glGetIntegerv(stage_limits[i],&caps[i]);
    fprintf(stderr,"[nvn-gles] STAGE_LIMITS ubo_vertex=%d ubo_fragment=%d ubo_compute=%d ssbo_vertex=%d ssbo_fragment=%d ssbo_compute=%d timer_query=%d\n",caps[0],caps[1],caps[2],caps[3],caps[4],caps[5],L.query_counter&&L.query_u64);
    glGenFramebuffers(1,&L.fbo);glGenFramebuffers(1,&L.read_fbo);glGenVertexArrays(1,&L.vao);glBindVertexArray(L.vao);glGenProgramPipelines(1,&L.pipeline);glBindProgramPipeline(L.pipeline);
    L.state.near=0;L.state.far=1;L.state.mask.mask=~0u;L.state.viewport[2]=1280;L.state.viewport[3]=720;L.profile_start=gl_now();L.initialized=1;
    gl_check("initialize");gl_leave();
}
void mtl_init(void){pthread_once(&gl_once,gl_initialize);}
static GlPool *gl_pool(uint64_t addr,uint64_t size,uint64_t *offset){
    for(unsigned i=0;i<L.npool;i++){GlPool *p=&L.pools[i];uint64_t base=(uint64_t)(uintptr_t)p->guest;
        if(addr>=base&&addr-base<=p->size&&size<=p->size-(addr-base)){if(offset)*offset=addr-base;return p;}}
    return NULL;
}
void mtl_pool_add_flags(const void *mem,uint64_t size,uint32_t flags){mtl_init();gl_enter();if(!size||L.npool==GL_POOLS)gl_stop("pool bounds",size);L.pools[L.npool++]=(GlPool){.guest=mem,.size=size,.serial=++L.pool_serial,.flags=flags};gl_leave();}
void mtl_pool_add(const void *mem,uint64_t size){mtl_pool_add_flags(mem,size,0);}
void mtl_pool_del(const void *mem){mtl_init();gl_enter();glFinish();for(unsigned i=0;i<L.npool;i++)if(L.pools[i].guest==mem){gl_resource_forget(L.pools[i].serial);glDeleteBuffers(1,&L.pools[i].backing.name);L.pools[i]=L.pools[--L.npool];break;}gl_leave();}
int mtl_addr(uint64_t addr,uint64_t size,void **buffer,uint64_t *offset){GlPool *p=gl_pool(addr,size,offset);if(buffer)*buffer=p;return p!=NULL;}
int mtl_gpu_addr(uint64_t a,uint64_t n,void **b,uint64_t *o,uint64_t *gpu){if(gpu)*gpu=a;return mtl_addr(a,n,b,o);}
static int overlap(GlRange a,GlRange b){return a.size&&b.size&&a.addr<b.addr+b.size&&b.addr<a.addr+a.size;}
static void gl_read_invalidate(GlRange r){for(unsigned i=0;i<256;i++)if(L.read_cache[i].buffer&&overlap(L.read_cache[i].source.range,r))L.read_cache[i].buffer=0;}
static void gl_mark(GlRange r){if(!gl_pool(r.addr,r.size,NULL))gl_stop("GPU write outside pool",r.addr);if(L.nwrites==GL_WRITES)gl_stop("GPU write count",L.nwrites);gl_read_invalidate(r);L.write_cpu[L.nwrites]=NULL;L.writes[L.nwrites++]=r;}
static void gl_backing(GlPool *p,uint64_t required){
    if(required>p->used_end)p->used_end=required;
    if(required<=p->backing.capacity)return;
    gl_check("before pool backing growth");
    fprintf(stderr,"[nvn-gles] BACKING_GROW present=%llu pool=%llu old=%llu required=%llu limit=%llu\n",(unsigned long long)L.presents,(unsigned long long)p->serial,(unsigned long long)p->backing.capacity,(unsigned long long)required,(unsigned long long)p->size);
    GLenum error=nvn_gl_buffer_reserve(&p->backing,required,p->size);
    if(error)gl_stop("pool backing growth",error);
}
static void gl_pool_update(GlPool *pool,uint64_t offset,uint64_t size,const void *data){
    if(!size)return;
    uint64_t start=gl_now();glBindBuffer(GL_COPY_WRITE_BUFFER,pool->backing.name);
    if(L.mapped_updates){
        GLenum error=nvn_gl_buffer_mapped_update(&pool->backing,offset,size,data);if(error)gl_stop("mapped pool update",error);
    }else if((L.stream_updates||L.classic_valid)&&L.submission_active&&size<=4ull*1024*1024&&L.staged_submit_bytes<=4ull*1024*1024-size){
        if(L.ntemp==GL_WRITES)gl_stop("staging buffer count",L.ntemp);GlTemp *t=&L.temps[L.ntemp++];*t=(GlTemp){0};glGenBuffers(1,&t->buffer);
        GLenum error=nvn_gl_buffer_stage_update(&pool->backing,t->buffer,offset,size,data);if(error)gl_stop("staged pool update",error);
        L.staged_update_calls++;L.staged_update_bytes+=size;L.staged_submit_bytes+=size;
    }else glBufferSubData(GL_COPY_WRITE_BUFFER,offset,size,data);
    uint64_t ns=gl_now()-start;L.pool_update_ns+=ns;
    if(L.presents==1&&ns>10000000)fprintf(stderr,"[nvn-gles] SLOW_POOL_UPDATE pool=%llu offset=%llu bytes=%llu capacity=%llu elapsed_ms=%.3f\n",(unsigned long long)pool->serial,(unsigned long long)offset,(unsigned long long)size,(unsigned long long)pool->backing.capacity,ns/1e6);
}
typedef struct {GlPool *pool;int tracked;} GlUploadContext;
static GLuint gl_temp(size_t bytes,const void *data);
#include "nvn_gl_uniform.inc"
static void gl_emit_upload(void *context,uint64_t addr,uint64_t size){
    GlUploadContext *upload=context;
    gl_pool_update(upload->pool,addr-(uint64_t)(uintptr_t)upload->pool->guest,size,(const void *)(uintptr_t)addr);
    L.buffer_upload_calls++;L.buffer_upload_bytes+=size;if(!upload->tracked)L.buffer_untracked_bytes+=size;
}
static GlPool *gl_upload(GlRange r,uint64_t *offset){
    gl_uniform_flush_range(r);
    GlPool *p=gl_pool(r.addr,r.size,offset);if(!p)gl_stop("buffer range outside pool",r.addr);gl_backing(p,r.addr-(uint64_t)(uintptr_t)p->guest+r.size);glBindBuffer(GL_COPY_WRITE_BUFFER,p->backing.name);
    uint64_t hash=r.addr^(r.addr>>19)^(r.size*0x9e3779b97f4a7c15ull)^p->serial,stamp=0;GlUpload *cached=&L.upload_cache[hash&4095];
    int tracked=r.size&&nvn_dirty_tracked((void *)(uintptr_t)r.addr,r.size);
    int partial=(L.resource_reuse||L.stream_updates||L.mapped_updates)&&tracked&&cached->pool_serial==p->serial&&cached->range.addr==r.addr&&cached->range.size==r.size;
    uint64_t previous_stamp=partial?cached->stamp:0;
    if(tracked&&cached->pool_serial==p->serial&&cached->range.addr==r.addr&&cached->range.size==r.size&&!nvn_dirty_since((void *)(uintptr_t)r.addr,r.size,cached->stamp)){L.buffer_upload_hits++;return p;}
    if(tracked){stamp=nvn_dirty_now();nvn_dirty_protect((void *)(uintptr_t)r.addr,r.size);}
    GlUploadContext upload={p,tracked};
    if(nvn_transfer_visit(r.addr,r.size,L.writes,L.nwrites,previous_stamp,partial?nvn_dirty_next_range:NULL,gl_emit_upload,&upload)==UINT64_MAX)gl_stop("buffer transfer plan",r.addr);
    if(tracked)*cached=(GlUpload){r,p->serial,stamp};
    return p;
}
static void gl_queries_end(void){if(L.query_active){glEndQuery(GL_ANY_SAMPLES_PASSED);L.query_active=0;if(L.nquery==GL_WRITES)gl_stop("occlusion query count",L.nquery);L.queries[L.nquery++]=L.sample_query;L.sample_query=0;}}
void mtl_sub_begin(void){mtl_init();gl_enter();
    if(L.queue_wait_pending){glMemoryBarrier(GL_ALL_BARRIER_BITS);L.queue_wait_pending=0;L.queue_wait_barriers++;}
    L.submission_active=1;L.staged_submit_bytes=0;L.nwrites=L.ntemp=L.nevent=L.nreport=L.nquery=0;L.nuniform_pending=0;L.uniform_pending_bytes=0;L.readonly_draw=0;memset(L.read_cache,0,sizeof L.read_cache);}
void mtl_sub_copy(uint64_t src,uint64_t dst,uint64_t size){gl_uniform_flush_all();uint64_t so,doff;GlRange sr={src,size},dr={dst,size};
    if(!gl_pool(src,size,NULL)||!gl_pool(dst,size,&doff)||overlap(sr,dr))gl_stop("buffer copy range",dst);
    /* Every prior GLES submission has completed and published its writes.
     * An equal, CPU-authoritative copy changes neither bytes nor visibility. */
    int pending=0;for(unsigned i=0;i<L.nreport;i++)if(L.reports[i].addr&&(overlap(sr,(GlRange){(uint64_t)(uintptr_t)L.reports[i].addr,16})||overlap(dr,(GlRange){(uint64_t)(uintptr_t)L.reports[i].addr,16})))pending=1;
    for(unsigned i=0;i<L.nevent;i++)if(overlap(sr,(GlRange){(uint64_t)(uintptr_t)L.events[i].addr,4})||overlap(dr,(GlRange){(uint64_t)(uintptr_t)L.events[i].addr,4}))pending=1;
    if(!pending&&nvn_transfer_cpu_copy_unchanged(src,dst,size,L.writes,L.nwrites)){L.cpu_equal_copies++;L.cpu_equal_copy_bytes+=size;return;}
    GlPool *s=gl_upload(sr,&so),*d=gl_pool(dst,size,&doff);gl_backing(d,doff+size);glBindBuffer(GL_COPY_READ_BUFFER,s->backing.name);glBindBuffer(GL_COPY_WRITE_BUFFER,d->backing.name);glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,so,doff,size);gl_mark((GlRange){dst,size});}
void mtl_sub_event(volatile uint32_t *addr,uint32_t mode,uint32_t value){gl_uniform_flush_all();if(mode>1||L.nevent==GL_WRITES||!gl_pool((uint64_t)(uintptr_t)addr,4,NULL))gl_stop("event range",(uint64_t)(uintptr_t)addr);L.events[L.nevent].addr=addr;L.events[L.nevent].mode=mode;L.events[L.nevent++].value=value;}
void mtl_sub_counter(uint64_t addr,uint32_t type){
    gl_uniform_flush_all();
    if(!gl_pool(addr,16,NULL)||L.nreport==GL_WRITES)gl_stop("counter range",addr);
    if(type!=NVN_COUNTER_TYPE_SAMPLES_PASSED&&type!=NVN_COUNTER_TYPE_TIMESTAMP&&type!=NVN_COUNTER_TYPE_TIMESTAMP_TOP)gl_stop("counter type",type);
    if(!L.host_timestamps&&(!L.query_counter||!L.query_u64))gl_stop("guest timestamp requires EXT_disjoint_timer_query",type);
    if(type==NVN_COUNTER_TYPE_SAMPLES_PASSED)gl_filter("occlusion_count_approximated_by_boolean_query",type);
    gl_queries_end();unsigned i=L.nreport++;L.reports[i].addr=addr;L.reports[i].type=type;L.reports[i].query=L.nquery;
    /* Explicit diagnostic fallback uses the guest system-tick clock domain.
     * Fence completion still gates publication; these are issue timestamps,
     * not GPU execution times, and never count completed display frames. */
    L.reports[i].timer=0;L.reports[i].host_tick=(uint64_t)((long double)gl_now()*12/625);
    if(!L.host_timestamps){glGenQueries(1,&L.reports[i].timer);L.query_counter(L.reports[i].timer,GL_TIMESTAMP_EXT);}
}
static void gl_counter_reset(void){gl_queries_end();if(L.nreport==GL_WRITES)gl_stop("counter reset count",L.nreport);unsigned i=L.nreport++;memset(&L.reports[i],0,sizeof L.reports[i]);L.reports[i].query=L.nquery;}
#include "nvn_gl_writeback.inc"
uint64_t mtl_sub_commit(void){
    gl_queries_end();gl_uniform_flush_all();gl_writeback_queue();uint64_t wait_start=gl_now();glMemoryBarrier(GL_ALL_BARRIER_BITS);GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)gl_stop("GPU fence",0);glFlush();
    for(;;){GLenum r=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000ull);if(r==GL_ALREADY_SIGNALED||r==GL_CONDITION_SATISFIED)break;if(r==GL_WAIT_FAILED)gl_stop("GPU wait",r);}glDeleteSync(fence);L.gpu_wait_ns+=gl_now()-wait_start;
    uint64_t read_start=gl_now();
    gl_writeback_publish();
    L.readback_ns+=gl_now()-read_start;
    if(L.nreport&&!L.host_timestamps&&L.query_counter){GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);if(disjoint)gl_stop("disjoint GPU counter interval",disjoint);}
    unsigned query_cursor=0;
    for(unsigned i=0;i<L.nreport;i++){
        while(query_cursor<L.reports[i].query){GLuint result=0;glGetQueryObjectuiv(L.queries[query_cursor++],GL_QUERY_RESULT,&result);L.samples+=result!=0;}
        if(!L.reports[i].addr){L.samples=0;continue;}
        uint64_t stamp=L.reports[i].host_tick;
        if(!L.host_timestamps){GLuint64 timestamp=0;L.query_u64(L.reports[i].timer,GL_QUERY_RESULT,&timestamp);glDeleteQueries(1,&L.reports[i].timer);stamp=(uint64_t)((long double)timestamp*12/625);}
        uint64_t words[2]={L.reports[i].type==NVN_COUNTER_TYPE_SAMPLES_PASSED?L.samples:stamp,stamp};
        dm_guest_write_prepare((void *)(uintptr_t)L.reports[i].addr,16);memcpy((void *)(uintptr_t)L.reports[i].addr,words,16);}
    while(query_cursor<L.nquery){GLuint result=0;glGetQueryObjectuiv(L.queries[query_cursor++],GL_QUERY_RESULT,&result);L.samples+=result!=0;}
    glDeleteQueries(L.nquery,L.queries);L.nquery=0;
    for(unsigned i=0;i<L.nevent;i++){volatile uint32_t *p=L.events[i].addr;dm_guest_write_prepare((void *)p,4);if(L.events[i].mode)__atomic_fetch_add(p,L.events[i].value,__ATOMIC_SEQ_CST);else __atomic_store_n(p,L.events[i].value,__ATOMIC_SEQ_CST);}
    for(unsigned i=0;i<L.ntemp;i++){glDeleteTextures(1,&L.temps[i].texture);glDeleteBuffers(1,&L.temps[i].buffer);}L.ntemp=L.nwrites=L.nevent=L.nreport=0;
    for(unsigned i=0;i<GL_RESOURCE_ENTRIES;i++)L.resources[i].pinned=0;
    L.submission_active=0;gl_check("commit");uint64_t n=++L.fence;atomic_store(&L.completed,n);gl_leave();return n;
}
void mtl_sub_commit_async(void){(void)mtl_sub_commit();}
int mtl_async_enabled(void){return 0;}
uint64_t mtl_last_fence(void){return L.fence;}
int mtl_last_commit_error(void){return 0;}
int mtl_wait(uint64_t fence,uint64_t timeout){uint64_t start=gl_now();while(fence>atomic_load(&L.completed)){if(timeout!=UINT64_MAX&&gl_now()-start>=timeout)return 0;struct timespec p={0,100000};nanosleep(&p,NULL);}return 1;}
void mtl_finish(void){mtl_init();gl_enter();glFinish();gl_leave();}
void mtl_drain(void){mtl_finish();}

/* GLES commits wait for the real GPU fence and publish CPU writes before
 * completion. A queue dependency still needs a visibility barrier at the next
 * submission; accepting a merely enqueued or never-fenced sync is unsafe. */
void dm_vk_queue_wait_sync(const sync_t *sync){
    mtl_init();gl_enter();
    if(L.submission_active||!atomic_load(&sync->fenced)||atomic_load(&sync->fence)>atomic_load(&L.completed))gl_stop("queue wait on unpublished GLES fence",atomic_load(&sync->fence));
    L.queue_wait_pending=1;L.queue_wait_dependencies++;gl_leave();
}
void soul_queue_wait_counters(uint64_t *dependencies,uint64_t *barriers,int *pending){
    mtl_init();gl_enter();*dependencies=L.queue_wait_dependencies;*barriers=L.queue_wait_barriers;*pending=L.queue_wait_pending;gl_leave();
}
/* The D2R target-rebuild protocol is not implemented by this experimental
 * backend. Fail explicitly if requested rather than advertise a ready frame. */
void dm_linux_renderer_rebuild_prepare(void){gl_stop("dynamic target rebuild unavailable in GLES",0);}
int dm_linux_renderer_rebuild_ready(void){return 0;}
void dm_linux_renderer_rebuild_finish(void){gl_stop("dynamic target rebuild unavailable in GLES",0);}

#include "nvn_gl_read_buffer.inc"
#include "nvn_gl_resource.inc"
#include "nvn_gl_texture.inc"
#include "nvn_gl_buffer_sample.inc"
#include "nvn_gl_shader.inc"
#include "nvn_gl_classic.inc"
#include "nvn_gl_replay.inc"

void dm_vk_graphics_begin(void *mem,size_t size){(void)mem;(void)size;mtl_init();L.live=1;}
void dm_vk_graphics_end(void){mtl_finish();L.live=0;}
int dm_vk_graphics_live(void){return L.live;}
void *dm_vk_native_window(void){return L.window;}
void dm_vk_pipeline_cache_save(void){}
void dm_vk_program_capture(const NVNshaderData *s,int n){(void)s;(void)n;}
void mtl_set_window_origin_mode(int mode){if(mode!=0&&mode!=1)gl_stop("window origin",mode);L.origin=mode;}
void dm_vk_set_depth_mode(int mode){if(mode!=NVN_DEPTH_MODE_NEAR_IS_MINUS_W&&mode!=NVN_DEPTH_MODE_NEAR_IS_ZERO)gl_stop("depth mode",mode);L.depth_mode=mode;}
void dm_vk_set_layer_crop(int x,int y,int width,int height){
    if(x<0||y<0||width<0||height<0||width>INT32_MAX-x||height>INT32_MAX-y)gl_stop("VI crop bounds",width);
    mtl_init();gl_enter();L.vi_crop[0]=x;L.vi_crop[1]=y;L.vi_crop[2]=width;L.vi_crop[3]=height;gl_leave();
}
void dm_linux_menu_scene_changed(void){atomic_fetch_add(&L.menu_input_epoch,1);}
int dm_vk_index_full_uint32(void){return 1;}
int dm_vk_memory_get_stats(DmVkMemoryStats *out){(void)out;return ENOTSUP;}
int dm_vk_memory_trim_caches(unsigned flags,DmVkMemoryStats *out){(void)flags;(void)out;return ENOTSUP;}
void mtl_prog_release(prog_t *p){(void)p;}

void mtl_exec_rest(const uint64_t *h,int n){(void)h;(void)n;}
void mtl_set_frame(uint64_t frame){L.frame=frame;}
void mtl_stats(uint64_t *p,uint64_t *d){*p=L.passes;*d=L.draws;}
void mtl_pkg_stats(uint64_t *u,uint64_t *r){*u=L.uploads;*r=0;}
void mtl_create_stats(uint64_t *s,uint64_t *p){*s=L.compiles;*p=0;}
double mtl_gpu_ms(uint64_t f){(void)f;return -1;}
int mtl_gpu_span(uint64_t f,uint64_t *a,uint64_t *b){(void)f;(void)a;(void)b;return 0;}
int mtl_link_active(void){return 0;}
int mtl_link_wait_tick(uint64_t t){(void)t;return 0;}
int mtl_has_window(void){return L.window!=NULL;}
void mtl_xv_window_tex(uint64_t s){(void)s;}
void mtl_ub_present(double ms){(void)ms;}
void mtl_res_snapshot(mtl_res_count *out){memset(out,0,sizeof *out);out->shaders=L.nshader;out->live_tex=L.ntexture;out->pools=L.npool;out->pool_adds=L.pool_serial;}
void mtl_res_counts(uint32_t tick,const char *why){(void)tick;(void)why;}
void mtl_ui_ghost(int on,float a,float b,const float grid[4],float c,float d){(void)on;(void)a;(void)b;(void)grid;(void)c;(void)d;}
int mtl_ui_ghost_rect(float r[4]){(void)r;return 0;}
uint64_t mtl_host_latch(void){return 0;}
void mtl_host_apply(uint64_t v){(void)v;}
void bf_want_note(const char *key){(void)key;}
int mtl_display_hz(void){return 60;}
double mtl_display_refresh(void){return 60;}
double mtl_pace_hz(void){return 60;}
int mtl_warm_wait(int timeout){(void)timeout;return 1;}
void mtl_present_test_stall(uint64_t n){(void)n;}
uint64_t mtl_upload_ns_take(void){return 0;}
uint64_t mtl_upload_gpu_ns_take(void){return 0;}
uint64_t mtl_pkg_miss(void){return 0;}
uint64_t mtl_drawable_ns_take(void){return 0;}
uint64_t mtl_ds_omit(void){return 0;}
uint64_t mtl_udc_saved_bytes(void){return 0;}
void mtl_bw(uint64_t *a,uint64_t *b,uint64_t *c){*a=*b=*c=0;}
void mtl_args_stats(uint64_t *a,uint64_t *b){*a=*b=0;}
void mtl_use_stats(uint64_t *a,uint64_t *b){*a=*b=0;}

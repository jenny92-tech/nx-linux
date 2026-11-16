#include "../../host/hle.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include "aspect_policy.h"
#include "resolution_policy.h"

/* The host filesystem implements its own diagnostics and has no Horizon FSP
 * access-log service. Preserve original SetAllocator, but answer its SDK log
 * policy query locally instead of connecting to an absent service manager. */
HLE_FORCE("_ZN2nn2fs6detail18IsEnabledAccessLogEj", "Host filesystem has no Horizon access-log sink", int,
          soul_fs_access_log, unsigned target) {
    (void)target;return 0;
}
HLE_FORCE("_ZN2nn2fs6detail18IsEnabledAccessLogEv", "Host filesystem has no Horizon access-log sink", int,
          soul_fs_access_log_default, void) {
    return 0;
}

/* The host's existing arc4random_buf implementation uses Linux getrandom,
 * retries EINTR and fails closed. No Horizon csrng IPC session is required. */
void arc4random_buf(void *,size_t);
HLE_FORCE("_ZN2nn6crypto15InitializeCsrngEv", "Initialize the actual Linux CSPRNG backend", void,
          soul_crypto_init, void) {
    unsigned char readiness;arc4random_buf(&readiness,sizeof readiness);
}
HLE_FORCE("_ZN2nn6crypto36GenerateCryptographicallyRandomBytesEPvm", "Fill from the actual Linux CSPRNG", void,
          soul_crypto_bytes, void *output, size_t size) {
    if(!output && size)die("Null CSPRNG destination");
    arc4random_buf(output,size);
}

/* Settings language IDs/codes and module105 result codes follow the pinned
 * service ABI. Conversion is pure; no Horizon settings session is needed.
 * The first18 entries include both legacy Chinese and zh-Hans/zh-Hant. */
HLE_FORCE("_ZN2nn8settings6detail16MakeLanguageCodeEPNS0_12LanguageCodeENS0_8LanguageE", "Convert a settings language enum to its canonical 8-byte language code",Result,
          soul_make_language_code,void *out,int language) {
    static const char codes[][8]={"ja","en-US","fr","de","it","es","zh-CN","ko","nl","pt","ru","zh-TW","en-GB","fr-CA","es-419","zh-Hans","zh-Hant","pt-BR","pl","th"};
    if(!out)return (621u<<9)|105u;
    if((unsigned)language>=sizeof codes/sizeof codes[0])return (625u<<9)|105u;
    memcpy(out,codes[language],8);return RES_OK;
}
void soul_language_check(void) {
    char code[8];
    if(soul_make_language_code(code,15) || memcmp(code,"zh-Hans",8))die("language conversion Simplified Chinese");
    if(soul_make_language_code(code,16) || memcmp(code,"zh-Hant",8))die("language conversion Traditional Chinese");
    if(soul_make_language_code(code,1) || strcmp(code,"en-US"))die("language conversion English");
    if(soul_make_language_code(code,-1)!=((625u<<9)|105u) || soul_make_language_code(NULL,0)!=((621u<<9)|105u))die("language conversion invalid arguments");
    LOG("SOUL_LANGUAGE_CHECK passed standard codes and actual settings result values");
}

/* The SDK has counted and exact-read overloads. Host handles are checked file
 * descriptors, not SDK FileAccessor pointers; keep both overloads on the same
 * backend. Original SDK9.3 code at649d0/64a4c proves short-read Result177a02. */
Result dm_android_fs_read_exact(uint64_t,int64_t,void *,size_t);
HLE_FORCE("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm", "Read an exact byte count through the existing checked host filesystem",Result,
          soul_fs_read_file_exact,uint64_t handle,int64_t offset,void *buffer,size_t size) {
    return dm_android_fs_read_exact(handle,offset,buffer,size);
}
void soul_fs_exact_read_check(void) {
    const char *temp=getenv("DM_TEMP");char path[4096];
    if(!temp || snprintf(path,sizeof path,"%s/read-overload-XXXXXX",temp)>=(int)sizeof path)die("Exact read fixture temp path");
    int fd=mkstemp(path);if(fd<0)die("Exact read fixture create");
    if(unlink(path))die("Exact read fixture unlink");
    static const uint8_t data[8]={1,3,5,7,9,11,13,15};uint8_t buffer[4];
    if(write(fd,data,sizeof data)!=sizeof data || soul_fs_read_file_exact(fd,2,buffer,4) || memcmp(buffer,data+2,4))die("Exact read fixture bytes");
    if(soul_fs_read_file_exact(fd,6,buffer,4)!=0x177a02u)die("Exact read fixture original short-read result");
    close(fd);LOG("SOUL_FS_EXACT_READ_CHECK passed actual offset read and original SDK short-read result");
}

/* Horizon's font service is replaced by one openly licensed static CJK font.
 * Keep its verified bytes read-only for the process lifetime: Unity/FreeType
 * retain the returned address. Mapping once also avoids six copied fonts.
 * This substitutes glyphs, not Nintendo's exact metrics or private-use icons. */
static pthread_once_t soul_font_once=PTHREAD_ONCE_INIT;
static const uint8_t *soul_font_bytes;
static size_t soul_font_size;
static void soul_font_load(void) {
    const char *path=getenv("DM_SHARED_FONT");
    if(!path || !*path)die("Shared font path is required");
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
    if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<12 || st.st_size>64*1024*1024)die("Invalid shared font file");
    uint8_t head[12];
    if(pread(fd,head,sizeof head,0)!=sizeof head || memcmp(head,"OTTO",4))die("Expected pinned static OpenType CJK font");
    unsigned tables=((unsigned)head[4]<<8)|head[5];
    if(!tables || 12u+16u*tables>(uint64_t)st.st_size)die("Invalid shared font directory");
    void *map=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(map==MAP_FAILED)die("Shared font mmap failed");
    soul_font_bytes=map;soul_font_size=st.st_size;
    LOG("SOUL_SHARED_FONT mapped actual OpenType bytes=%zu read_only=1 host_replacement=1",soul_font_size);
}
static void soul_font_require(int type) {
    if((unsigned)type>=6)die("Unsupported shared font type=%d",type);
    if(pthread_once(&soul_font_once,soul_font_load))die("Shared font initialization failed");
}
HLE_FORCE("_ZN2nn2pl21RequestSharedFontLoadENS0_14SharedFontTypeE", "Load the real host shared font synchronously",void,
          soul_font_request,int type) { soul_font_require(type); }
HLE_FORCE("_ZN2nn2pl22GetSharedFontLoadStateENS0_14SharedFontTypeE", "Report loaded only after a real font is mapped",int,
          soul_font_state,int type) { soul_font_require(type);return 1; }
HLE_FORCE("_ZN2nn2pl20GetSharedFontAddressENS0_14SharedFontTypeE", "Return process-lifetime read-only OpenType bytes",const void *,
          soul_font_address,int type) { soul_font_require(type);return soul_font_bytes; }
HLE_FORCE("_ZN2nn2pl17GetSharedFontSizeENS0_14SharedFontTypeE", "Return the actual mapped OpenType byte size",size_t,
          soul_font_length,int type) { soul_font_require(type);return soul_font_size; }
void soul_shared_font_check(void) {
    soul_font_request(0);const void *address=soul_font_address(0);size_t length=soul_font_length(0);
    for(int type=0;type<6;type++)
        if(soul_font_state(type)!=1 || soul_font_address(type)!=address || soul_font_length(type)!=length)die("Shared font mapping lifetime/size mismatch");
    if(!address || length<12 || memcmp(address,"OTTO",4))die("Shared font actual OpenType bytes");
    LOG("SOUL_SHARED_FONT_CHECK passed actual immutable data and shared mapping for six types bytes=%zu",length);
}

/* Opt-in, source-bound request through this Unity player's own Screen API.
 * Static registration at main2314d34 binds Screen::SetResolution via the
 * RELATIVE slot4b258b0 to2312a30. Its managed caller12091c0 proves four int
 * arguments. No viewport/resource dimensions are patched behind Unity's back.
 * Run on the original nnMain thread, after player initialization; the engine
 * owns resize scheduling, dependent render targets and UI layout. */
static void soul_resolution_dimensions(int *width,int *height) {
    if(!soul_resolution_policy(getenv("DM_SOUL_ASPECT"),getenv("DM_SOUL_RENDER_SCALE"),width,height))
        die("Soul Knight requires aspect 4:3/16:9 and render scale 1/.75/.5");
}
void soul_aspect_prepare(void) {
    int width,height;soul_resolution_dimensions(&width,&height);
    if(width*3!=height*4)return;
    const char *enabled=getenv("DM_SOUL_LOW_RESOLUTION");if(!enabled||strcmp(enabled,"1"))return;
    module_t *m=&g_mod[0];const uint64_t at=0x1f75eb0;
    /* Called immediately after load/identity checks, before module init or SDK
     * threads. Unity's Switch ApplyResolution rejects h/w !=9/16 here. Admit
     * only the selected exact 4:3 mode in addition to original requests; keep resize
     * flags, size limits, render-target rebuild and camera/UI notifications.
     * x9 was the tolerance constant's address and is dead after its ldr. */
    static const uint32_t expected[]={0x2a170108,0x1e204c20,0xbd463521,0x1e212000,
        0x7a409904,0x54000061,0x2a1f03e0,0x14000026};
    if(g_nmod!=3||strcmp(m->name,"main")||strcmp(m->build_id,"02242ae4e90dcf439210fe87f692de05")||
       m->text_end<at+16||memcmp(m->base+at-16,expected,sizeof expected))die("Soul Knight aspect policy source differs");
    long page=sysconf(_SC_PAGESIZE);if(page<=0||((uint64_t)page&((uint64_t)page-1)))die("Soul Knight aspect page size");
    uint8_t *stub=MAP_FAILED;uint32_t branch=0;uintptr_t hints[]={(uintptr_t)m->base+m->size,(uintptr_t)m->base-(uintptr_t)page};
    for(unsigned i=0;i<2;i++){
        stub=mmap((void *)hints[i],(size_t)page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(stub==MAP_FAILED)continue;
        if(soul_aspect_branch((uintptr_t)m->base+at,(uintptr_t)stub,&branch))break;
        if(munmap(stub,(size_t)page))die("Soul Knight aspect unused stub unmap");stub=MAP_FAILED;
    }
    if(stub==MAP_FAILED||!soul_aspect_stub_size((uint32_t *)stub,(uintptr_t)stub,(uintptr_t)m->base+at+4,(unsigned)width,(unsigned)height))die("Soul Knight aspect branch reach");
    __builtin___clear_cache((char *)stub,(char *)stub+40);
    if(mprotect(stub,(size_t)page,PROT_READ|PROT_EXEC))die("Soul Knight aspect stub seal");
    void *site=m->base+at;void *site_page=(void *)((uintptr_t)site&~((uintptr_t)page-1));
    if(mprotect(site_page,(size_t)page,PROT_READ|PROT_WRITE))die("Soul Knight aspect startup text prepare");
    memcpy(site,&branch,4);__builtin___clear_cache((char *)site,(char *)site+4);
    if(mprotect(site_page,(size_t)page,PROT_READ|PROT_EXEC))die("Soul Knight aspect startup text seal");
    /* Process-lifetime RX stub; no live-code rewriting or RWX pages. */
    LOG("SOUL_ASPECT_POLICY extra=%dx%d original_modes=preserved hook=1f75eb0 startup_before_guest_init=1",width,height);
}
void soul_resolution_tick(void) {
    uint64_t soul_present_count(void);int soul_on_main_thread(void);
    static _Atomic int requested;
    const char *enabled=getenv("DM_SOUL_LOW_RESOLUTION");
    if(atomic_load(&requested) || !enabled || strcmp(enabled,"1") || !soul_on_main_thread() || soul_present_count()<360)return;
    int width,height;soul_resolution_dimensions(&width,&height);
    /* Let Unity resize its cameras, targets and UI together. Presentation
     * continues to fit the actual crop; it never stretches a 16:9 image. */
    if(g_nmod!=3 || strcmp(g_mod[0].name,"main") || strcmp(g_mod[0].build_id,"02242ae4e90dcf439210fe87f692de05") || g_mod[0].text_end<=0x2312a40)die("Soul Knight resolution title identity");
    static const uint8_t prologue[16]={0xf6,0x0f,0x1d,0xf8,0xf5,0x53,0x01,0xa9,0xf3,0x7b,0x02,0xa9,0xf3,0x03,0x03,0x2a};
    void *entry=(uint8_t *)g_mod[0].base+0x2312a30;
    void *registered=NULL;
    if(g_mod[0].size<0x4b258b8)die("Soul Knight Screen API slot bounds");
    memcpy(&registered,(uint8_t *)g_mod[0].base+0x4b258b0,sizeof registered);
    if(registered!=entry)die("Soul Knight Screen API registration target differs");
    if(memcmp(entry,prologue,sizeof prologue))die("Soul Knight Screen API prologue differs");
    if(atomic_exchange(&requested,1))return;
    LOG("SOUL_UNITY_RESOLUTION_REQUEST width=%d height=%d fullscreen_mode=1 refresh=0 original_main_thread=1 original_api=2312a30",width,height);
    ((void (*)(int,int,int,int))entry)(width,height,1,0);
    LOG("SOUL_UNITY_RESOLUTION_REQUEST returned; actual presentation extent still requires verification");
}

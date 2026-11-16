// INPUT: Real HLE save mounts, write/metadata changes and an injected durable-commit failure.
// OUTPUT: Exit stays blocked until every transaction has committed or rolled back.
// POS: Linux CPU fixture; shutdown is stubbed, game saves and devices are never opened.
#include "../../host/hle.h"
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
struct dm_flags g_flag;uint64_t g_t0_ns;
static unsigned checks,shutdowns;static int fail_commit;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"save exit failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
void dm_prof_add(int id,uint64_t ns,uint64_t bytes){(void)id;(void)ns;(void)bytes;}
void dm_guest_write_prepare(void *p,size_t n){(void)p;(void)n;}
int dm_custom_lang_explicit(void){return -1;}
/* The full Linux runtime supplies this through its OS service module. */
size_t strlcpy(char *out,const char *in,size_t capacity){size_t n=strlen(in);if(capacity){size_t copy=n<capacity-1?n:capacity-1;memcpy(out,in,copy);out[copy]=0;}return n;}
void dm_host_shutdown(const char *why){CHECK(strstr(why,"main-menu exit"));shutdowns++;}
int __real_dm_android_fs_commit_snapshot(const char *,const char *);
int __wrap_dm_android_fs_commit_snapshot(const char *a,const char *b){if(fail_commit){errno=EIO;return -1;}return __real_dm_android_fs_commit_snapshot(a,b);}
extern int dm_linux_try_menu_exit(void);
static void *binding(const char *name){extern const hle_force_ent __start_dm_force[],__stop_dm_force[];for(const hle_force_ent *p=__start_dm_force;p<__stop_dm_force;p++)if(!strcmp(p->name,name))return p->fn;CHECK(0);return NULL;}
int main(void){
    Result (*mount_save)(const char *,const void *)=binding("_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE");
    Result (*commit)(const char *)=binding("_ZN2nn2fs6CommitEPKc");
    Result (*create)(const char *,int64_t)=binding("_ZN2nn2fs10CreateFileEPKcl");
    Result (*mkdir_guest)(const char *)=binding("_ZN2nn2fs15CreateDirectoryEPKc");
    Result (*remove_guest)(const char *)=binding("_ZN2nn2fs10DeleteFileEPKc");
    Result (*remove_tree)(const char *)=binding("_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc");
    Result (*rename_guest)(const char *,const char *)=binding("_ZN2nn2fs10RenameFileEPKcS2_");
    Result (*open_guest)(uint64_t *,const char *,int)=binding("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
    Result (*write_guest)(uint64_t,int64_t,const void *,size_t,const void *)=binding("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE");
    Result (*resize)(uint64_t,int64_t)=binding("_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl");
    void (*close_guest)(uint64_t)=binding("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
    char root[]="/tmp/c197-menu-save-XXXXXX";CHECK(mkdtemp(root));char first[1024],second[1024];
    snprintf(first,sizeof first,"%s/first",root);snprintf(second,sizeof second,"%s/second",root);CHECK(!mkdir(first,0700)&&!mkdir(second,0700));
    CHECK(!setenv("DM_SAVE",first,1)&&!mount_save("one",NULL));CHECK(!setenv("DM_SAVE",second,1)&&!mount_save("two",NULL));
    CHECK(dm_linux_try_menu_exit()&&shutdowns==1);
    CHECK(!create("one:/a.bin",4));CHECK(!dm_linux_try_menu_exit());
    fail_commit=1;CHECK(commit("one"));CHECK(!dm_linux_try_menu_exit()&&shutdowns==1);
    fail_commit=0;CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit()&&shutdowns==2);
    uint64_t fd;CHECK(!open_guest(&fd,"one:/a.bin",3));CHECK(!dm_linux_try_menu_exit());
    CHECK(!resize(fd,8));close_guest(fd);CHECK(!dm_linux_try_menu_exit());CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit());
    CHECK(!open_guest(&fd,"one:/a.bin",3));CHECK(!write_guest(fd,0,"data",4,NULL));close_guest(fd);CHECK(!dm_linux_try_menu_exit());
    CHECK(!create("two:/other.bin",4));CHECK(!commit("two"));CHECK(!dm_linux_try_menu_exit());CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit());
    CHECK(!rename_guest("one:/a.bin","one:/b.bin"));CHECK(!dm_linux_try_menu_exit());CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit());
    CHECK(!remove_guest("one:/b.bin"));CHECK(!dm_linux_try_menu_exit());CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit());
    CHECK(!mkdir_guest("one:/nested"));CHECK(!dm_linux_try_menu_exit());CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit());
    CHECK(!remove_tree("one:/nested"));CHECK(!dm_linux_try_menu_exit());CHECK(!commit("one"));CHECK(dm_linux_try_menu_exit());
    printf("MENU_SAVE_EXIT_OK checks=%u shutdowns=%u metadata_resize_write_failed_commit_multi_mount=1\n",checks,shutdowns);
}

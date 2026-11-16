// INPUT: Explicit computer-prepared owned-content manifest and loaded main build.
// OUTPUT: Offline AOC index 1 only for the verified 94012 package and executable.
// POS: Linux service backend; no store, network, key access or implicit ownership.
#include "../host/hle.h"
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef DM_LINUX_GAME_94012
static pthread_once_t aoc_once=PTHREAD_ONCE_INIT;
static int aoc_count;
static void aoc_initialize(void) {
    const char *path=getenv("DM_LINUX_AOC_MANIFEST");
    if(!path)return;
    const module_t *main=NULL;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){main=&g_mod[i];break;}
    if(!main||strcmp(main->build_id,"db0507f36b84200ac05f75d76ed5026f"))
        die("owned-content manifest does not match the loaded game build");
    static const char expected[]="D2RAOC1 94012 db0507f36b84200ac05f75d76ed5026f 0100726014352000 0100726014353001 1 0ddedb30a6aac83e613de61f72e0c182cb989d9f3e6018132b207fba8d60c849\n";
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)die("cannot open owned-content manifest");
    struct stat st;char text[sizeof expected];
    int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)(sizeof expected-1);
    ssize_t n=valid?read(fd,text,sizeof text):-1;
    close(fd);
    if(!valid||n!=(ssize_t)(sizeof expected-1)||memcmp(text,expected,sizeof expected-1))
        die("unreviewed owned-content manifest");
    aoc_count=1;
    LOG("LINUX_AOC_READY build=94012 count=1 index=1 verified_metadata=1");
}
HLE_FORCE("_ZN2nn3aoc17CountAddOnContentEv", "Linux verified owned-content count", int, h_linux_aoc_Count, void) {
    pthread_once(&aoc_once,aoc_initialize);return aoc_count;
}
HLE_FORCE("_ZN2nn3aoc16ListAddOnContentEPiii", "Linux verified owned-content indices", int, h_linux_aoc_List, int *out, int off, int n) {
    pthread_once(&aoc_once,aoc_initialize);
    if(!aoc_count||!out||off!=0||n<=0)return 0;
    out[0]=1;return 1;
}
HLE_FORCE("_ZN2nn3aoc31GetAddOnContentListChangedEventEPNS_2os11SystemEventE", "Linux immutable owned-content change event", void, h_linux_aoc_GetEv, void *ev) {
    hle_bind_system_event(ev,0);
}

#endif

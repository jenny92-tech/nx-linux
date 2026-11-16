// INPUT: Synthetic local manifests and loaded-module identities in forked tests.
// OUTPUT: Ownership defaults, exact binding, list bounds and event behavior checked.
// POS: Linux host regression; no original files, network or cryptographic keys.
#include "../aoc.c"
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
module_t g_mod[MAX_MOD];
int g_nmod=1;
static int bound;
void hle_bind_system_event(void *event,int autoclear){assert(event==(void *)1);assert(!autoclear);bound++;}
static const char manifest[]="D2RAOC1 94012 db0507f36b84200ac05f75d76ed5026f 0100726014352000 0100726014353001 1 0ddedb30a6aac83e613de61f72e0c182cb989d9f3e6018132b207fba8d60c849\n";
int main(void){
    struct rlimit core={0,0};assert(!setrlimit(RLIMIT_CORE,&core));
    char path[]="/tmp/d2r-aoc-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    assert(write(fd,manifest,sizeof manifest-1)==sizeof manifest-1);assert(!close(fd));
    strcpy(g_mod[0].name,"main");strcpy(g_mod[0].build_id,"db0507f36b84200ac05f75d76ed5026f");
    for(int test=0;test<5;test++){
        pid_t pid=fork();assert(pid>=0);
        if(!pid){
            unsetenv("DM_LINUX_AOC_MANIFEST");
            if(test)setenv("DM_LINUX_AOC_MANIFEST",path,1);
            if(test==2)g_mod[0].build_id[0]='0';
            if(test==3)setenv("DM_LINUX_AOC_MANIFEST","/nonexistent/d2r-aoc",1);
            if(test==4){int output=open(path,O_WRONLY|O_APPEND);assert(output>=0);assert(write(output,"x",1)==1);close(output);}
            int expected=test?1:0,values[2]={-7,-9};
            assert(h_linux_aoc_Count()==expected);
            assert(h_linux_aoc_List(values,0,2)==expected);
            assert(values[0]==(expected?1:-7)&&values[1]==-9);
            assert(!h_linux_aoc_List(values,-1,2)&&!h_linux_aoc_List(values,1,2));
            assert(!h_linux_aoc_List(values,0,0)&&!h_linux_aoc_List(NULL,0,2));
            h_linux_aoc_GetEv((void *)1);assert(bound==1);_exit(0);
        }
        int status;assert(waitpid(pid,&status,0)==pid);
        if(test<2)assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
        else assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
    }
    assert(!unlink(path));puts("AOC manifest/default/build/list/event cases passed");return 0;
}

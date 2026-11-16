// INPUT: Synthetic, PID-scoped input commands and physical controller snapshots.
// OUTPUT: Expiry, replay rejection, real-input priority and symlink checks.
// POS: Host-only Linux diagnostic input test; never opens an input device.
#include "../input_control.h"
#include <assert.h>
static void command(char *line,size_t size,unsigned seq,unsigned mask,int lx,unsigned hold) {
    snprintf(line,size,"%llu %u %x %d 0 0 0 %u\n",(unsigned long long)getpid(),seq,mask,lx,hold);
}
int main(void) {
    dm_linux_test_input s={0};char line[256];
    command(line,sizeof line,1,1,0,300);
    assert(dm_linux_test_command(&s,line,100,0)==1&&s.active&&s.until==400&&s.buttons==1);
    assert(dm_linux_test_command(&s,line,101,0)==-1);
    command(line,sizeof line,2,0x900,0,1000);
    assert(dm_linux_test_command(&s,line,200,1)==0&&!s.active&&s.sequence==2);
    command(line,sizeof line,3,0x10000,0,100);
    assert(dm_linux_test_command(&s,line,200,0)==-1&&s.sequence==2);
    command(line,sizeof line,3,1,0,2001);
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    snprintf(line,sizeof line,"%llu 3 1 4294967296 0 0 0 100\n",(unsigned long long)getpid());
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    snprintf(line,sizeof line,"%llu 3 1 0 0 0 0 4294967396\n",(unsigned long long)getpid());
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    snprintf(line,sizeof line,"%llu 18446744073709551616 1 0 0 0 0 100\n",(unsigned long long)getpid());
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    command(line,sizeof line,3,1,32768,100);
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    snprintf(line,sizeof line,"%llu 3 1 0 0 0 0 100\n",(unsigned long long)getpid()+1);
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    command(line,sizeof line,3,1,0,100);strcat(line,"extra");
    assert(dm_linux_test_command(&s,line,200,0)==-1);
    char path[]="/tmp/d2r-input-test.XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    command(line,sizeof line,3,0x900,0,300);assert(write(fd,line,strlen(line))==(ssize_t)strlen(line));close(fd);
    uint64_t buttons=0;int32_t lx=0,ly=0,rx=0,ry=0;
    dm_linux_test_poll(&s,path,1000,&buttons,&lx,&ly,&rx,&ry);
    assert(buttons==0x900&&s.active);
    buttons=0;dm_linux_test_poll(&s,path,1300,&buttons,&lx,&ly,&rx,&ry);assert(!buttons&&!s.active);
    command(line,sizeof line,4,1,0,1000);fd=open(path,O_WRONLY|O_TRUNC);assert(fd>=0);
    assert(write(fd,line,strlen(line))==(ssize_t)strlen(line));close(fd);
    buttons=0;dm_linux_test_poll(&s,path,1400,&buttons,&lx,&ly,&rx,&ry);assert(buttons==1);
    buttons=2;dm_linux_test_poll(&s,path,1405,&buttons,&lx,&ly,&rx,&ry);assert(buttons==2&&!s.active);
    char linked[256];snprintf(linked,sizeof linked,"%s.link",path);assert(!symlink(path,linked));
    dm_linux_test_input other={0};buttons=0;
    dm_linux_test_poll(&other,linked,1000,&buttons,&lx,&ly,&rx,&ry);assert(!other.sequence&&!buttons);
    unlink(linked);unlink(path);puts("input control: PID, replay, bounds, expiry, physical priority, and symlink tests passed");
}

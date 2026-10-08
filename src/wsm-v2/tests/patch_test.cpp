#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static int protect_count=0, fail_at=0;
static int test_protect(void *p,size_t size,int prot) {
    ++protect_count;
    if (fail_at && protect_count==fail_at) return -1;
    return mprotect(p,size,prot);
}
#define WSM_PATCH_PROTECT test_protect
#include "../jni/wsm_patch.h"
#include "../jni/wsm_arm64_branch.h"
static int observed_permissions(void *map) {
    FILE *f=fopen("/proc/self/maps","r");assert(f);char line[512];int protection=-1;
    while(fgets(line,sizeof line,f)) {
        unsigned long long start,end;char flags[8];
        if(sscanf(line,"%llx-%llx %7s",&start,&end,flags)==3 && (uintptr_t)map>=start && (uintptr_t)map<end) {
            protection=(flags[0]=='r'?PROT_READ:0)|(flags[1]=='w'?PROT_WRITE:0)|(flags[2]=='x'?PROT_EXEC:0);break;
        }
    }
    fclose(f);assert(protection>=0);return protection;
}
int main() {
    uint32_t branch=0;
    assert(wsm_arm64_branch(0x10000000,0x18000000-4,branch)&&branch==0x15ffffff);
    assert(wsm_arm64_branch(0x18000000,0x10000000,branch)&&branch==0x16000000);
    assert(!wsm_arm64_branch(0x10000000,0x18000000,branch));
    assert(!wsm_arm64_branch(0x18000004,0x10000000,branch));
    assert(!wsm_arm64_branch(0x10000001,0x10000005,branch));
#ifndef WSM_TEST_TMPDIR
#define WSM_TEST_TMPDIR "/data/local/tmp"
#endif
    char filename[]=WSM_TEST_TMPDIR "/wsm-patch-unit-XXXXXX";
    int fd=mkstemp(filename);assert(fd>=0);
    const size_t ps=sysconf(_SC_PAGESIZE);assert(ftruncate(fd,ps)==0);
    uint32_t original[4]={0x11111111,0x22222222,0x33333333,0x44444444};
    uint32_t replacement[4]={0x55555555,0x66666666,0x77777777,0x88888888};
    assert(pwrite(fd,original,16,64)==16);
    void *maps[3];int perms[3]={PROT_READ|PROT_EXEC,PROT_READ,PROT_READ|PROT_WRITE};
    for(int i=0;i<3;++i){maps[i]=mmap(nullptr,ps,perms[i],MAP_PRIVATE,fd,0);assert(maps[i]!=MAP_FAILED);}
    int original_protection[3];
    for(int i=0;i<3;++i) {
        original_protection[i]=observed_permissions(maps[i]);
        assert(original_protection[i]&PROT_READ);
        assert(bool(original_protection[i]&PROT_WRITE)==bool(perms[i]&PROT_WRITE));
        if(original_protection[i]!=perms[i]) printf("INFO requested protection=%d, observed=%d\n",perms[i],original_protection[i]);
    }
    uintptr_t code=(uintptr_t)maps[0]+64;
    assert(patch_aliases("wsm-patch-unit-",code,replacement,0)==-1);
    assert(patch_aliases("wsm-patch-unit-",1,replacement,16)==-1);
    assert(patch_aliases("wsm-patch-unit-",code,replacement,16)==3);
    for(auto map:maps)assert(memcmp((char *)map+64,replacement,16)==0);
    assert(patch_aliases("wsm-patch-unit-",code,original,16)==3);
    void *near=wsm_near_page(code);assert(near!=MAP_FAILED);
    assert(wsm_arm64_branch(code,(uintptr_t)near,branch));
    assert(patch_aliases("wsm-patch-unit-",code,&branch,4)==3);
    for(auto map:maps){assert(*(uint32_t *)((char *)map+64)==branch);assert(memcmp((char *)map+68,original+1,12)==0);}
    assert(patch_aliases("wsm-patch-unit-",code,original,4)==3);
    munmap(near,ps);
    protect_count=0;fail_at=3; // Fail opening the second alias after the first was modified.
    assert(patch_aliases("wsm-patch-unit-",code,replacement,16)==-4);
    fail_at=0;
    for(auto map:maps)assert(memcmp((char *)map+64,original,16)==0);
    // Restore the actual baseline kernel mapping, which translators may expose
    // differently from requested guest PROT_EXEC. Never skip permission checks.
    for(int i=0;i<3;++i) assert(observed_permissions(maps[i])==original_protection[i]);
    for(auto map:maps)munmap(map,ps);close(fd);assert(unlink(filename)==0);
    puts("PASS branch: imm26 bounds/alignment, near allocation, atomic 4-byte patch, adjacent bytes unchanged");
    puts("PASS patch: 3 aliases, readback, original permissions, invalid range, forced partial failure rollback");
}

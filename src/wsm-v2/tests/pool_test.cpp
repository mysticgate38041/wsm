#include "../jni/trampoline_pool.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
alignas(16384) static unsigned char arena[3][16384];static bool fail=false;
static bool reachable(uintptr_t a,uintptr_t b){return a && b;}
static bool seal(void *,size_t){return !fail;}
static bool never(uintptr_t,uintptr_t){return false;}
static bool real_seal(void *p,size_t n){return mprotect(p,n,PROT_READ|PROT_EXEC)==0;}
int main() {
    wsm::TrampolinePool pool(arena,16384,3,4096,nullptr,seal);
    assert(!pool.reserve(0,reachable));auto *one=pool.reserve(4,reachable);assert(one==arena[0]);
    assert(!pool.seal(one,4097));assert(!pool.seal(arena[2],4));fail=true;assert(!pool.seal(one,4));fail=false;
    assert(pool.seal(one,4));assert(!pool.seal(one,4));assert(pool.retained()==1&&pool.committed_bytes()==4096);
    assert(pool.reserve(4,reachable)==arena[1]);assert(pool.reserve(4,reachable)==arena[2]);assert(!pool.reserve(4,reachable));
    wsm::TrampolinePool denied(arena,16384,3,4096,nullptr,seal);assert(!denied.reserve(4,never));assert(denied.retained()==0);
    wsm::TrampolinePool invalid(arena,16384,513,4096,nullptr,seal);assert(!invalid.reserve(4,reachable));
    const size_t n=static_cast<size_t>(sysconf(_SC_PAGESIZE));void *page=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(page!=MAP_FAILED);wsm::TrampolinePool actual(page,n,1,n,nullptr,real_seal);
    auto *owned=actual.reserve(4,reachable);assert(owned==page);*static_cast<uint32_t *>(owned)=0;
    assert(actual.seal(owned,4));assert(!actual.seal(owned,4));assert(!munmap(page,n));
    puts("pool: retained allocation, bounds, failure and RX sealing passed");
}

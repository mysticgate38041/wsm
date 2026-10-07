#pragma once
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
inline bool wsm_arm64_branch(uintptr_t from, uintptr_t to, uint32_t &word) {
    int64_t delta = (int64_t)to - (int64_t)from;
    if (((from | to) & 3) || (delta & 3) || delta < -134217728LL || delta > 134217724LL) return false;
    word = 0x14000000u | ((uint32_t)(delta / 4) & 0x03ffffffu); return true;
}
// A hint with NOREPLACE never uses MAP_FIXED; old kernels may ignore the flag,
// in which case an unexpected result is unmapped rather than overwriting memory.
inline void *wsm_near_page(uintptr_t code) {
    const uintptr_t ps = (uintptr_t)sysconf(_SC_PAGESIZE);
    if (!ps || (ps & (ps - 1)) || code < 134217728ULL) return MAP_FAILED;
    const uintptr_t low = (code - 134217728ULL + ps - 1) & ~(ps - 1);
    const uintptr_t high = (code + 134217724ULL) & ~(ps - 1);
    FILE *f = fopen("/proc/self/maps", "r"); if (!f) return MAP_FAILED;
    char line[512]; uintptr_t previous = low; void *result = MAP_FAILED;
    while (fgets(line, sizeof line, f)) {
        unsigned long long start=0,end=0;
        if (sscanf(line,"%llx-%llx",&start,&end)!=2) continue;
        if (end <= low) continue;
        uintptr_t gap_end = start < high ? (uintptr_t)start : high;
        uintptr_t candidate = (previous + ps - 1) & ~(ps - 1);
        if (candidate <= high && gap_end >= candidate && gap_end - candidate >= ps) {
            uint32_t branch;
            if (wsm_arm64_branch(code,candidate,branch)) {
                void *page=mmap((void *)candidate,ps,PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|0x100000,-1,0);
                if (page==(void *)candidate) { result=page; break; }
                if (page!=MAP_FAILED) munmap(page,ps);
            }
        }
        if (end > previous) previous=(uintptr_t)end;
        if (previous >= high) break;
    }
    fclose(f); return result;
}

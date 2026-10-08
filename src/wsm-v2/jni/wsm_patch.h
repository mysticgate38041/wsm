#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#ifndef WSM_PATCH_LOG
#define WSM_PATCH_LOG(...) ((void)0)
#endif
#ifndef WSM_PATCH_PROTECT
#define WSM_PATCH_PROTECT mprotect
#endif
// Permission-preserving alias transaction; caller owns patch serialization.
[[maybe_unused]] static bool wsm_patch_readable(uintptr_t address, size_t len, const char *mapname) {
    FILE *file = fopen("/proc/self/maps", "r");
    if (!file) return false;
    char line[512]; bool found = false;
    while (fgets(line, sizeof line, file)) {
        unsigned long long start = 0, end = 0; char perms[8] = {};
        if (strstr(line, mapname) && sscanf(line, "%llx-%llx %7s", &start, &end, perms) == 3 &&
            perms[0] == 'r' && address >= start && address < end && len <= end - address) { found = true; break; }
    }
    fclose(file); return found;
}
static int patch_aliases(const char *mapname, uintptr_t code, const uint32_t *bytes, int len) {
    uintptr_t ms[64] = {}, me[64] = {}, mo[64] = {};
    int nmaps = 0;
    int protection[64] = {};
    FILE *mf = fopen("/proc/self/maps", "r");
    if (mf) {
        char line[512];
        while (fgets(line, sizeof line, mf) && nmaps < 64) {
            if (!strstr(line, mapname)) continue;
            unsigned long long s = 0, e = 0, off = 0;
            char perms[8] = {};
            if (sscanf(line, "%llx-%llx %7s %llx", &s, &e, perms, &off) != 4) continue;
            ms[nmaps] = (uintptr_t)s; me[nmaps] = (uintptr_t)e; mo[nmaps] = (uintptr_t)off;
            protection[nmaps] = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) | (perms[2] == 'x' ? PROT_EXEC : 0);
            nmaps++;
        }
        fclose(mf);
    }
    uintptr_t F = 0; bool found = false;
    for (int i = 0; i < nmaps; i++)
        if (code >= ms[i] && code < me[i]) { F = (code - ms[i]) + mo[i]; found = true; break; }
    if (!found || len <= 0 || len > 24) return -1;
    WSM_PATCH_LOG("aliases: code=0x%llx F=0x%llx nmaps=%d", (unsigned long long)code, (unsigned long long)F, nmaps);
    for (int i = 0; i < nmaps; i++)
        WSM_PATCH_LOG("aliases: [%d] 0x%llx-0x%llx off=0x%llx", i, (unsigned long long)ms[i],
             (unsigned long long)me[i], (unsigned long long)mo[i]);
    // Capture all aliases before modifying any of them (shared mappings may mirror writes).
    uintptr_t targets[64] = {}, pages[64] = {};
    size_t spans[64] = {};
    int prots[64] = {};
    uint8_t backup[64][24] = {};
    int count = 0;
    const uintptr_t ps = (uintptr_t)sysconf(_SC_PAGESIZE);
    if (!ps || (ps & (ps - 1))) return -3;
    for (int i = 0; i < nmaps; i++) {
        if (F < mo[i]) continue;
        uintptr_t rel = F - mo[i];
        if (rel > me[i] - ms[i] || (uintptr_t)len > me[i] - ms[i] - rel) continue;
        if (!(protection[i] & PROT_READ)) return -3;
        uintptr_t a = ms[i] + rel;
        targets[count] = a; pages[count] = a & ~(ps - 1);
        spans[count] = ((a + len + ps - 1) & ~(ps - 1)) - pages[count];
        prots[count] = protection[i];
        memcpy(backup[count], (void *)a, len);
        ++count;
    }
    if (!count) return -1;
    int attempted = 0;
    bool failed = false;
    for (int i = 0; i < count; ++i) {
        attempted = i + 1;
        if (WSM_PATCH_PROTECT((void *)pages[i], spans[i], prots[i] | PROT_WRITE) != 0) { failed = true; break; }
        if (len==4 && !(targets[i]&3)) __atomic_store_n((uint32_t *)targets[i],bytes[0],__ATOMIC_RELEASE);
        else memcpy((void *)targets[i], bytes, len);
        __builtin___clear_cache((char *)targets[i], (char *)targets[i] + len);
        bool matches = memcmp((void *)targets[i], bytes, len) == 0;
        if (WSM_PATCH_PROTECT((void *)pages[i], spans[i], prots[i]) != 0 || !matches) { failed = true; break; }
    }
    if (!failed) return count;
    bool rolled_back = true;
    for (int i = attempted - 1; i >= 0; --i) {
        if (WSM_PATCH_PROTECT((void *)pages[i], spans[i], prots[i] | PROT_WRITE) != 0) { rolled_back = false; continue; }
        if (len==4 && !(targets[i]&3)) { uint32_t value; memcpy(&value,backup[i],4); __atomic_store_n((uint32_t *)targets[i],value,__ATOMIC_RELEASE); }
        else memcpy((void *)targets[i], backup[i], len);
        __builtin___clear_cache((char *)targets[i], (char *)targets[i] + len);
        if (memcmp((void *)targets[i], backup[i], len) != 0) rolled_back = false;
        if (WSM_PATCH_PROTECT((void *)pages[i], spans[i], prots[i]) != 0) rolled_back = false;
    }
    WSM_PATCH_LOG("alias transaction failed rollback=%d", rolled_back);
    return rolled_back ? -4 : -5;
}

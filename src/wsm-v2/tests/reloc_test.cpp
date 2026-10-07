#include "../jni/wsm_arm64_reloc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
static int64_t target(uint32_t w, uintptr_t pc) {
    uint32_t bits = ((w >> 29) & 3u) | (((w >> 5) & 0x7ffffu) << 2);
    int64_t imm = (bits & 0x100000u) ? (int64_t)bits - 0x200000 : bits;
    return (w & 0x80000000u) ? (int64_t)(pc & ~uintptr_t(4095)) + imm*4096 : (int64_t)pc + imm;
}
static uint32_t encode(bool page, int32_t delta, unsigned reg) {
    uint32_t bits = (uint32_t)delta & 0x1fffffu;
    return (page ? 0x90000000u : 0x10000000u) | ((bits & 3u) << 29) | ((bits >> 2) << 5) | reg;
}
int main() {
    uint32_t out = 0, roundtrip = 0;
    for (bool page : {false, true}) {
        for (int32_t imm : {-1048576,-1048575,-4096,-1,0,1,4095,1048574,1048575}) {
            for (unsigned reg = 0; reg < 32; ++reg) {
                uint32_t w = encode(page, imm, reg);
                const uintptr_t from = 0x108c86d64ULL;
                for (int displacement : {-65536,-4096,0,4096,65536}) {
                    uintptr_t to = from + displacement;
                    bool ok = wsm_arm64_relocate_word(w, from, to, out);
                    int64_t delta = target(w, from) - (page ? (int64_t)(to & ~uintptr_t(4095)) : (int64_t)to);
                    int64_t adjusted = page ? delta / 4096 : delta;
                    assert(ok == (adjusted >= -1048576 && adjusted <= 1048575));
                    if (ok) {
                        assert(target(out,to) == target(w,from) && (out & 31u) == reg);
                        assert(wsm_arm64_relocate_word(out,to,from,roundtrip) && roundtrip == w);
                    }
                }
            }
        }
    }
    // Actual Guardian Tales critical getter prologue, including its fourth ADRP.
    uint32_t critical[4] = {0xfc1e0fe8u,0xf90007feu,0xa9014ff4u,0xf000af94u};
    uint32_t relocated[4]{};
    for (uintptr_t to : {uintptr_t(0x88c86d58),uintptr_t(0x98c86d58)}) {
        assert(wsm_arm64_relocate_prologue(critical,0x8c86d58,to,relocated));
        assert(memcmp(critical,relocated,12) == 0);
        assert(target(critical[3],0x8c86d64) == target(relocated[3],to+12));
    }
    for (uint32_t unsafe : {0x58000000u,0x18000000u,0x1c000000u,0x9c000000u,0x98000000u,
        0x14000000u,0x94000000u,0x54000000u,0x54000010u,0x34000000u,0x35000000u,0x36000000u,0x37000000u,0xd61f0000u,0xd63f0000u}) {
        out = 0xdeadbeefu;
        assert(!wsm_arm64_relocate_word(unsafe,0x100000,0x110000,out) && out == 0xdeadbeefu);
    }
    assert(!wsm_arm64_relocate_word(encode(false,0,0),0x100000,0x300000,out));
    assert(!wsm_arm64_relocate_word(encode(true,0,0),0x100000000,0x300000000,out));
    assert(!wsm_arm64_relocate_word(0xd503201f,1,4,out));
    uint32_t failed[4]={1,2,3,4},before[4];memcpy(before,failed,16);
    critical[3]=0x94000000u;
    assert(!wsm_arm64_relocate_prologue(critical,0x100000,0x110000,failed) && memcmp(failed,before,16)==0);
    puts("PASS relocation: ADR/ADRP target conservation, signed boundaries, registers, roundtrip, actual GT critical prologue, unsafe literals/branches, atomic refusal");
}

#pragma once
#include <stdint.h>
#include <stddef.h>

// Only address-generation instructions are relocated. Control flow and literal
// loads require a different emitter and are deliberately refused here.
inline bool wsm_arm64_address_instruction(uint32_t w) {
    return (w & 0x1f000000u) == 0x10000000u; // ADR and ADRP
}
inline bool wsm_arm64_copy_supported(uint32_t w) {
    if (wsm_arm64_address_instruction(w)) return true;
    if ((w & 0x3b000000u) == 0x18000000u) return false; // all LDR literal/PRFM variants
    if ((w & 0x7c000000u) == 0x14000000u) return false; // B/BL
    if ((w & 0xff000000u) == 0x54000000u) return false; // B.cond / BC.cond
    if ((w & 0x7e000000u) == 0x34000000u || (w & 0x7e000000u) == 0x36000000u) return false; // CB/TB
    if ((w & 0xfffffc1fu) == 0xd61f0000u || (w & 0xfffffc1fu) == 0xd63f0000u) return false; // BR/BLR
    return true;
}
inline bool wsm_arm64_relocate_word(uint32_t w, uintptr_t from, uintptr_t to, uint32_t &out) {
    if ((from | to) & 3u || !wsm_arm64_copy_supported(w)) return false;
    if (!wsm_arm64_address_instruction(w)) { out = w; return true; }
    const uint32_t encoded = ((w >> 29) & 3u) | (((w >> 5) & 0x7ffffu) << 2);
    const int64_t imm = (encoded & 0x100000u) ? (int64_t)encoded - 0x200000LL : encoded;
    const bool page = (w & 0x80000000u) != 0;
    const int64_t source = page ? (int64_t)(from & ~uintptr_t(4095)) : (int64_t)from;
    const int64_t destination = page ? (int64_t)(to & ~uintptr_t(4095)) : (int64_t)to;
    const int64_t delta = source + imm * (page ? 4096LL : 1LL) - destination;
    if (page && delta % 4096) return false;
    const int64_t adjusted = page ? delta / 4096 : delta;
    if (adjusted < -1048576LL || adjusted > 1048575LL) return false;
    const uint32_t bits = (uint32_t)adjusted & 0x1fffffu;
    out = (w & ~0x60ffffe0u) | ((bits & 3u) << 29) | ((bits >> 2) << 5);
    return true;
}
inline bool wsm_arm64_relocate_prologue(const uint32_t *original, uintptr_t from, uintptr_t to, uint32_t *out) {
    if (!original || !out) return false;
    uint32_t temporary[4];
    for (size_t i = 0; i < 4; ++i)
        if (!wsm_arm64_relocate_word(original[i], from + 4*i, to + 4*i, temporary[i])) return false;
    for (size_t i = 0; i < 4; ++i) out[i] = temporary[i];
    return true; // Never publish a partially relocated sequence.
}

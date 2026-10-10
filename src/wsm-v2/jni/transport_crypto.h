#pragma once
/* WSM keyed command transport (v6.4).
   The file channel can be read by anything running as the game's uid (the
   game itself, its child processes, any injected daemon). Frames are therefore
   obfuscated with a keystream derived from the per-process nonce that only the
   root tooling can read (module dir, 0600). Legacy plaintext frames remain
   accepted for compatibility; encrypted frames carry the "E1:" magic. */
#include <stddef.h>
#include <stdint.h>

namespace wsm {
/* One splitmix64 step: advance state and return the next 64-bit value. */
uint64_t next_splitmix(uint64_t &state);
/* XOR buffer with the byte stream derived from the nonce (schedule shared
   with scripts/wsmctl.py; locked vector in tests/transport_test.cpp). */
void xor_stream(uint64_t nonce, uint8_t *buffer, size_t length);
/* Decode "E1:<hex>" into `out` (NUL-terminated). False on prefix/hex/size/NUL
   violations. */
bool transport_decode(uint64_t nonce, const char *frame, char *out, size_t cap);
/* Encode `plain` as "E1:<hex>". False when the output buffer is too small. */
bool transport_encode(uint64_t nonce, const char *plain, char *out, size_t cap);
} // namespace wsm

#include "transport_crypto.h"
#include <string.h>

namespace wsm {

uint64_t next_splitmix(uint64_t &state) {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void xor_stream(uint64_t nonce, uint8_t *buffer, size_t length) {
    uint64_t state = nonce;
    for (size_t i = 0; i < length; ++i) {
        buffer[i] ^= static_cast<uint8_t>(next_splitmix(state) & 0xFFu);
    }
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool transport_decode(uint64_t nonce, const char *frame, char *out, size_t cap) {
    if (!frame || !out || cap == 0) return false;
    if (strncmp(frame, "E1:", 3) != 0) return false;
    const char *hex = frame + 3;
    const size_t hexlen = strlen(hex);
    if (hexlen == 0 || (hexlen & 1u) != 0 || hexlen / 2 + 1 > cap) return false;
    if (hexlen / 2 > 544) return false;
    uint8_t tmp[544];
    const size_t bytes = hexlen / 2;
    for (size_t i = 0; i < bytes; ++i) {
        const int hi = hex_value(hex[2 * i]);
        const int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        tmp[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    xor_stream(nonce, tmp, bytes);
    for (size_t i = 0; i < bytes; ++i) {
        if (tmp[i] == 0) return false; /* commands are NUL-free text */
    }
    memcpy(out, tmp, bytes);
    out[bytes] = '\0';
    return true;
}

bool transport_encode(uint64_t nonce, const char *plain, char *out, size_t cap) {
    if (!plain || !out) return false;
    const size_t length = strlen(plain);
    if (length == 0 || length > 4096) return false;
    if (cap < 3 + 2 * length + 1) return false;
    uint8_t tmp[4096];
    memcpy(tmp, plain, length);
    xor_stream(nonce, tmp, length);
    static const char hexd[] = "0123456789abcdef";
    out[0] = 'E'; out[1] = '1'; out[2] = ':';
    for (size_t i = 0; i < length; ++i) {
        out[3 + 2 * i] = hexd[tmp[i] >> 4];
        out[3 + 2 * i + 1] = hexd[tmp[i] & 15];
    }
    out[3 + 2 * length] = '\0';
    return true;
}

} // namespace wsm

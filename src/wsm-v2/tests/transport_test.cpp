#include "../jni/transport_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    const uint64_t nonce = 0x0123456789ABCDEFull;
    char frame[256];
    char back[128];

    /* roundtrip */
    assert(wsm::transport_encode(nonce, "@42 gm all 1", frame, sizeof frame));
    assert(strncmp(frame, "E1:", 3) == 0);
    assert(wsm::transport_decode(nonce, frame, back, sizeof back));
    assert(strcmp(back, "@42 gm all 1") == 0);

    /* locked interoperability vector with scripts/wsmctl.py */
    assert(strcmp(frame, "E1:dda78ccc6f1f2530778b2fdd") == 0);

    /* determinism */
    char frame2[256];
    assert(wsm::transport_encode(nonce, "@42 gm all 1", frame2, sizeof frame2));
    assert(strcmp(frame, frame2) == 0);

    /* wrong key never yields the original plaintext */
    char wrong[128];
    if (wsm::transport_decode(nonce ^ 1ull, frame, wrong, sizeof wrong)) {
        assert(strcmp(wrong, "@42 gm all 1") != 0);
    }

    /* negatives */
    assert(!wsm::transport_decode(nonce, "@42 gm all 1", back, sizeof back));
    assert(!wsm::transport_decode(nonce, "E1:", back, sizeof back));
    assert(!wsm::transport_decode(nonce, "E1:zz", back, sizeof back));
    assert(!wsm::transport_decode(nonce, "E1:abc", back, sizeof back));
    assert(!wsm::transport_encode(nonce, "", frame, sizeof frame));
    assert(!wsm::transport_encode(nonce, "@42 gm all 1", frame, 3));
    puts("PASS transport: keyed channel roundtrip, locked vector, tamper and bounds");
}

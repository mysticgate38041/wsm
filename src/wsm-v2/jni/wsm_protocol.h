// WSM v2 protocol — shared between loader (host) and engine (guest).
// POD only; fixed-width types; no pointers cross this channel.
#pragma once
#include <stdint.h>

#define WSM_PROTOCOL_VERSION 3u
#define WSM_BUILD_STAMP "wsm-v6.2.0-rc1"
#define WSM_CHANNEL_MAP_SIZE 4096u

#define WSM_OFF_HELLO 0u
#define WSM_OFF_ACK 128u
#define WSM_OFF_PROBE 320u

#define WSM_HELLO_MAGIC 0x574D5348u /* "WSMH" */
#define WSM_ACK_MAGIC 0x574D5341u   /* "WSMA" */
#define WSM_PROBE_MAGIC 0x574D5350u /* "WSMP" */

/* engine ack.state */
#define WSM_STATE_READY 1u
#define WSM_STATE_DUPLICATE 2u

/* engine ack.caps bits */
#define WSM_CAP_LIBIL2CPP_SEEN 0x1u
#define WSM_CAP_DLOPEN_OK 0x2u
#define WSM_CAP_SYMBOLS_ALL 0x4u
#define WSM_CAP_PROBE_DONE 0x8u
#define WSM_CAP_FIXTURE_CALLBACK_OK 0x10u
#define WSM_CAP_FIXTURE_CLASS_ABSENT 0x20u
#define WSM_CAP_ELF_PARSE_OK 0x40u
#define WSM_CAP_QUERY_OK 0x80u

/* probe.stage */
#define WSM_PROBE_STAGE_ABSENT 1u
#define WSM_PROBE_STAGE_REPORT 2u

typedef struct {
    uint32_t magic;      /* WSM_HELLO_MAGIC when loader staged */
    uint32_t protocol;   /* WSM_PROTOCOL_VERSION */
    uint32_t loader_pid;
    uint32_t target_uid;
    uint64_t nonce;      /* session nonce, echoed by engine */
    char build[32];      /* loader build stamp */
    char abi[16];        /* loader abi string */
    uint32_t stage;      /* 1 = hello written */
} wsm_hello_t;

typedef struct {
    uint32_t magic;      /* WSM_ACK_MAGIC */
    uint32_t protocol;   /* echoed */
    uint64_t nonce_echo; /* echoed hello nonce */
    uint32_t engine_pid;
    uint32_t engine_uid;
    uint32_t state;      /* WSM_STATE_* */
    uint32_t caps;       /* WSM_CAP_* bitmask */
    char build[32];      /* engine build stamp */
    char abi[16];        /* engine abi string */
    char note[64];
} wsm_ack_t;

typedef struct {
    uint32_t magic;       /* WSM_PROBE_MAGIC, written last */
    uint32_t stage;       /* WSM_PROBE_STAGE_* */
    uint64_t il2cpp_base; /* 0 when absent */
    uint64_t ts_ms;
    char text[512];
} wsm_probe_t;

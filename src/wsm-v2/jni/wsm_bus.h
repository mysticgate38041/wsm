#pragma once
#include <stdint.h>
#define WSM_BUS_WORDS 160
#define WSM_HOOK_SLOTS 32
#define WSM_BUS_MAGIC 0x57534D3642555301ULL
#define WSM_BUS_PROTOCOL 3ULL
#define WSM_BUS_IDENTITY 150
#define WSM_BUS_VERSION 151
#define WSM_BUS_PID 152
#define WSM_BUS_NONCE 153
#define WSM_PAYLOAD_REL "payload/arm64-v8a.so"
// Independent main-thread mailbox. Agent commands never acknowledge this work.
#define WSM_MAIN_REQUEST 130
#define WSM_MAIN_ACK 131
#define WSM_MAIN_STATE 132
#define WSM_MAIN_STAGE 133
#define WSM_MAIN_ALLOWED 134
#define WSM_MAIN_TID 135
#define WSM_MAIN_APPLIED 136
#define WSM_MAIN_SKIPPED 137
#define WSM_MAIN_API 138
#define WSM_MAIN_HOOKED 139

// Lower 32 bits: shared futex generation. Upper word remains reserved.
#define WSM_BUS_EVENT 144

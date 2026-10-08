/* WSM 6 ARM64 payload — agent thread with authenticated in-process bus.
 * Constructor receives the bus address via loader-owned process environment.
 * Thread: heartbeat bus[10]++, layani command: 1=ping, 2=mul a*b, 9=exit.
 * NO libc++_shared. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <android/log.h>
#include <stdlib.h>
#include "../jni/wsm_bus.h"
#include "../jni/shared_bus_event.h"
#include "../jni/trampoline_pool.h"
#include "../jni/wsm_protocol.h"
#include "../jni/wsm_arm64_branch.h"
#include "../jni/wsm_arm64_reloc.h"
#include "../jni/wsm_sweep.h"

#define LOG_TAG "WSM-H64"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#if defined(__aarch64__)
#define H64_ARCH "aarch64"
#else
#define H64_ARCH "x86_64"
#endif

#define H64_BUS_MAGIC 0xA864CAFEULL
#define H64_THREAD_MAGIC 0x74EAD001ULL

static volatile uint64_t *g_bus = nullptr;

extern "C" __attribute__((visibility("default"))) int h64_magic(void); /* fwd (dipatch PATCHSELF) */
extern "C" __attribute__((visibility("default"))) int h64_victim(void); /* korban ke-2 (belum pernah dipanggil) */

volatile uint32_t g_hotcount = 0;      /* POC-4: counter yg di-increment via trampoline game */

/* --- POC-5: page-guard watchpoint — jebakan SIGSEGV di field HP monster --- */
#include <signal.h>
#include <ucontext.h>
static uintptr_t g_guard_target = 0;
static volatile int g_guard_hits = 0;
static volatile uintptr_t g_guard_last_pc = 0, g_guard_last_lr = 0, g_guard_last_x0 = 0, g_guard_last_addr = 0;
static volatile int g_guard_active = 0;
static struct sigaction g_old_segv;
static int g_segv_installed = 0;

static void wsm_segv(int sig, siginfo_t *si, void *ucv) {
    uintptr_t fa = (uintptr_t)si->si_addr;
    uintptr_t page = g_guard_target & ~(uintptr_t)0xFFF;
    if (g_guard_active && (fa & ~(uintptr_t)0xFFF) == page) {
        /* halaman target: buka RW selamanya + auto-disarm (hindari badai fault), log kalau kena field kita */
        mprotect((void *)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
        g_guard_active = 0;
        if (fa >= g_guard_target && fa < g_guard_target + 8) {
            ucontext_t *uc = (ucontext_t *)ucv;
            g_guard_last_pc = uc->uc_mcontext.pc;
            g_guard_last_lr = uc->uc_mcontext.regs[30];
            g_guard_last_x0 = uc->uc_mcontext.regs[0];
            g_guard_last_addr = fa;
            g_guard_hits++;
            LOGI("GUARD HIT #%d pc=0x%llx lr=0x%llx x0=0x%llx addr=0x%llx",
                 g_guard_hits, (unsigned long long)g_guard_last_pc,
                 (unsigned long long)g_guard_last_lr, (unsigned long long)g_guard_last_x0,
                 (unsigned long long)fa);
        } else {
            LOGI("guard: page fault (bukan field) addr=0x%llx -> RW+dilepas", (unsigned long long)fa);
        }
        return;
    }
    /* bukan halaman kita: pasang balik handler lama lalu re-raise (jangan panggil langsung!) */
    if (g_old_segv.sa_sigaction || g_old_segv.sa_handler) {
        sigaction(SIGSEGV, &g_old_segv, nullptr);
    }
    raise(sig);
}

static void guard_install_once(void) {
    if (g_segv_installed) return;
    struct sigaction sa = {};
    sa.sa_sigaction = wsm_segv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    g_segv_installed = 1;
    LOGI("guard: SIGSEGV handler installed (old=%p flags=0x%x)", (void *)g_old_segv.sa_sigaction, g_old_segv.sa_flags);
}

/* --- POC-4 helpers v3: MULTI-SLOT absolute-jump hooks --- */
static int patch_aliases(const char *mapname, uintptr_t code, const uint32_t *bytes, int len); /* fwd */
static bool wsm_patch_readable(uintptr_t address, size_t len, const char *mapname);
static int restore_hook(int slot, const char *mapname);
#define NSLOT WSM_HOOK_SLOTS
static uint32_t g_hook_orig[NSLOT][4] = {};
static uint32_t *g_hookblock[NSLOT] = {};
static uintptr_t g_hook_code2[NSLOT] = {};
static uint32_t g_patch_word[NSLOT] = {};
static size_t g_patch_len[NSLOT] = {};

// Linker-owned, page-isolated BSS survives native-bridge address reservations.
// Every published page remains immutable until process exit.
alignas(16384) static uint8_t g_owned_trampolines[512][16384];
static bool pool_reachable(uintptr_t from,uintptr_t to){uint32_t branch;return wsm_arm64_branch(from,to,branch);}
static void *pool_allocate(uintptr_t target){void *p=wsm_near_page(target);return p==MAP_FAILED?nullptr:p;}
static bool pool_seal(void *page,size_t length){return mprotect(page,length,PROT_READ|PROT_EXEC)==0;}
static wsm::TrampolinePool &trampoline_pool(){
    static wsm::TrampolinePool pool(g_owned_trampolines,16384,512,
        static_cast<size_t>(sysconf(_SC_PAGESIZE)),pool_allocate,pool_seal);return pool;
}
static uintptr_t g_hook_orig_addr[NSLOT] = {};  /* v28: alamat pemilik orig tiap slot */
volatile uint32_t g_hotcount_arr[NSLOT] = {};

// Serialize installs on the agent. Restore an owned slot before reuse and retain
// each published RX page until process exit, including after a successful OFF.
static int install_prepare(int slot, uintptr_t code, const char *mapname="libil2cpp.so") {
    if (slot < 0 || slot >= NSLOT || !code || !wsm_patch_readable(code,16,mapname)) return -1;
    if (g_hook_code2[slot] && restore_hook(slot,mapname) < 0) return -11;
    if(trampoline_pool().retained()>=512) return -12;
    if((size_t)sysconf(_SC_PAGESIZE)>16384) return -10;
    void *page=trampoline_pool().reserve(code,pool_reachable);
    if(!page) return -10;
    g_hookblock[slot]=(uint32_t *)page;
    uint32_t current[4]; memcpy(current,(void *)code,16);
    if (current[0]==0x58000051u && current[1]==0xD61F0220u) return -2;
    if (!(current[0]|current[1]|current[2]|current[3])) return -3;
    memcpy(g_hook_orig[slot],current,16); g_hook_orig_addr[slot]=code;
    return 0;
}
static int publish_block(int slot,const char *mapname,uintptr_t code,size_t words) {
    uint32_t branch=0;
    if (!wsm_arm64_branch(code,(uintptr_t)g_hookblock[slot],branch)) return -10;
    if(words>SIZE_MAX/sizeof(uint32_t) || !trampoline_pool().seal(g_hookblock[slot],words*sizeof(uint32_t))) return -13;
    g_hook_code2[slot]=code;g_patch_word[slot]=branch;g_patch_len[slot]=4;
    return patch_aliases(mapname,code,&branch,4);
}

/* v25: GOD BLOCK — block kondisional: kalau target==hero_stats → skip damage total! */
static void build_god_block(int slot, uintptr_t code, uintptr_t hero_stats) {
    uint32_t *B = g_hookblock[slot];
    /* literals: hero_stats @ B[25..26] — v29c: pakai ADR (PC-rel ±1MB, skala byte) BUKAN
       adrp+LDR-unsigned (LDR scales imm12 x8 — offset 0xC64 jadi 0x6320 = baca alamat salah!).
       ADR selalu benar: jarak B[0] → B[25] = 100 byte konstan. */
    B[0] = 0x10000329u;   /* adr x9, #+100  (=&B[25]) */
    B[1] = 0xF9400129u;   /* ldr x9, [x9]   (hero_stats) */
    B[2] = 0xEB09001Fu;                                                 /* cmp x0, x9 */
    /* b.eq -> B[21] */
    int32_t dwords = 21 - 3;
    B[3] = 0x54000000u | ((uint32_t)(dwords & 0x7FFFF) << 5) | 0u;      /* b.eq skip */
    /* normal: counter */
    B[4] = 0x58000389u; // ldr x9, B[32]: full counter pointer, no ADRP distance limit
    B[5] = 0xD503201Fu;
    B[32] = (uint32_t)(uintptr_t)&g_hotcount_arr[slot];
    B[33] = (uint32_t)((uint64_t)(uintptr_t)&g_hotcount_arr[slot] >> 32);
    B[6] = 0xB940012Au;
    B[7] = 0x1100054Au;
    B[8] = 0xB900012Au;
    uint64_t orig_addr = (uint64_t)(uintptr_t)&B[13];
    B[9] = 0x58000051u;
    B[10] = 0xD61F0220u;
    B[11] = (uint32_t)orig_addr;
    B[12] = (uint32_t)(orig_addr >> 32);
    /* B[13..16] = orig 16B (diisi install_god) */
    B[17] = 0x58000051u;
    B[18] = 0xD61F0220u;
    uint64_t ret_addr = (uint64_t)code + 16;
    B[19] = (uint32_t)ret_addr;
    B[20] = (uint32_t)(ret_addr >> 32);
    /* skip tail — v29: tulis NOL ke out-params dulu (x2/x3 = out ptrs) supaya caller
       membaca 0, bukan sampah stack; baru return 0. (Crash v28=0xdead1007 root: out tak terisi) */
    B[21] = 0xB900005Fu;  /* str wzr, [x2] */
    B[22] = 0xB900007Fu;  /* str wzr, [x3] */
    B[23] = 0x52800000u;  /* mov w0, #0 */
    B[24] = 0xD65F03C0u;  /* ret */
    B[25] = (uint32_t)hero_stats;
    B[26] = (uint32_t)((uint64_t)hero_stats >> 32);
}
static int install_god(int slot, const char *mapname, uintptr_t code, uintptr_t hero_stats) {
    if (slot < 0 || slot >= NSLOT) return -1;
    int rc = install_prepare(slot, code);
    if (rc != 0) return rc;
    memcpy(&g_hookblock[slot][13], g_hook_orig[slot], 16);
    build_god_block(slot, code, hero_stats);
    return publish_block(slot,mapname,code,34);
}

static void build_hook_block(int slot, uintptr_t code, uintptr_t cnt) {
    uint32_t *B = g_hookblock[slot];
    B[0] = 0x58000289u; // ldr x9, B[20]: full counter pointer
    B[1] = 0xD503201Fu;
    B[20] = (uint32_t)cnt; B[21] = (uint32_t)((uint64_t)cnt >> 32);
    B[2] = 0xB940012Au;
    B[3] = 0x1100054Au;
    B[4] = 0xB900012Au;
    uint64_t orig_addr = (uint64_t)(uintptr_t)&B[10];
    B[5] = 0x58000051u;
    B[6] = 0xD61F0220u;
    B[7] = (uint32_t)orig_addr;
    B[8] = (uint32_t)(orig_addr >> 32);
    /* B[10..13] = orig 16B (diisi install_hook) */
    B[14] = 0x58000051u;
    B[15] = 0xD61F0220u;
    uint64_t ret_addr = (uint64_t)code + 16;
    B[16] = (uint32_t)ret_addr;
    B[17] = (uint32_t)(ret_addr >> 32);
}
static int install_hook(int slot, const char *mapname, uintptr_t code) {
    if (slot < 0 || slot >= NSLOT) return -1;
    int rc = install_prepare(slot, code);
    if (rc != 0) return rc;
    memcpy(&g_hookblock[slot][10], g_hook_orig[slot], 16);
    build_hook_block(slot, code, (uintptr_t)&g_hotcount_arr[slot]);
    return publish_block(slot,mapname,code,22);
}
static int restore_hook(int slot, const char *mapname) {
    if (slot < 0 || slot >= NSLOT || !g_hook_code2[slot]) return 0;
    if (!wsm_patch_readable(g_hook_code2[slot],g_patch_len[slot],mapname)) return -11;
    if (g_patch_len[slot]==4 && *(volatile uint32_t *)g_hook_code2[slot]!=g_patch_word[slot] &&
        *(volatile uint32_t *)g_hook_code2[slot]!=g_hook_orig[slot][0]) return -11;
    int n = patch_aliases(mapname, g_hook_code2[slot], g_hook_orig[slot], (int)g_patch_len[slot]);
    if (n <= 0) return -1; // Preserve rollback data when restoration fails.
    g_hook_orig_addr[slot] = 0;
    memset(g_hook_orig[slot], 0, 16);
    g_hook_code2[slot] = 0; g_patch_len[slot]=0;
    return n;
}
#define WSM_PATCH_LOG LOGI
#include "../jni/wsm_patch.h"

// ExecuteTasks is a static void callback owned by Unity. Preserve its original
// implementation and run scene work only after it returns on UnityMain.
static void (*g_main_original)(void *) = nullptr;
static __thread bool g_main_reentrant = false;
static void main_dispatch(void *method) {
    auto original = __atomic_load_n(&g_main_original, __ATOMIC_ACQUIRE);
    if (original) original(method);
    if (!g_bus || g_main_reentrant) return;
    char thread[16]{};
    if (pthread_getname_np(pthread_self(), thread, sizeof thread) || strcmp(thread, "UnityMain")) return;
    g_bus[WSM_MAIN_TID] = (uint64_t)gettid();
    const uint64_t ticket = __atomic_load_n(&g_bus[WSM_MAIN_REQUEST], __ATOMIC_ACQUIRE);
    if (!ticket || ticket == __atomic_load_n(&g_bus[WSM_MAIN_ACK], __ATOMIC_ACQUIRE)) return;
    uint64_t pending=(uint64_t)wsm::SweepState::Pending;
    if(!__atomic_compare_exchange_n(&g_bus[WSM_MAIN_STATE],&pending,
        (uint64_t)wsm::SweepState::Running,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    g_main_reentrant = true;
    const auto *api = (const wsm::SweepApi *)(uintptr_t)g_bus[WSM_MAIN_API];
    wsm::SweepResult result{wsm::SweepState::Failed, 0, 0};
    if (api) result = wsm::run_sweep(*api, (void *)(uintptr_t)g_bus[WSM_MAIN_STAGE],
                                   &g_bus[WSM_MAIN_ALLOWED], ticket);
    g_bus[WSM_MAIN_APPLIED] = result.applied; g_bus[WSM_MAIN_SKIPPED] = result.skipped;
    __atomic_store_n(&g_bus[WSM_MAIN_STATE], (uint64_t)result.state, __ATOMIC_RELEASE);
    __atomic_store_n(&g_bus[WSM_MAIN_ACK], ticket, __ATOMIC_RELEASE);
    g_main_reentrant = false;
}
static int install_main_dispatch(uintptr_t code, const char *library="libil2cpp.so") {
    constexpr int slot = WSM_HOOK_SLOTS - 1;
    if (g_bus[WSM_MAIN_HOOKED]) return g_hook_code2[slot] == code ? 1 : -1;
    int rc = install_prepare(slot, code, library);
    if (rc) return rc;
    uint32_t *b = g_hookblock[slot];
    // A four-byte branch replaces ONE instruction; relocate only that word.
    // Copying the first sixteen bytes would include ExecuteTasks' TBNZ.
    if (!wsm_arm64_relocate_word(g_hook_orig[slot][0], code, (uintptr_t)(b+8), b[8]) ||
        !wsm_arm64_branch((uintptr_t)(b+9), code+4, b[9])) return -9;
    b[0]=0x58000090u; b[1]=0xD61F0200u; // ldr x16, b[4]; br x16
    b[2]=b[3]=0xD503201Fu;
    const uintptr_t callback=(uintptr_t)&main_dispatch;
    b[4]=(uint32_t)callback;b[5]=(uint32_t)((uint64_t)callback>>32);
    __atomic_store_n(&g_main_original, reinterpret_cast<void (*)(void *)>(b+8), __ATOMIC_RELEASE);
    rc=publish_block(slot,library,code,10);
    if(rc>0)__atomic_store_n(&g_bus[WSM_MAIN_HOOKED],1ULL,__ATOMIC_RELEASE);
    return rc;
}

static int build_scaled_getter(uint32_t *B,uint32_t foff,uint32_t fbits,uintptr_t hs) {
                        uint32_t ldr_s0 = 0xBD400000u | (((foff >> 2) & 0xFFFu) << 10);
                        if (hs) { /* conditional hero-only: x0 == hero_stats -> scaled, else normal */
                            B[0] = 0x10000149u;   /* adr x9, #+40  (-> B[10]) */
                            B[1] = 0xF9400129u;   /* ldr x9, [x9] */
                            B[2] = 0xEB09001Fu;   /* cmp x0, x9 */
                            B[3] = 0x54000060u;   /* b.eq B[6] */
                            B[4] = ldr_s0;        /* normal: ldr s0,[x0,#off] */
                            B[5] = 0xD65F03C0u;   /* ret (nilai asli) */
                            B[6] = ldr_s0;        /* hero: ldr s0,[x0,#off] */
                            B[7] = 0x1C0000A1u;   /* ldr s1, #+20 (-> B[12]) */
                            B[8] = 0x1E210800u;   /* fmul s0, s0, s1 */
                            B[9] = 0xD65F03C0u;   /* ret */
                            B[10] = (uint32_t)hs;
                            B[11] = (uint32_t)((uint64_t)hs >> 32);
                            B[12] = fbits;
                            return 13;
                        } else { /* unconditional scale */
                            B[0] = ldr_s0;
                            B[1] = 0x1C000061u;   /* ldr s1, #+12 (-> B[4]) */
                            B[2] = 0x1E210800u;   /* fmul */
                            B[3] = 0xD65F03C0u;   /* ret */
                            B[4] = fbits;
                            return 5;
                        }
}
static int build_wrapped_getter(uint32_t *B,uintptr_t code,const uint32_t *original,uint32_t fbits,uintptr_t hs) {
    // Preserve the original method implementation (including obfuscated stats
    // and modifiers); multiply only its hero result. Normal callers take orig.
    B[0]=0x58000389u;B[1]=0xEB09001Fu;B[2]=0x540001C1u;
    B[3]=0xA9BF7BFDu;B[4]=0x58000350u;B[5]=0xD63F0200u;
    B[6]=0x1C000341u;B[7]=0x1E210800u;B[8]=0xA8C17BFDu;B[9]=0xD65F03C0u;
    if (!wsm_arm64_relocate_prologue(original, code, (uintptr_t)(B+16), B+16)) return 0;
    // A direct branch preserves all registers defined by the copied prologue,
    // including x16 when it is the ADR/ADRP destination.
    if (!wsm_arm64_branch((uintptr_t)(B+20), code+16, B[20])) return 0;
    B[21]=0xD503201Fu;
    uint64_t ret=(uint64_t)code+16,orig=(uint64_t)(uintptr_t)(B+16);
    B[22]=(uint32_t)ret;B[23]=(uint32_t)(ret>>32);
    B[28]=(uint32_t)hs;B[29]=(uint32_t)((uint64_t)hs>>32);
    B[30]=(uint32_t)orig;B[31]=(uint32_t)(orig>>32);B[32]=fbits;
    return 33;
}
// Execute synthetic code only: branch, neighboring function, hero predicate,
// float getter and restoration. No game memory is modified by this test.
static int branch_selftest() {
    const size_t ps=(size_t)sysconf(_SC_PAGESIZE);
    uint32_t *target=(uint32_t *)mmap(nullptr,ps,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(target==MAP_FAILED)return -1;
    uint32_t *near=(uint32_t *)wsm_near_page((uintptr_t)target);
    if(near==MAP_FAILED){munmap(target,ps);return -2;}
    uint32_t original[4]={0x528000e0u,0xD503201Fu,0xD503201Fu,0xD503201Fu};
    memcpy(target,original,16);target[4]=0xD65F03C0u;
    target[5]=0x52800c60u;target[6]=0xD65F03C0u; // neighbor returns 99
    target[16]=0xBD400000u;target[17]=0xD65F03C0u; // getter reads float at offset 0
    uint32_t wrapped[7]={0xA9BF7BFDu,0x910003FDu,0xD503201Fu,0xD503201Fu,0xBD400000u,0xA8C17BFDu,0xD65F03C0u};
    memcpy(target+24,wrapped,sizeof wrapped);
    // Address generation in the copied prologue must still find the original
    // data after it moves into a different page (the GT critical getter uses ADRP).
    const uint32_t data_offset = 384;
    // x16 is intentional: the return-to-original jump must not destroy it.
    const uint32_t address_wrapped[7]={0xA9BF7BFDu,0x910003FDu,0x90000010u,
        0x91000210u | (data_offset << 10),0xBD400200u,0xA8C17BFDu,0xD65F03C0u};
    memcpy(target+40,address_wrapped,sizeof address_wrapped);
    float original_value=2;memcpy((uint8_t *)target+data_offset,&original_value,4);
    near[0]=0x52800540u;near[1]=0xD65F03C0u; // returns 42
    float hero=2,other=3,multiplier=3;uint32_t bits;memcpy(&bits,&multiplier,4);
    build_scaled_getter(near+128,0,bits,(uintptr_t)&hero);
    build_wrapped_getter(near+192,(uintptr_t)(target+24),wrapped,bits,(uintptr_t)&hero);
    if(!build_wrapped_getter(near+256,(uintptr_t)(target+40),address_wrapped,bits,(uintptr_t)&hero)) {
        munmap(near,ps);munmap(target,ps);return -14;
    }
    uint32_t *saved=g_hookblock[31];g_hookblock[31]=near+64;
    memcpy(near+64+13,original,16);build_god_block(31,(uintptr_t)target,(uintptr_t)&hero);
    g_hookblock[31]=saved;
    __builtin___clear_cache((char *)near,(char *)near+ps);
    __builtin___clear_cache((char *)target,(char *)target+ps);
    int rc=0;
    if(mprotect(near,ps,PROT_READ|PROT_EXEC)||mprotect(target,ps,PROT_READ|PROT_EXEC))rc=-3;
    auto write=[&](size_t index,uint32_t value)->bool{
        if(mprotect(target,ps,PROT_READ|PROT_WRITE))return false;
        __atomic_store_n(target+index,value,__ATOMIC_RELEASE);
        __builtin___clear_cache((char *)(target+index),(char *)(target+index+1));
        return mprotect(target,ps,PROT_READ|PROT_EXEC)==0;
    };
    typedef int(*IntFn)(void *,void *,int *,int *);typedef float(*FloatFn)(void *);
    IntFn run=(IntFn)(uintptr_t)target,neighbor=(IntFn)(uintptr_t)(target+5);
    FloatFn getter=(FloatFn)(uintptr_t)(target+16);
    FloatFn wrapped_getter=(FloatFn)(uintptr_t)(target+24);
    FloatFn address_getter=(FloatFn)(uintptr_t)(target+40);
    uint32_t branch=0;int out1=5,out2=6;
    if(!rc && (run(nullptr,nullptr,&out1,&out2)!=7 || neighbor(nullptr,nullptr,nullptr,nullptr)!=99))rc=-4;
    if(!rc && (!wsm_arm64_branch((uintptr_t)target,(uintptr_t)near,branch)||!write(0,branch)||run(nullptr,nullptr,&out1,&out2)!=42||neighbor(nullptr,nullptr,nullptr,nullptr)!=99))rc=-5;
    if(!rc && (!wsm_arm64_branch((uintptr_t)target,(uintptr_t)(near+64),branch)||!write(0,branch)||run(&hero,nullptr,&out1,&out2)!=0||out1!=0||out2!=0||run(&other,nullptr,&out1,&out2)!=7))rc=-6;
    if(!rc && (!wsm_arm64_branch((uintptr_t)(target+16),(uintptr_t)(near+128),branch)||!write(16,branch)||getter(&hero)!=6||getter(&other)!=3))rc=-7;
    if(!rc && (!wsm_arm64_branch((uintptr_t)(target+24),(uintptr_t)(near+192),branch)||!write(24,branch)||wrapped_getter(&hero)!=6||wrapped_getter(&other)!=3))rc=-9;
    if(!rc && (!write(24,wrapped[0])||wrapped_getter(&hero)!=2))rc=-10;
    if(!rc && (address_getter(&hero)!=2 || !wsm_arm64_branch((uintptr_t)(target+40),(uintptr_t)(near+256),branch) ||
        !write(40,branch) || address_getter(&hero)!=6 || address_getter(&other)!=2))rc=-15;
    if(!rc && (!write(40,address_wrapped[0]) || address_getter(&hero)!=2))rc=-16;
    if(!rc && (!write(0,original[0])||!write(16,0xBD400000u)||run(nullptr,nullptr,&out1,&out2)!=7||getter(&hero)!=2||neighbor(nullptr,nullptr,nullptr,nullptr)!=99))rc=-8;
    munmap(near,ps);munmap(target,ps);return rc;
}

static void *h64_agent(void *arg) {
    (void)arg;
    if (!g_bus) return nullptr;
    g_bus[2] = (uint64_t)getpid();
    g_bus[3] = H64_BUS_MAGIC;
    g_bus[11] = H64_THREAD_MAGIC;
    LOGI("agent thread UP pid=%d", (int)getpid());
    for (;;) {
        const uint32_t generation=wsm::bus_generation(g_bus);
        __atomic_add_fetch(&g_bus[10], 1ULL, __ATOMIC_RELEASE); /* heartbeat */
        uint64_t cmd = __atomic_load_n(&g_bus[0], __ATOMIC_ACQUIRE);
        if (cmd) { g_bus[68] = (uint64_t)(int64_t)-1; g_bus[69] = 0; }
        uint64_t tok = g_bus[7]; /* v31: token ack anti-stale */
        if (cmd == 1) { /* ping */
            g_bus[2] = (uint64_t)getpid();
            g_bus[3] = H64_BUS_MAGIC;
            g_bus[1] = 2;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            LOGI("agent: ping served");
        } else if (cmd == 2) { /* compute a*b */
            g_bus[6] = g_bus[4] * g_bus[5];
            g_bus[1] = 3;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            LOGI("agent: mul %llu x %llu = %llu",
                 (unsigned long long)g_bus[4], (unsigned long long)g_bus[5],
                 (unsigned long long)g_bus[6]);
        } else if (cmd == 3) { /* READ64: bus[4]=addr -> bus[6]=value */
            uint64_t a = g_bus[4];
            g_bus[6] = a ? *(volatile uint64_t *)(uintptr_t)a : 0;
            g_bus[1] = 3;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            LOGI("agent: read64 [0x%llx]=0x%llx", (unsigned long long)a,
                 (unsigned long long)g_bus[6]);
        } else if (cmd == 4) { /* WRITE64: bus[4]=addr, bus[5]=value */
            uint64_t a = g_bus[4];
            if (a) *(volatile uint64_t *)(uintptr_t)a = g_bus[5];
            g_bus[1] = 3;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            LOGI("agent: write64 [0x%llx]=0x%llx", (unsigned long long)a,
                 (unsigned long long)g_bus[5]);
        } else if (cmd == 5) { /* PATCHSELF v4: + scan/patch copy di /memfd:mem_cache (tanpa restore) */
            uint32_t code[2] = {0x52800EE0u, 0xD65F03C0u};
            uint8_t *pm = (uint8_t *)(uintptr_t)(int (*)()) & h64_magic;
            uint8_t *pv = (uint8_t *)(uintptr_t)(int (*)()) & h64_victim;
            uint64_t oldm = *(uint64_t *)pm;
            uint64_t oldv = *(uint64_t *)pv;
            g_bus[6] = oldm;
            g_bus[30] = oldv;
            uintptr_t ms[16] = {}, me[16] = {}, mo[16] = {};
            int nmaps = 0;
            uintptr_t cs[8] = {}, ce[8] = {};
            int ncs = 0;
            FILE *mf = fopen("/proc/self/maps", "r");
            if (mf) {
                char line[512];
                while (fgets(line, sizeof line, mf)) {
                    unsigned long long s = 0, e = 0, off = 0;
                    char perms[8] = {};
                    if (sscanf(line, "%llx-%llx %7s %llx", &s, &e, perms, &off) != 4) continue;
                    if (strstr(line, "libh64z.so")) {
                        if (nmaps < 16) { ms[nmaps] = (uintptr_t)s; me[nmaps] = (uintptr_t)e; mo[nmaps] = (uintptr_t)off; nmaps++; }
                    } else if (strstr(line, "mem_cache")) {
                        if (ncs < 8) { cs[ncs] = (uintptr_t)s; ce[ncs] = (uintptr_t)e; ncs++; }
                    }
                }
                fclose(mf);
            }
            g_bus[42] = (uint64_t)ncs;
            /* scan mem_cache utk pattern ASLI + patch */
            int hitm = 0, hitv = 0;
            for (int c = 0; c < ncs; c++) {
                for (uintptr_t a = cs[c]; a + 8 <= ce[c]; a += 4) {
                    uint64_t v;
                    memcpy(&v, (void *)a, 8);
                    if (v == oldm) {
                        if (mprotect((void *)(a & ~(uintptr_t)0xFFF), 4096, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
                            memcpy((void *)a, code, 8);
                            __builtin___clear_cache((char *)a, (char *)a + 8);
                            if (!hitm) g_bus[43] = a;
                            hitm++;
                        }
                    } else if (v == oldv) {
                        if (mprotect((void *)(a & ~(uintptr_t)0xFFF), 4096, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
                            memcpy((void *)a, code, 8);
                            __builtin___clear_cache((char *)a, (char *)a + 8);
                            hitv++;
                        }
                    }
                }
            }
            g_bus[40] = (uint64_t)hitm;
            g_bus[41] = (uint64_t)hitv;
            /* patch file aliases juga */
            #define FIND_ALIASES2(P, AL, N) do { \
                uintptr_t F_ = 0; int found_ = 0; \
                for (int i = 0; i < nmaps; i++) if ((uintptr_t)(P) >= ms[i] && (uintptr_t)(P) < me[i]) { F_ = ((uintptr_t)(P) - ms[i]) + mo[i]; found_ = 1; break; } \
                (N) = 0; \
                if (found_) for (int i = 0; i < nmaps && (N) < 8; i++) { uintptr_t rel_ = F_ - mo[i]; if (F_ < mo[i] || rel_ >= (me[i] - ms[i])) continue; (AL)[(N)++] = ms[i] + rel_; } \
            } while (0)
            uintptr_t alm[8] = {}, alv[8] = {};
            int nm = 0, nv = 0;
            FIND_ALIASES2(pm, alm, nm);
            FIND_ALIASES2(pv, alv, nv);
            for (int i = 0; i < nm; i++) {
                mprotect((void *)(alm[i] & ~(uintptr_t)0xFFF), 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
                memcpy((void *)alm[i], code, 8);
                __builtin___clear_cache((char *)alm[i], (char *)alm[i] + 8);
            }
            for (int i = 0; i < nv; i++) {
                mprotect((void *)(alv[i] & ~(uintptr_t)0xFFF), 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
                memcpy((void *)alv[i], code, 8);
                __builtin___clear_cache((char *)alv[i], (char *)alv[i] + 8);
            }
            g_bus[9] = (uint64_t)nm;
            g_bus[13] = (uint64_t)(nm + nv);
            /* CALL keduanya (volatile fn-ptr — cegah optimizer fold!) */
            int (*volatile fpm)() = (int (*)())(void *)pm;
            int (*volatile fpv)() = (int (*)())(void *)pv;
            int rv1 = fpv();
            int r1 = fpm();
            g_bus[31] = (uint64_t)(uint32_t)rv1;
            g_bus[8] = (uint64_t)(uint32_t)r1;
            LOGI("agent: v4 magic=0x%x victim=0x%x | mc hits m=%d v=%d regions=%d", r1, rv1, hitm, hitv, ncs);
            g_bus[1] = 3;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 6) { /* HOOKINSTALL v4: slot=bus[59], code=bus[60]; bus[68]=rc, bus[69]=np */
            uintptr_t code = (uintptr_t)g_bus[60];
            int slot = (int)g_bus[59];
            if (!code || slot < 0 || slot >= NSLOT) { g_bus[1] = 6; __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE); }
            else {
                int np = install_hook(slot, "libil2cpp.so", code);
                g_bus[68] = (np < 0) ? (uint64_t)(int64_t)np : 0;
                g_bus[69] = (np > 0) ? (uint64_t)np : 0;
                LOGI("agent: HOOKINSTALLv4 slot=%d code=0x%llx block=0x%llx rc=%d", slot,
                     (unsigned long long)code,
                     (unsigned long long)(uintptr_t)&g_hookblock[slot][0], np);
                g_bus[1] = 5;
                __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            }
        } else if (cmd == 7) { /* HOOKRESTORE-ALL v3 + lapor counts */
            int nr = 0;
            for (int s = 0; s < NSLOT; s++) nr += restore_hook(s, "libil2cpp.so");
            for (int s = 0; s < NSLOT; s++) g_bus[70 + s] = (uint64_t)g_hotcount_arr[s];
            LOGI("agent: HOOKRESTORE-ALL nr=%d c=[%u,%u,%u,%u]", nr,
                 (unsigned)g_hotcount_arr[0], (unsigned)g_hotcount_arr[1],
                 (unsigned)g_hotcount_arr[2], (unsigned)g_hotcount_arr[3]);
            g_bus[1] = 6;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 10) { /* HOOKREAD: lapor counts tanpa restore */
            for (int s = 0; s < NSLOT; s++) g_bus[70 + s] = (uint64_t)g_hotcount_arr[s];
            g_bus[1] = 8;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 8) { /* SELFTEST v3: slot 4 di h64_magic */
            uintptr_t code = (uintptr_t)&h64_magic;
            g_hotcount_arr[23] = 0;
            int np = install_hook(23, "libh64z.so", code);
            int (*volatile f)() = (int (*)())(void *)code;
            int r1 = f();
            int r2 = f();
            int r3 = f();
            uint32_t c = g_hotcount_arr[23];
            restore_hook(23, "libh64z.so");
            int r4 = f();
            g_bus[71] = (uint64_t)c;
            g_bus[72] = (uint64_t)(uint32_t)(r1 + r2 + r3);
            g_bus[73] = (uint64_t)(uint32_t)r4;
            g_bus[74] = (uint64_t)np;
            LOGI("agent: SELFTESTv3 np=%d count=%u r4=0x%x (harap count=3 r4=0x64c0de)", np, c, r4);
            g_bus[1] = 7;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 11) { /* RAWPATCH: slot=bus[59], code=bus[60], 2 kata=bus[61..62] */
            uintptr_t code = (uintptr_t)g_bus[60];
            int slot = (int)g_bus[59];
            if (!code || slot < 0 || slot >= NSLOT) { g_bus[1] = 6; __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE); }
            else {
                memcpy(g_hook_orig[slot], (void *)code, 16);
                g_hook_orig_addr[slot] = code;
                uint32_t words[4];
                memcpy(words, (void *)code, 16);
                words[0] = (uint32_t)g_bus[61];
                words[1] = (uint32_t)g_bus[62];
                g_hook_code2[slot] = code;
                int np = patch_aliases("libil2cpp.so", code, words, 8);
                g_bus[69] = (uint64_t)np;
                LOGI("agent: RAWPATCH slot=%d code=0x%llx w0=0x%x w1=0x%x np=%d", slot,
                     (unsigned long long)code, words[0], words[1], np);
                g_bus[1] = 5;
                __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            }
        } else if (cmd == 12) { /* GUARD: bus[80]=addr — pasang jebakan tulis di field */
            guard_install_once();
            g_guard_target = (uintptr_t)g_bus[80];
            g_guard_active = 1;
            int r = mprotect((void *)(g_guard_target & ~(uintptr_t)0xFFF), 4096, PROT_READ | PROT_EXEC);
            LOGI("guard: ARMED target=0x%llx mprotect=%d", (unsigned long long)g_guard_target, r);
            g_bus[1] = 5;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 13) { /* UNGUARD */
            g_guard_active = 0;
            if (g_guard_target)
                mprotect((void *)(g_guard_target & ~(uintptr_t)0xFFF), 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
            g_bus[1] = 5;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 14) { /* GUARDREPORT: hits + last pc/lr/x0 */
            g_bus[81] = (uint64_t)g_guard_hits;
            g_bus[82] = (uint64_t)g_guard_last_pc;
            g_bus[83] = (uint64_t)g_guard_last_lr;
            g_bus[84] = (uint64_t)g_guard_last_x0;
            g_bus[85] = (uint64_t)g_guard_last_addr;
            g_bus[1] = 8;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 15) { /* GODBLK v28: slot=bus[59], code=bus[60], hero_stats=bus[88] */
            uintptr_t code = (uintptr_t)g_bus[60];
            int slot = (int)g_bus[59];
            uintptr_t hs = (uintptr_t)g_bus[88];
            if (!code || slot < 0 || slot >= NSLOT || !hs) { g_bus[1] = 6; __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE); }
            else {
                int np = install_god(slot, "libil2cpp.so", code, hs);
                g_bus[68] = (np < 0) ? (uint64_t)(int64_t)np : 0;
                g_bus[69] = (np > 0) ? (uint64_t)np : 0;
                LOGI("agent: GODBLKv28 slot=%d code=0x%llx hero=0x%llx rc=%d", slot,
                     (unsigned long long)code, (unsigned long long)hs, np);
                g_bus[1] = 5;
                __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            }
        } else if (cmd == 16) { // production emitter executable selftest
            int rc=branch_selftest();g_bus[68]=(uint64_t)(int64_t)rc;
            g_bus[6]=rc==0?42:0;g_bus[1]=5;
            LOGI("agent: branch/god/getter/neighbor/restore selftest rc=%d",rc);
            __atomic_store_n(&g_bus[0],0ULL,__ATOMIC_RELEASE);
        } else if (cmd == 17) { /* RESTORE1 v30: bus[59]=slot -> bus[69]=nr */
            int slot = (int)g_bus[59];
            int nr = (slot >= 0 && slot < NSLOT) ? restore_hook(slot, "libil2cpp.so") : -1;
            g_bus[68] = (nr < 0) ? (uint64_t)(int64_t)nr : 0;
            g_bus[69] = (nr > 0) ? (uint64_t)nr : 0;
            LOGI("agent: RESTORE1 slot=%d nr=%d", slot, nr);
            g_bus[1] = 5;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 18) { /* MKBLK v30: slot=bus[59] code=bus[60] style=bus[61] off=bus[62] fbits=bus[63] hs=bus[88] */
            uintptr_t code = (uintptr_t)g_bus[60];
            int slot = (int)g_bus[59];
            int style = (int)g_bus[61];
            uint32_t foff = (uint32_t)g_bus[62];
            uint32_t fbits = (uint32_t)g_bus[63];
            uintptr_t hs = (uintptr_t)g_bus[88];
            int rc = -1;
            if (code && slot >= 0 && slot < NSLOT) {
                rc = install_prepare(slot, code);
                if (rc == 0) {
                    uint32_t *B = g_hookblock[slot];
                    memset(B, 0, 36 * sizeof(uint32_t));
                    int len = 0;
                    if (style == 2) { /* mov w0,#1; ret (return TRUE) */
                        B[0] = 0x52800020u;
                        B[1] = 0xD65F03C0u;
                        len = 2;
                    } else if (style == 5) { /* mov w0,#0; ret (return FALSE) */
                        B[0] = 0x52800000u;
                        B[1] = 0xD65F03C0u;
                        len = 2;
                    } else if (style == 6) { /* ldr s0,#+8; ret; literal 0.0f (return 0.0) */
                        B[0] = 0x1C000040u;
                        B[1] = 0xD65F03C0u;
                        B[2] = 0x00000000u;
                        len = 3;
                    } else if (style == 1) { /* scale float getter (body = ldr s0,[x0,#off]; ret) */
                        len=build_scaled_getter(B,foff,fbits,hs);
                    }
                    if(style==3 && hs)len=build_wrapped_getter(B,code,g_hook_orig[slot],fbits,hs);
                    if (len > 0) {
                        uint64_t blk=(uint64_t)(uintptr_t)B;
                        rc=publish_block(slot,"libil2cpp.so",code,(size_t)len);
                        LOGI("agent: MKBLK slot=%d code=0x%llx style=%d len=%d np=%d blk=0x%llx", slot,
                             (unsigned long long)code, style, len, rc, (unsigned long long)blk);
                    } else {
                        rc = -9;
                    }
                }
                g_bus[68] = (rc < 0) ? (uint64_t)(int64_t)rc : 0;
                g_bus[69] = (rc > 0) ? (uint64_t)rc : 0;
            }
            g_bus[1] = 5;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
        } else if (cmd == 19) { // install main-thread mailbox, no scene mutation here
            int rc=install_main_dispatch((uintptr_t)g_bus[60]);
            g_bus[68]=rc<0?(uint64_t)(int64_t)rc:0;g_bus[69]=rc>0?(uint64_t)rc:0;
            __atomic_store_n(&g_bus[0],0ULL,__ATOMIC_RELEASE);
        } else if (cmd == 9) { /* exit */
            g_bus[1] = 4;
            __atomic_store_n(&g_bus[0], 0ULL, __ATOMIC_RELEASE);
            __atomic_store_n(&g_bus[9],tok,__ATOMIC_RELEASE);wsm::notify_bus(g_bus);
            LOGI("agent: exit");
            break;
        }
        // Unknown commands terminate with an error instead of spinning forever.
        if(cmd && __atomic_load_n(&g_bus[0],__ATOMIC_ACQUIRE)==cmd) {
            g_bus[68]=(uint64_t)(int64_t)-22;__atomic_store_n(&g_bus[0],0ULL,__ATOMIC_RELEASE);
        }
        if (cmd) { __atomic_store_n(&g_bus[9],tok,__ATOMIC_RELEASE);wsm::notify_bus(g_bus); }
        else wsm::wait_bus(g_bus,generation,1000);
    }
    return nullptr;
}

__attribute__((constructor)) static void h64_ctor(void) {
    const char *bus = getenv("WSM_H64_BUS");
    char *end = nullptr;
    uintptr_t address = bus ? (uintptr_t)strtoull(bus, &end, 16) : 0;
    if (!address || !end || *end || (address & 7)) return;
    g_bus = reinterpret_cast<volatile uint64_t *>(address);
    if (g_bus[WSM_BUS_IDENTITY] != WSM_BUS_MAGIC ||
        g_bus[WSM_BUS_VERSION] != WSM_BUS_PROTOCOL ||
        g_bus[WSM_BUS_PID] != (uint64_t)getpid() || !g_bus[WSM_BUS_NONCE]) {
        g_bus = nullptr; return;
    }
    LOGI("ctor %s: arch=%s pid=%d bus=%p", WSM_BUILD_STAMP, H64_ARCH, (int)getpid(), (void *)(uintptr_t)g_bus);
    if (g_bus) {
        pthread_t th;
        if (pthread_create(&th, nullptr, h64_agent, nullptr) == 0) {
            pthread_detach(th);
            LOGI("agent spawned");
        } else {
            LOGI("agent spawn FAILED");
        }
    } else {
        LOGI("no bus pointer (file missing?)");
    }
}

extern "C" __attribute__((visibility("default"))) int h64_magic(void) {
    return 0x64C0DE;
}

extern "C" __attribute__((visibility("default"))) int h64_victim(void) {
    return 0x11111111;
}

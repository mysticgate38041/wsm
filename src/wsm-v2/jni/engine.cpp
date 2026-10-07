// WSM v2 Engine (runs in the game/app process; arm64 build via ART native bridge).
//   JNI_OnLoad -> duplicate guard -> engine thread:
//     channel map -> ack handshake -> outbound JNI callback (fixture only, silent in game)
//     -> read-only libil2cpp capability probe (maps + ELF dynsym parse; dlopen-free,
//        bridge-safe for ARM64 libs inside x86_64 host processes).
// v6: typed asynchronous control, scene/session gates, process-owned payload.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>
#include <setjmp.h>
#include <math.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <dirent.h>
#ifndef PTRACE_GETREGS
#define PTRACE_GETREGS 12 /* x86_64 ABI */
#endif
#include <jni.h>
#include <android/log.h>
#include "wsm_protocol.h"
#include "wsm_binding.h"
#include "wsm_feature_catalog.h"
#include "wsm_arm64_reloc.h"
#include "wsm_runtime.h"
#include "wsm_bus.h"

#define ELOGI(...) __android_log_print(ANDROID_LOG_INFO, "WSMEngine", __VA_ARGS__)

#if defined(__aarch64__)
#define WSM_ABI_STR "arm64-v8a"
#elif defined(__x86_64__)
#define WSM_ABI_STR "x86_64"
#else
#define WSM_ABI_STR "unknown"
#endif

namespace {

JavaVM *g_vm = nullptr;
int g_init_guard = 0;
int g_menu_dex_fd = -1;
wsm::Runtime g_runtime;
int g_identity_ok = 0, g_foreground = 0;
bool g_session_fault = false, g_godmode_on = false;
uint64_t g_scene_hero = 0, g_scene_stage = 0, g_target_base = 0, g_time_static = 0;
uint64_t g_method_flags = 0;
wsm::BindingApi g_binding_api{};
float g_speed_value = 2.0f;
bool g_bus_unhealthy = false;
bool g_timescale_owned = false;
static void modern_tick();
static void modern_reset(char *, size_t);
static void modern_commands();
static void modern_publish();
static void modern_request(const char *, char *, size_t, uint64_t = 0);


const char *const kSymbols[] = {
    "il2cpp_domain_get",
    "il2cpp_domain_assembly_open",
    "il2cpp_assembly_get_image",
    "il2cpp_class_from_name",
    "il2cpp_class_get_method_from_name",
    "il2cpp_method_get_pointer",
    "il2cpp_class_get_field_from_name",
    "il2cpp_field_get_offset",
    "il2cpp_resolve_icall",
    "il2cpp_thread_attach",
    "il2cpp_field_static_get_value",
    "il2cpp_runtime_invoke",
    "il2cpp_field_get_value",
    "il2cpp_string_new",
    "il2cpp_object_get_class",
    "il2cpp_class_get_name",
    "il2cpp_thread_detach",
};
const size_t kSymbolCount = sizeof(kSymbols) / sizeof(kSymbols[0]);

uint64_t now_ms() {
    struct timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Return the load base of the mapping whose path contains needle; 0 if absent.
   Prefers the mapping with file offset 0 (that one contains the ELF header). */
uint64_t find_lib_base(const char *needle, char *line_out, size_t cap) {
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    const size_t buf_cap = 1u << 21;
    char *buf = static_cast<char *>(malloc(buf_cap));
    if (!buf) {
        close(fd);
        return 0;
    }
    size_t got = 0;
    ssize_t n;
    while (got < buf_cap - 1 && (n = read(fd, buf + got, buf_cap - 1 - got)) > 0) {
        got += static_cast<size_t>(n);
    }
    close(fd);
    buf[got] = 0;
    uint64_t base = 0;
    uint64_t any_base = 0;
    char *save = nullptr;
    for (char *line = strtok_r(buf, "\n", &save); line != nullptr;
         line = strtok_r(nullptr, "\n", &save)) {
        if (strstr(line, needle) == nullptr) continue;
        if (any_base == 0) any_base = strtoull(line, nullptr, 16);
        /* offset is the third whitespace-separated field: addr perms offset ... */
        char *sp = line;
        int spaces = 0;
        while (*sp != 0 && spaces < 2) {
            if (*sp == ' ') spaces++;
            sp++;
        }
        if (spaces == 2 && strncmp(sp, "00000000", 8) == 0) {
            base = strtoull(line, nullptr, 16);
            if (line_out && cap > 0) snprintf(line_out, cap, "%s", line);
            break;
        }
    }
    if (base == 0) base = any_base; /* fallback: first match */
    free(buf);
    return base;
}

/* Collect distinct load bases (file offset 0) of mappings whose path contains needle. */
int collect_lib_bases(const char *needle, uint64_t *out, int cap) {
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    const size_t buf_cap = 1u << 21;
    char *buf = static_cast<char *>(malloc(buf_cap));
    if (!buf) {
        close(fd);
        return 0;
    }
    size_t got = 0;
    ssize_t n;
    while (got < buf_cap - 1 && (n = read(fd, buf + got, buf_cap - 1 - got)) > 0) {
        got += static_cast<size_t>(n);
    }
    close(fd);
    buf[got] = 0;
    int cnt = 0;
    char *save = nullptr;
    for (char *line = strtok_r(buf, "\n", &save); line && cnt < cap;
         line = strtok_r(nullptr, "\n", &save)) {
        if (!strstr(line, needle)) continue;
        char *sp = line;
        int spaces = 0;
        while (*sp != 0 && spaces < 2) {
            if (*sp == ' ') spaces++;
            sp++;
        }
        if (spaces != 2 || strncmp(sp, "00000000", 8) != 0) continue;
        uint64_t b = strtoull(line, nullptr, 16);
        bool dup = false;
        for (int i = 0; i < cnt; i++) {
            if (out[i] == b) dup = true;
        }
        if (!dup) out[cnt++] = b;
    }
    free(buf);
    return cnt;
}

/* ---- read-only ELF64 dynsym resolver (no dlopen; safe on ARM64 libs under the
   native bridge, where dlopen from x86_64 code fails). ---- */
uint32_t u32at(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
uint64_t u64at(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

struct ElfSyms {
    const uint8_t *img;
    const uint8_t *symtab;
    const uint8_t *strtab;
    uint32_t count;
};

bool elf_parse(uint64_t base, ElfSyms *out) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(base);
    if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F') return false;
    if (p[4] != 2 || p[5] != 1) return false; /* ELF64 little-endian */
    if ((u32at(p + 18) & 0xffffu) != 0xb7u) return false; /* EM_AARCH64 */
    uint64_t phoff = u64at(p + 32);
    uint16_t phentsize = 0, phnum = 0;
    memcpy(&phentsize, p + 54, 2);
    memcpy(&phnum, p + 56, 2);
    if (phentsize < 56 || phnum == 0 || phnum > 128) return false;
    uint64_t dyn_va = 0, dyn_sz = 0;
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *ph = p + phoff + static_cast<uint64_t>(i) * phentsize;
        if (u32at(ph) == 2 /* PT_DYNAMIC */) {
            dyn_va = u64at(ph + 16);
            dyn_sz = u64at(ph + 40);
            break;
        }
    }
    if (!dyn_va || dyn_sz < 16 || dyn_sz > (1u << 20)) return false;
    const uint8_t *dyn = p + dyn_va;
    uint64_t symtab = 0, strtab = 0, hash = 0, gnu = 0;
    for (uint64_t off = 0; off + 16 <= dyn_sz; off += 16) {
        uint64_t tag = u64at(dyn + off);
        uint64_t val = u64at(dyn + off + 8);
        if (tag == 0) break;                              /* DT_NULL */
        if (tag == 5) strtab = val;                       /* DT_STRTAB */
        else if (tag == 6) symtab = val;                  /* DT_SYMTAB */
        else if (tag == 4) hash = val;                    /* DT_HASH */
        else if (tag == 0x6ffffef5ull) gnu = val;         /* DT_GNU_HASH */
    }
    if (!symtab || !strtab) return false;
    uint32_t count = 0;
    if (hash) {
        count = u32at(p + hash + 4); /* nchain */
    } else if (gnu) {
        const uint8_t *h = p + gnu;
        uint32_t nbuckets = u32at(h + 0);
        uint32_t symoffset = u32at(h + 4);
        uint32_t bloom_size = u32at(h + 8);
        const uint32_t *buckets =
            reinterpret_cast<const uint32_t *>(h + 16 + static_cast<uint64_t>(bloom_size) * 8);
        const uint32_t *chain = buckets + nbuckets;
        uint32_t mx = symoffset;
        for (uint32_t i = 0; i < nbuckets && i < 4096; i++) {
            uint32_t b = buckets[i];
            if (b < symoffset) continue;
            for (uint32_t idx = b, n = 0; n < (1u << 20); idx++, n++) {
                uint32_t c = chain[idx - symoffset];
                if (c & 1u) {
                    if (idx > mx) mx = idx;
                    break;
                }
            }
        }
        count = mx + 1;
    } else {
        return false;
    }
    if (count < 2 || count > (1u << 22)) return false;
    out->img = p;
    out->symtab = p + symtab;
    out->strtab = p + strtab;
    out->count = count;
    return true;
}

bool elf_lookup(const ElfSyms &e, const char *want, uint64_t *addr_out) {
    for (uint32_t i = 0; i < e.count; i++) {
        const uint8_t *s = e.symtab + static_cast<uint64_t>(i) * 24;
        uint32_t name = u32at(s + 0);
        if (name == 0 || name > (1u << 23)) continue;
        const char *nm = reinterpret_cast<const char *>(e.strtab + name);
        if (strcmp(nm, want) == 0) {
            *addr_out = reinterpret_cast<uint64_t>(e.img) + u64at(s + 8);
            return true;
        }
    }
    return false;
}

/* ---- fault-guarded native access: transient houdini-alias mappings AND
   il2cpp internals touched from a foreign thread can fault — a faulting call
   is skipped (counted) instead of taking the game down. ---- */
static __thread sigjmp_buf g_elf_jmp;
static __thread volatile sig_atomic_t g_elf_guard = 0;
static __thread sigjmp_buf g_beat_jmp;
static __thread volatile sig_atomic_t g_beat_guard = 0;
volatile sig_atomic_t g_guard_faults = 0;
static struct sigaction g_previous_segv{};
volatile uintptr_t g_fault_pcs[16] = {};
volatile int g_fault_pc_idx = 0;
volatile uintptr_t g_fault_last_addr = 0;

void elf_segv_handler(int sig, siginfo_t *si, void *uctx) {
    if (g_elf_guard || g_beat_guard) {
        g_guard_faults++;
        uintptr_t pc = 0;
#if defined(__aarch64__)
        pc = reinterpret_cast<ucontext_t *>(uctx)->uc_mcontext.pc;
#elif defined(__x86_64__)
        pc = static_cast<uintptr_t>(
            reinterpret_cast<ucontext_t *>(uctx)->uc_mcontext.gregs[REG_RIP]);
#endif
        g_fault_pcs[g_fault_pc_idx++ & 15] = pc;
        g_fault_last_addr = reinterpret_cast<uintptr_t>(si->si_addr);
        if (g_elf_guard) siglongjmp(g_elf_jmp, 1);
        siglongjmp(g_beat_jmp, 1);
    }
    // Preserve ART/the application's handler for faults outside our guarded calls.
    if ((g_previous_segv.sa_flags & SA_SIGINFO) && g_previous_segv.sa_sigaction) {
        g_previous_segv.sa_sigaction(sig, si, uctx); return;
    }
    if (g_previous_segv.sa_handler != SIG_DFL && g_previous_segv.sa_handler != SIG_IGN && g_previous_segv.sa_handler) {
        g_previous_segv.sa_handler(sig); return;
    }
    signal(sig, SIG_DFL); raise(sig);
}

/* v3.5: handler installed ONCE (per-window sigaction churn raced across threads)
   and the jump target is THREAD-LOCAL — a fault can only ever resume the SAME
   thread's guarded window, never another thread's stack (menu-UI crash lesson). */
static void guard_install_once() {
    static volatile int done = 0;
    if (__atomic_test_and_set(&done, __ATOMIC_SEQ_CST)) return;
    struct sigaction sa{};
    sa.sa_sigaction = elf_segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, &g_previous_segv);
}

/* Wrap any risky native/il2cpp call: on SIGSEGV the call is abandoned and
   execution continues after GUARDED_END(). */
#define GUARDED_BEGIN()                                                    \
    do {                                                                   \
        guard_install_once();                                              \
        g_elf_guard = 1;                                                   \
        if (sigsetjmp(g_elf_jmp, 1) == 0) {
#define GUARDED_END()                                                      \
        }                                                                  \
        g_elf_guard = 0;                                                   \
    } while (0)
/* whole-beat guard (separate jmp target): any fault inside a loop iteration is
   abandoned and the loop continues — nested GUARDED_* blocks keep working. */
#define BEAT_BEGIN()                                                       \
    do {                                                                   \
        guard_install_once();                                              \
        g_beat_guard = 1;                                                  \
        if (sigsetjmp(g_beat_jmp, 1) == 0) {
#define BEAT_END()                                                         \
        }                                                                  \
        g_beat_guard = 0;                                                  \
    } while (0)

/* ---- G11 control plane: file-based command channel (guarded, reversible).
   Root writes a command line to wsm_cmd; the engine executes it against the
   live game and writes an ack line to wsm_ack (+ logcat CTL:...). ---- */
struct CtlPath {
    const char *cmd;
    const char *ack;
};
const CtlPath kCtlPaths[] = {
    {"/data/user/0/com.kakaogames.gdts/files/wsm_cmd",
     "/data/user/0/com.kakaogames.gdts/files/wsm_ack"},
};
const size_t kCtlCount = sizeof(kCtlPaths) / sizeof(kCtlPaths[0]);

void *ctl_klass = nullptr;
void *ctl_f_inst = nullptr;
void *ctl_f_mods = nullptr;
void *ctl_mi_mod = nullptr;
void *ctl_mi_instance = nullptr;
void *ctl_mi_unmod = nullptr;
void *ctl_mi_setmax = nullptr;
void *ctl_mi_resetmax = nullptr;
void *ctl_mi_clear = nullptr;
void *ctl_instance = nullptr;
void *ctl_fn_invoke = nullptr;
void *ctl_fn_strnew = nullptr;
void *ctl_fn_fsgv = nullptr;

int ctl_read(const char *path, char *buf, size_t cap) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    ssize_t n; do { n = read(fd, buf, cap - 1); } while (n < 0 && errno == EINTR);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return static_cast<int>(n);
}

void ctl_write(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    fchmod(fd, 0600);
    const size_t len = strnlen(text, 4095);
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, text + done, len - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += static_cast<size_t>(w);
    }
    close(fd);
}

static inline bool ptr_ok(const void *p); /* fwd: defined below with the ELF helpers */

uint64_t ctl_mods_count() {
    if (!ctl_fn_fsgv || !ctl_f_mods) return 0;
    typedef void (*fn_get_t)(void *, void *);
    fn_get_t f = nullptr;
    memcpy(&f, &ctl_fn_fsgv, sizeof f);
    void *mv = nullptr;
    GUARDED_BEGIN();
    f(ctl_f_mods, &mv);
    GUARDED_END();
    if (!ptr_ok(mv)) return 0; /* reject poison/raw garbage (0xdead1031 lesson) */
    int32_t sz = -1;
    GUARDED_BEGIN();
    memcpy(&sz, reinterpret_cast<const uint8_t *>(mv) + 0x18, 4);
    GUARDED_END();
    return (sz >= 0 && sz <= 1000000) ? static_cast<uint64_t>(sz) : 0;
}

/* v3.5: out-of-band timescale apply — used when the request arrives from the
   game UI thread (menu tap); the ticker calls this on the engine thread. */
static inline bool ptr_ok(const void *p);
static bool method_static(void *method) {
    if (!g_method_flags || !method) return false;
    typedef uint32_t (*fn)(void *,uint32_t *); fn f=nullptr; memcpy(&f,&g_method_flags,sizeof f);
    uint32_t impl=0,flags=0; GUARDED_BEGIN(); flags=f(method,&impl); GUARDED_END();
    return (flags & 0x10u)!=0;
}
static uint64_t method_code(void *method) {
    uint64_t code=0; if (!ptr_ok(method)) return 0;
    GUARDED_BEGIN(); memcpy(&code,method,sizeof code); GUARDED_END(); return code;
}
static void *strict_binding(void *klass, const wsm::Binding &wanted) {
    wsm::BindingResult result{nullptr, wsm::BindingState::Unavailable, 0};
    GUARDED_BEGIN(); result = wsm::resolve_binding(g_binding_api, klass, wanted); GUARDED_END();
    return result.state == wsm::BindingState::Found ? result.method : nullptr;
}
void ctl_ts_apply(float val, char *ack, size_t cap) {
    if(val<=0.001f && !g_timescale_owned){snprintf(ack,cap,"OK timescale already OFF");return;}
    typedef void *(*fn_inv_t5)(void *, void *, void **, void **);
    typedef void *(*fn_sn_t5)(const char *);
    fn_inv_t5 inv = nullptr;
    fn_sn_t5 sn = nullptr;
    memcpy(&inv, &ctl_fn_invoke, sizeof inv);
    memcpy(&sn, &ctl_fn_strnew, sizeof sn);
    // Query the public field API only when a ready-scene command needs it.
    if(ctl_fn_fsgv && ctl_f_inst) {
        typedef void(*GetStatic)(void *,void *);GetStatic get=nullptr;
        memcpy(&get,&ctl_fn_fsgv,sizeof get);void *instance=nullptr;
        GUARDED_BEGIN();get(ctl_f_inst,&instance);GUARDED_END();ctl_instance=instance;
    }
    const bool is_static = method_static(ctl_mi_mod) && method_static(ctl_mi_unmod);
    if (!is_static && ctl_mi_instance && inv) {
        void *exc = nullptr;
        GUARDED_BEGIN(); ctl_instance = inv(ctl_mi_instance, nullptr, nullptr, &exc); GUARDED_END();
        if (exc) ctl_instance = nullptr;
    }
    void *receiver = is_static ? nullptr : ctl_instance;
    if ((!is_static && !receiver) || !inv || !sn || !ctl_mi_mod || !ctl_mi_unmod) {
        snprintf(ack, cap, "ERR timescale unavailable: GlobalTimeManager instance/API absent");
        return;
    }
    void *s = nullptr;
    GUARDED_BEGIN(); s = sn("wsm"); GUARDED_END();
    if (!s) { snprintf(ack, cap, "ERR strnew"); return; }
    if (val <= 0.001f) {
        void *exc0 = nullptr; void *args0[1] = { s };
        GUARDED_BEGIN(); (void) inv(ctl_mi_unmod, receiver, args0, &exc0); GUARDED_END();
        if(!exc0)g_timescale_owned=false;
        snprintf(ack, cap, "%s timescale OFF", exc0 ? "ERR" : "OK"); return;
    }
    bool allow = true;
    void *args[3] = { &val, s, &allow };
    void *exc1 = nullptr;g_timescale_owned=true; // An attempted call may have applied before an exception.
    GUARDED_BEGIN();
    (void) inv(ctl_mi_mod, receiver, args, &exc1);
    GUARDED_END();
    snprintf(ack, cap, "%s timescale %.2f modsN=%llu", exc1 ? "ERR" : "OK", static_cast<double>(val),
             static_cast<unsigned long long>(ctl_mods_count()));
}

/* ---- G14 (v3.0) GT feature framework — Guardian Tales-adapted.
   Chain: Stage.Instance -> characterManager -> GetAllPlayers() -> per Character:
     characterStatsBehaviour -> AddCharacterStatsOption / RemoveCharacterStatsOption /
     set_Stamina / set_Mana   (official game APIs, guarded, reversible). ---- */
enum { FK_OPTION = 0, FK_STAM, FK_MANA, FK_TIMESCALE, FK_OHK, FK_DMG, FK_CRIT, FK_STUN,
       FK_AURA, FK_ONEHP, FK_AGGRO };
struct FeatSlot {
    const char *id;
    int kind;
    float value;
    bool on;
};
enum { FEAT_GOD, FEAT_HP, FEAT_STAM, FEAT_MANA, FEAT_POISE, FEAT_IMMUNE, FEAT_TIMESCALE,
       FEAT_OHK, FEAT_DMG, FEAT_CRIT, FEAT_STUN, FEAT_AURA, FEAT_ONEHP, FEAT_AGGRO, FEAT_COUNT };
FeatSlot g_feats[FEAT_COUNT] = {
    {"god", FK_OPTION, 3.0f, false},      /* Immortal(1)|Invincible(2) */
    {"hp", FK_OPTION, 1.0f, false},       /* Immortal(1) */
    {"stam", FK_STAM, 100.0f, false},
    {"mana", FK_MANA, 100.0f, false},
    {"poise", FK_OPTION, 52.0f, false},   /* NoKnockBackByDamage(4)|NoStun(16)|NoDown(32) */
    {"immune", FK_OPTION, 224.0f, false}, /* NoAilment: NoDown(32)|NoArial(64)|NoPoison(128) */
    {"timescale", FK_TIMESCALE, 1.0f, false},
    {"ohk", FK_OHK, 1.0f, false},         /* pulse: InstantKillDamage to monsters */
    {"dmg", FK_DMG, 10.0f, false},        /* pulse modifier (multiplier) */
    {"crit", FK_CRIT, 1.0f, false},       /* pulse critical flag */
    {"stunall", FK_STUN, 1.0f, false},    /* pulse stun (factor/result/duration) */
    {"aura", FK_AURA, 20.0f, false},      /* kill/stun sweep radius in metres */
    {"onehp", FK_ONEHP, 1.0f, false},     /* drop monsters to exactly 1 HP */
    {"aggro", FK_AGGRO, 1.0f, false},     /* reset enemy aggro each beat */
};
struct OwnedOptions { void *stats; uint32_t bits; };
OwnedOptions g_owned_options[64]{};
void *g_m_getoptions = nullptr;
volatile int g_feat_version = 0;
volatile int g_feat_applied_version = -1;
volatile int g_last_char_count = -1;
volatile int g_menu_ctx = 0;      /* 1 = called from the game UI thread (menu tap) */
volatile int g_pending_ts = -1;   /* >=0 -> ticker applies timescale out-of-band */
volatile float g_pending_ts_val = 0.0f;
uint64_t g_img = 0, g_dom = 0;
void *g_cfn = nullptr, *g_cgm = nullptr, *g_cgf = nullptr;
void *g_finv = nullptr, *g_fgv2 = nullptr, *g_attach = nullptr;
void *g_cls_stage = nullptr, *g_cls_cmgr = nullptr, *g_cls_char = nullptr, *g_cls_stats = nullptr;
void *g_cls_fos = nullptr, *g_m_fos_applydmg = nullptr, *g_m_fos_damage = nullptr;
void *g_m_fos_checkdie = nullptr, *g_m_fos_changehp = nullptr, *g_m_stats_applydmg = nullptr;
void *g_m_stage_inst = nullptr, *g_m_cm_players = nullptr;
void *g_f_stage_cm = nullptr, *g_f_char_stats = nullptr;
void *g_m_stage_getcm = nullptr, *g_m_char_getstats = nullptr;
void *g_objcls = nullptr, *g_clsname = nullptr;

/* Android user-space pointer sanity: reject obvious garbage before we call into it. */
static inline bool ptr_ok(const void *p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x1000000000ull && v < 0x0000800000000000ull;
}

void clsname_of(void *obj, char *out, size_t cap) {
    if (cap == 0) return;
    out[0] = '-';
    out[1] = 0;
    if (!g_objcls || !g_clsname || !ptr_ok(obj)) return;
    typedef void *(*fn_ogc_t)(void *);
    typedef const char *(*fn_cgn_t)(void *);
    fn_ogc_t ogc = nullptr;
    fn_cgn_t cgn = nullptr;
    memcpy(&ogc, &g_objcls, sizeof ogc);
    memcpy(&cgn, &g_clsname, sizeof cgn);
    void *c = nullptr;
    const char *nm = nullptr;
    GUARDED_BEGIN();
    c = ogc(obj);
    GUARDED_END();
    if (!c) return;
    GUARDED_BEGIN();
    nm = cgn(c);
    GUARDED_END();
    if (!nm) return;
    snprintf(out, cap, "%s", nm);
}
void *g_m_addopt = nullptr, *g_m_remopt = nullptr, *g_m_setstam = nullptr, *g_m_setmana = nullptr;
void *g_m_getopts = nullptr;
/* ---- G15 (wave-2) damage pipeline: DamageInfo factories + apply ---- */
void *g_cls_dinfo = nullptr;
void *g_m_cm_monsters = nullptr, *g_m_gtd = nullptr, *g_m_damage = nullptr;
void *g_m_dis_mod = nullptr, *g_m_dis_crit = nullptr;
void *g_m_dis_stunf = nullptr, *g_m_dis_stunr = nullptr, *g_m_dis_stund = nullptr;
void *g_m_dis_notmortal = nullptr;
void *g_m_gdl = nullptr, *g_m_char_getas = nullptr, *g_m_stats_isdead = nullptr;
void *g_m_char_getdb = nullptr, *g_m_char_getovdb = nullptr;
void *g_m_char_getpos = nullptr;
void *g_cls_mdb = nullptr, *g_m_mdb_damage = nullptr, *g_m_mdb_die = nullptr;
void *g_m_odr = nullptr, *g_m_stats_ondead = nullptr, *g_m_setdeadconf = nullptr;
void *g_m_mdb_ondead = nullptr, *g_m_mdb_diein = nullptr, *g_m_mdb_dmgreact = nullptr, *g_m_mdb_dying = nullptr;
void *g_m_cmd_settarget = nullptr;
void *g_m_stats_gethp = nullptr;
void *g_cls_bm = nullptr, *g_m_stage_getbm = nullptr, *g_m_bm_getbattlefor = nullptr;
void *g_cls_bi = nullptr, *g_m_bi_resetaggro = nullptr;
void *g_m_char_setpos1 = nullptr, *g_m_char_setpos2 = nullptr;
void *g_cls_mdc = nullptr, *g_m_mdc_create = nullptr, *g_m_cmd_exec = nullptr;

/* v19 SHOTGUN: tabel target hook — {pointer, label} — dipakai autohook + hookall + hookread */
struct HookTarget { void **ptr; const char *label; };
static HookTarget g_hook_targets[] = {
    { &g_m_damage,        "stats.Damage" },
    { &g_m_mdb_damage,    "mdb.Damage" },
    { &g_m_mdb_die,       "mdb.Die" },
    { &g_m_gdl,           "genDmgLua" },
    { &g_m_odr,           "stats.OnDamageRecorder" },
    { &g_m_stats_ondead,  "stats.OnDeadEvent" },
    { &g_m_setdeadconf,   "stats.set_IsDeadConfirmed" },
    { &g_m_mdb_ondead,    "mdb.OnDeadEvent" },
    { &g_m_mdb_diein,     "mdb.DieInternal" },
    { &g_m_mdb_dmgreact,  "mdb.DamageReaction" },
    { &g_m_mdb_dying,     "mdb.SendMonsterDyingState" },
    { &g_m_cmd_exec,      "mdc.Execute" },
    { &g_m_cmd_settarget, "mdc.set_Target" },
    /* v23: damage path ASLI (dari HW watchpoint) */
    { &g_m_fos_applydmg,  "FOS.ApplyDamage(3)" },
    { &g_m_fos_damage,    "FOS.Damage" },
    { &g_m_fos_checkdie,  "FOS.CheckWillDie" },
    { &g_m_fos_changehp,  "FOS.ChangeHpToFixed" },
    { &g_m_stats_applydmg,"CSB.ApplyDamage(3)" },
};
#define NHOOKT (int)(sizeof(g_hook_targets) / sizeof(g_hook_targets[0]))
/* v28: alamat absolut dump-verified per target (non-ASLR) — validasi + force saat resolver nyasar.
   0 = tak terverifikasi (jangan dipaksa). Sejajar indeks g_hook_targets (dump v3.54). */
static const unsigned long long g_hook_abs[NHOOKT] = {
    0x40002B88C2C4ULL, /* [0] stats.Damage */
    0x40002B8B7930ULL, /* [1] mdb.Damage */
    0x40002B8BA8F4ULL, /* [2] mdb.Die */
    0x4000292C3778ULL, /* [3] genDmgLua */
    0x40002B88CCBCULL, /* [4] stats.OnDamageRecorder */
    0x40002B88F768ULL, /* [5] stats.OnDeadEvent */
    0x40002B886780ULL, /* [6] stats.set_IsDeadConfirmed */
    0x40002B8BAC40ULL, /* [7] mdb.OnDeadEvent */
    0x40002B8B93DCULL, /* [8] mdb.DieInternal */
    0x40002B8B9D34ULL, /* [9] mdb.DamageReaction */
    0x40002B8BAD40ULL, /* [10] mdb.SendMonsterDyingState */
    0x400028E66254ULL, /* [11] mdc.Execute */
    0x400028E66168ULL, /* [12] mdc.set_Target */
    0x400028FB64B4ULL, /* [13] FOS.ApplyDamage(3) */
    0x400028FB7398ULL, /* [14] FOS.Damage */
    0x400028FB63ACULL, /* [15] FOS.CheckWillDie */
    0x400028FB77A0ULL, /* [16] FOS.ChangeHpToFixed */
    0x0ULL,            /* [17] CSB.ApplyDamage(3) — warisan base, tak ada offset sendiri */
};
void *g_cls_scmd = nullptr, *g_m_scmd_create = nullptr, *g_m_scmd_exec = nullptr;
void *g_cls_cb = nullptr, *g_m_char_getcb = nullptr, *g_m_cb_onevent = nullptr;
void *g_cls_ssm = nullptr, *g_m_ssm_change = nullptr;
void *g_cls_sstate = nullptr, *g_m_sstate_create = nullptr;
void *g_cls_sev = nullptr, *g_m_sev_create = nullptr;
volatile int g_last_pulse_skip = 0;
volatile int g_scratch_reset = 0; /* set when onehp toggled: clears the scratch ledger */
volatile int g_stun_vec = 3; /* pulse stun vector: 3=legacy dmg = SAFE default (vec1 state=CRASH, verified) */
void *g_scratched[64] = {};
int g_scratched_n = 0; /* feat-thread only */
volatile int g_pulse_src = 0; /* 0 = trap factory (proven), 1 = Lua factory */
volatile int g_pending_sweep = 0;

typedef void *(*fn_inv_t2)(void *, void *, void **, void **);
typedef void *(*fn_cgm_t2)(void *, const char *, int);
typedef void *(*fn_cgf_t2)(void *, const char *);
typedef void *(*fn_cfn_t2)(void *, const char *, const char *);
typedef void (*fn_fgv_t2)(void *, void *, void *);

fn_inv_t2 feat_inv() { fn_inv_t2 f = nullptr; memcpy(&f, &g_finv, sizeof f); return f; }
fn_cgm_t2 feat_cgm() { fn_cgm_t2 f = nullptr; memcpy(&f, &g_cgm, sizeof f); return f; }
fn_cgf_t2 feat_cgf() { fn_cgf_t2 f = nullptr; memcpy(&f, &g_cgf, sizeof f); return f; }
fn_cfn_t2 feat_cfn() { fn_cfn_t2 f = nullptr; memcpy(&f, &g_cfn, sizeof f); return f; }
fn_fgv_t2 feat_fgv() { fn_fgv_t2 f = nullptr; memcpy(&f, &g_fgv2, sizeof f); return f; }

uint32_t feat_want_mask(bool *stam, bool *mana) {
    uint32_t m = 0;
    *stam = false;
    *mana = false;
    for (int i = 0; i < FEAT_COUNT; i++) {
        const FeatSlot &f = g_feats[i];
        if (!f.on) continue;
        if (f.kind == FK_OPTION) m |= static_cast<uint32_t>(f.value);
        else if (f.kind == FK_STAM) *stam = true;
        else if (f.kind == FK_MANA) *mana = true;
    }
    return m;
}

bool feat_resolve() {
    if (g_m_addopt && g_m_remopt && g_m_setstam && g_m_setmana &&
        g_m_stage_inst && g_m_cm_players && g_m_stage_getcm && g_m_char_getstats) {
        return true;
    }
    if (!g_img) return false;
    fn_cfn_t2 cfn = feat_cfn();
    fn_cgm_t2 cgm = feat_cgm();
    fn_cgf_t2 cgf = feat_cgf();
    if (!cfn || !cgm || !cgf) return false;
    void *img = reinterpret_cast<void *>(g_img);
    GUARDED_BEGIN();
    if (!g_cls_stage) g_cls_stage = cfn(img, "Oak", "Stage");
    if (!g_cls_cmgr) g_cls_cmgr = cfn(img, "Oak", "CharacterManager");
    if (!g_cls_char) g_cls_char = cfn(img, "Oak", "Character");
    if (!g_cls_stats) g_cls_stats = cfn(img, "Oak", "CharacterStatsBehaviour");
    GUARDED_END();
    if (!g_cls_stage || !g_cls_cmgr || !g_cls_char || !g_cls_stats) return false;
    GUARDED_BEGIN();
    if (!g_m_stage_inst) g_m_stage_inst = cgm(g_cls_stage, "get_Instance", 0);
    if (!g_m_cm_players) g_m_cm_players = cgm(g_cls_cmgr, "GetAllPlayers", 0);
    if (!g_f_stage_cm) g_f_stage_cm = cgf(g_cls_stage, "characterManager");
    if (!g_f_char_stats) g_f_char_stats = cgf(g_cls_char, "characterStatsBehaviour");
    if (!g_m_addopt) g_m_addopt = cgm(g_cls_stats, "AddCharacterStatsOption", 1);
    if (!g_m_remopt) g_m_remopt = cgm(g_cls_stats, "RemoveCharacterStatsOption", 1);
    if (!g_m_getoptions) g_m_getoptions = cgm(g_cls_stats, "get_CharacterStatsOptions", 0);
    if (!g_m_setstam) g_m_setstam = cgm(g_cls_stats, "set_Stamina", 1);
    if (!g_m_setmana) g_m_setmana = cgm(g_cls_stats, "set_Mana", 1);
    if (!g_m_getopts) g_m_getopts = cgm(g_cls_stats, "get_CharacterStatsOptions", 0);
    if (!g_cls_dinfo) g_cls_dinfo = cfn(img, "Oak", "DamageInfo");
    if (!g_m_cm_monsters) g_m_cm_monsters = cgm(g_cls_cmgr, "GetAllMonsters", 0);
    if (g_cls_dinfo) {
        if (!g_m_gtd) g_m_gtd = cgm(g_cls_dinfo, "GenerateTrapDamage", 2);
        if (!g_m_dis_mod) g_m_dis_mod = cgm(g_cls_dinfo, "set_modifier", 1);
        if (!g_m_dis_crit) g_m_dis_crit = cgm(g_cls_dinfo, "set_critical", 1);
        if (!g_m_dis_stunf) g_m_dis_stunf = cgm(g_cls_dinfo, "set_stunFactor", 1);
        if (!g_m_dis_stunr) g_m_dis_stunr = cgm(g_cls_dinfo, "set_stunResult", 1);
        if (!g_m_dis_stund) g_m_dis_stund = cgm(g_cls_dinfo, "set_stunDuration", 1);
        if (!g_m_dis_notmortal) g_m_dis_notmortal = cgm(g_cls_dinfo, "set_notMortal", 1);
        if (!g_m_gdl) g_m_gdl = cgm(g_cls_dinfo, "GenerateDamageFromLua", 15);
    }
    if (!g_m_damage) g_m_damage = cgm(g_cls_stats, "Damage", 1);
    if (!g_m_odr) g_m_odr = cgm(g_cls_stats, "OnDamageRecorder", 2);
    if (!g_m_stats_ondead) g_m_stats_ondead = cgm(g_cls_stats, "OnDeadEvent", 0);
    if (!g_m_setdeadconf) g_m_setdeadconf = cgm(g_cls_stats, "set_IsDeadConfirmed", 1);
    if (!g_m_char_getas) g_m_char_getas = cgm(g_cls_char, "get_ActiveState", 0);
    if (!g_m_stats_isdead) g_m_stats_isdead = cgm(g_cls_stats, "get_IsDead", 0);
    if (!g_m_char_getdb) g_m_char_getdb = cgm(g_cls_char, "get_DamagedBehaviour", 0);
    if (!g_m_char_getovdb) g_m_char_getovdb = cgm(g_cls_char, "get_OverrideDamageBehaviour", 0);
    if (!g_m_char_getpos) g_m_char_getpos = cgm(g_cls_char, "get_Position", 0);
    if (!g_cls_mdb) g_cls_mdb = cfn(img, "Oak", "MonsterDamagedBehaviour");
    /* v23: TARGET ASLI hasil watchpoint — FieldObjectStatsBehaviour + ApplyDamage */
    if (!g_cls_fos) g_cls_fos = cfn(img, "Oak", "FieldObjectStatsBehaviour");
    if (g_cls_fos && !g_m_fos_applydmg) g_m_fos_applydmg = cgm(g_cls_fos, "ApplyDamage", 3);
    if (g_cls_fos && !g_m_fos_damage) g_m_fos_damage = cgm(g_cls_fos, "Damage", 1);
    if (g_cls_fos && !g_m_fos_checkdie) g_m_fos_checkdie = cgm(g_cls_fos, "CheckWillDie", 1);
    if (g_cls_fos && !g_m_fos_changehp) g_m_fos_changehp = cgm(g_cls_fos, "ChangeHpToFixedValue", 1);
    if (g_cls_stats && !g_m_stats_applydmg) g_m_stats_applydmg = cgm(g_cls_stats, "ApplyDamage", 3);
    if (g_cls_mdb && !g_m_mdb_damage) g_m_mdb_damage = cgm(g_cls_mdb, "Damage", 1);
    if (g_cls_mdb && !g_m_mdb_die) g_m_mdb_die = cgm(g_cls_mdb, "Die", 1);
    if (g_cls_mdb && !g_m_mdb_ondead) g_m_mdb_ondead = cgm(g_cls_mdb, "OnDeadEvent", 1);
    if (g_cls_mdb && !g_m_mdb_diein) g_m_mdb_diein = cgm(g_cls_mdb, "DieInternal", 1);
    if (g_cls_mdb && !g_m_mdb_dmgreact) g_m_mdb_dmgreact = cgm(g_cls_mdb, "DamageReaction", 1);
    if (g_cls_mdb && !g_m_mdb_dying) g_m_mdb_dying = cgm(g_cls_mdb, "SendMonsterDyingState", 1);
    if (!g_m_stats_gethp) g_m_stats_gethp = cgm(g_cls_stats, "get_HP", 0);
    if (!g_cls_bm) g_cls_bm = cfn(img, "Oak", "BattleManager");
    if (!g_m_stage_getbm) g_m_stage_getbm = cgm(g_cls_stage, "get_BattleManager", 0);
    if (g_cls_bm && !g_m_bm_getbattlefor) g_m_bm_getbattlefor = strict_binding(g_cls_bm,
        {"GetBattleFor", "Oak.BattleInstance", {"Oak.IFieldObject", "System.Boolean", nullptr}, 2, false});
    if (!g_cls_bi) g_cls_bi = cfn(img, "Oak", "BattleInstance");
    if (g_cls_bi && !g_m_bi_resetaggro) g_m_bi_resetaggro = cgm(g_cls_bi, "ResetAggro", 1);
    if (!g_m_char_setpos1) g_m_char_setpos1 = cgm(g_cls_char, "Oak.IFieldObject.set_Position", 1);
    if (!g_m_char_setpos2) g_m_char_setpos2 = cgm(g_cls_char, "set_Position", 1);
    if (!g_cls_mdc) g_cls_mdc = cfn(img, "Oak", "MonsterDeadCommand");
    if (g_cls_mdc && !g_m_mdc_create) g_m_mdc_create = cgm(g_cls_mdc, "Create", 1);
    if (g_cls_mdc && !g_m_cmd_exec) g_m_cmd_exec = cgm(g_cls_mdc, "Execute", 1);
    if (g_cls_mdc && !g_m_cmd_settarget) g_m_cmd_settarget = cgm(g_cls_mdc, "set_Target", 1);
    if (!g_cls_scmd) g_cls_scmd = cfn(img, "Oak", "StunCommand");
    if (g_cls_scmd && !g_m_scmd_create) g_m_scmd_create = cgm(g_cls_scmd, "Create", 3);
    if (g_cls_scmd && !g_m_scmd_exec) g_m_scmd_exec = cgm(g_cls_scmd, "Execute", 1);
    if (!g_m_char_getcb) g_m_char_getcb = cgm(g_cls_char, "get_CharacterBehaviour", 0);
    if (!g_cls_cb) g_cls_cb = cfn(img, "Oak", "CharacterBehaviour");
    if (g_cls_cb && !g_m_cb_onevent) g_m_cb_onevent = cgm(g_cls_cb, "OnEvent", 1);
    if (!g_cls_ssm) g_cls_ssm = cfn(img, "Oak", "ManualTransitionStateMachine");
    if (!g_cls_ssm) g_cls_ssm = cfn(img, "", "ManualTransitionStateMachine");
    if (g_cls_ssm && !g_m_ssm_change) g_m_ssm_change = cgm(g_cls_ssm, "ChangeState", 1);
    if (!g_cls_sstate) g_cls_sstate = cfn(img, "Oak", "CharacterStunState");
    if (g_cls_sstate && !g_m_sstate_create) g_m_sstate_create = cgm(g_cls_sstate, "Create", 3);
    if (!g_cls_sev) g_cls_sev = cfn(img, "Oak", "StunEvent");
    if (g_cls_sev && !g_m_sev_create) g_m_sev_create = cgm(g_cls_sev, "Create", 3);
    if (!g_m_stage_getcm) g_m_stage_getcm = cgm(g_cls_stage, "get_CharacterManager", 0);
    if (!g_m_char_getstats) g_m_char_getstats = cgm(g_cls_char, "get_CharacterStatsBehaviour", 0);
    GUARDED_END();
    if (g_attach && g_dom) {
        typedef void *(*fn_attach_t2)(void *);
        fn_attach_t2 af = nullptr;
        memcpy(&af, &g_attach, sizeof af);
        GUARDED_BEGIN();
        af(reinterpret_cast<void *>(g_dom));
        GUARDED_END();
    }
    return g_m_addopt && g_m_remopt && g_m_setstam && g_m_setmana &&
           g_m_stage_inst && g_m_cm_players && g_m_stage_getcm && g_m_char_getstats;
}

/* mode 0 = add-only tick; mode 1 = full (add + remove diff); skip_opts=1 = stam/mana only.
   Returns chars touched or negative. */
int feat_apply(int mode, int skip_opts) {
    bool stam = false, mana = false;
    uint32_t want = feat_want_mask(&stam, &mana);
    if (skip_opts && !stam && !mana) return 0;
    if (mode == 0 && want == 0 && !stam && !mana) return 0;
    if (!feat_resolve()) return -1;
    fn_inv_t2 inv = feat_inv();
    fn_fgv_t2 fgv = feat_fgv();
    if (!inv || !fgv) return -1;
    void *exc = nullptr;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(stage)) return -2;
    void *cmgr = nullptr;
    if (g_m_stage_getcm) {
        GUARDED_BEGIN();
        cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(cmgr)) return -3;
    void *lst = nullptr;
    GUARDED_BEGIN();
    lst = inv(g_m_cm_players, cmgr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(lst)) return -5;
    int32_t size = 0;
    memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
    void **items = nullptr;
    memcpy(&items, reinterpret_cast<const uint8_t *>(lst) + 0x10, 8);
    if (size < 0 || size > 64 || !ptr_ok(items)) return -6;
    int applied = 0;
    for (int32_t i = 0; i < size; i++) {
        void *obj = nullptr;
        /* il2cpp array data starts at +0x20 (klass@0, monitor@8, bounds@0x10, len@0x18) */
        memcpy(&obj, reinterpret_cast<const uint8_t *>(items) + 0x20 + static_cast<size_t>(i) * 8,
               8);
        if (!ptr_ok(obj)) continue;
        void *stats = nullptr;
        /* method-only path: the raw field read faults on this build (field_get_type) — dropped */
        if (g_m_char_getstats) {
            GUARDED_BEGIN();
            stats = inv(g_m_char_getstats, obj, nullptr, &exc);
            GUARDED_END();
        }
        if (!ptr_ok(stats)) continue;
        if (!skip_opts) {
            OwnedOptions *owned = nullptr;
            for (auto &entry : g_owned_options) if (entry.stats == stats) { owned = &entry; break; }
            if (!owned) for (auto &entry : g_owned_options) if (!entry.stats) { entry.stats = stats; owned = &entry; break; }
            if (!owned || !g_m_getoptions) continue;
            void *box = nullptr;
            GUARDED_BEGIN(); box = inv(g_m_getoptions, stats, nullptr, &exc); GUARDED_END();
            if (!ptr_ok(box) || exc) continue;
            uint32_t current = 0;
            GUARDED_BEGIN(); memcpy(&current, (uint8_t *)box + 0x10, 4); GUARDED_END();
            uint32_t remove = owned->bits & ~want;
            uint32_t add = want & ~current;
            if (remove) { int mask = (int)remove; void *a[1] = { &mask };
                GUARDED_BEGIN(); (void) inv(g_m_remopt, stats, a, &exc); GUARDED_END();
                if (!exc) owned->bits &= ~remove;
            }
            if (add) { int mask = (int)add; void *a[1] = { &mask };
                GUARDED_BEGIN(); (void) inv(g_m_addopt, stats, a, &exc); GUARDED_END();
                if (!exc) owned->bits |= add;
            }
        }
        if (stam) {
            float sv = g_feats[FEAT_STAM].value;
            void *a[1] = { &sv };
            GUARDED_BEGIN();
            (void) inv(g_m_setstam, stats, a, &exc);
            GUARDED_END();
        }
        if (mana) {
            float sv = g_feats[FEAT_MANA].value;
            void *a[1] = { &sv };
            GUARDED_BEGIN();
            (void) inv(g_m_setmana, stats, a, &exc);
            GUARDED_END();
        }
        applied++;
    }
    return applied;
}

/* light char-count probe (ticker's spawn detection) */
int feat_count() {
    if (!feat_resolve()) return -1;
    fn_inv_t2 inv = feat_inv();
    if (!inv) return -1;
    void *exc = nullptr;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(stage)) return -2;
    void *cmgr = nullptr;
    if (g_m_stage_getcm) {
        GUARDED_BEGIN();
        cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(cmgr)) return -3;
    void *lst = nullptr;
    GUARDED_BEGIN();
    lst = inv(g_m_cm_players, cmgr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(lst)) return -5;
    int32_t size = 0;
    memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
    return (size >= 0 && size <= 64) ? size : -6;
}

/* ---- G15 wave-2: damage pulse — hit every monster with an official game
   DamageInfo (GenerateTrapDamage -> modifier/crit/stun tweaks -> stats.Damage).
   Runs on the ticker thread only (v3.5 UI-thread lesson). ---- */
__attribute__((unused)) int feat_pulse() {
    if (!feat_resolve()) return -1;
    if (!g_m_cm_monsters || !g_m_gtd || !g_m_damage) return -1;
    fn_inv_t2 inv = feat_inv();
    if (!inv) return -1;
    void *exc = nullptr;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(stage)) return -2;
    void *cmgr = nullptr;
    if (g_m_stage_getcm) {
        GUARDED_BEGIN();
        cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(cmgr)) return -3;
    void *lst = nullptr;
    GUARDED_BEGIN();
    lst = inv(g_m_cm_monsters, cmgr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(lst)) return -5;
    int32_t size = 0;
    memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
    void **items = nullptr;
    memcpy(&items, reinterpret_cast<const uint8_t *>(lst) + 0x10, 8);
    if (size <= 0 || size > 256 || !ptr_ok(items)) return 0;
    const bool ohk = g_feats[FEAT_OHK].on;
    const bool stun = g_feats[FEAT_STUN].on;
    if (!ohk && !stun) return 0;
    const bool crit = g_feats[FEAT_CRIT].on;
    const float mult = g_feats[FEAT_DMG].on ? g_feats[FEAT_DMG].value : 1.0f;
    const bool dmg_on = g_feats[FEAT_DMG].on && mult > 1.001f;
    int dmg = ohk ? 1000000 : 10; /* DamageConstants.InstantKillDamage */
    const bool use_lua = (g_pulse_src == 1) && g_m_gdl != nullptr;
    /* hero (kill credit sender) */
    void *hero = nullptr;
    {
        void *plist = nullptr;
        if (g_m_cm_players) {
            GUARDED_BEGIN();
            plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
            GUARDED_END();
        }
        if (ptr_ok(plist)) {
            int32_t ps = 0;
            memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
            void **pit = nullptr;
            memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
            if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                void *h = nullptr;
                memcpy(&h, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                if (ptr_ok(h)) hero = h;
            }
        }
    }
    int hit = 0, skip = 0;
    for (int32_t i = 0; i < size; i++) {
        void *mon = nullptr;
        memcpy(&mon, reinterpret_cast<const uint8_t *>(items) + 0x20 + static_cast<size_t>(i) * 8,
               8);
        if (!ptr_ok(mon)) continue;
        /* filter: only fully-active characters (hitting dormant spawns breaks them) */
        if (g_m_char_getas) {
            void *ab = nullptr;
            GUARDED_BEGIN();
            ab = inv(g_m_char_getas, mon, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(ab)) {
                int32_t act = 0;
                memcpy(&act, reinterpret_cast<const uint8_t *>(ab) + 0x10, 4);
                if (act != 3) { /* ActiveState.Enabled */
                    skip++;
                    continue;
                }
            }
        }
        void *mstats = nullptr;
        if (g_m_char_getstats) {
            GUARDED_BEGIN();
            mstats = inv(g_m_char_getstats, mon, nullptr, &exc);
            GUARDED_END();
        }
        if (!ptr_ok(mstats)) continue;
        /* filter: skip dead/dying so their death flow can finish */
        if (g_m_stats_isdead) {
            void *db = nullptr;
            GUARDED_BEGIN();
            db = inv(g_m_stats_isdead, mstats, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(db)) {
                uint8_t dead = 0;
                memcpy(&dead, reinterpret_cast<const uint8_t *>(db) + 0x10, 1);
                if (dead) {
                    skip++;
                    continue;
                }
            }
        }
        void *boxed = nullptr;
        if (use_lua) {
            /* official script-damage factory: Melee|IgnoreDefense, mortal, hero-credited */
            int16_t dt = 1025;
            void *sender = hero;
            void *target = mon;
            struct NF2 {
                bool has;
                float v;
            } mod = {dmg_on, mult};
            struct V3 {
                float x, y, z;
            } dir = {0, 0, 0};
            bool cr = crit;
            bool nm = false;
            bool nc = false;
            int sf = stun ? 9 : 0;
            float sd = stun ? 2.0f : 0.0f;
            int kbf = 0;
            struct V3 kdir = {0, 0, 0};
            float kbfc = 0.0f;
            void *he = nullptr;
            uint8_t sfxb[64] = {};
            void *a15[15] = {&dt,   &sender, &target, &mod,  &dir,  &cr,   &nm, &nc,
                             &sf,   &sd,     &kbf,    &kdir, &kbfc, &he,   sfxb};
            GUARDED_BEGIN();
            boxed = inv(g_m_gdl, nullptr, a15, &exc);
            GUARDED_END();
        } else {
            void *a2[2] = {&mon, &dmg};
            GUARDED_BEGIN();
            boxed = inv(g_m_gtd, nullptr, a2, &exc);
            GUARDED_END();
        }
        if (!ptr_ok(boxed)) continue;
        static __thread uint8_t infobuf[0x300];
        memset(infobuf, 0, sizeof infobuf);
        memcpy(infobuf, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
        /* trap damage is non-lethal by design — force it mortal so kills land */
        if (!use_lua && g_m_dis_notmortal) {
            bool nb = false;
            void *a1[1] = {&nb};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_notmortal, infobuf, a1, &exc);
            GUARDED_END();
        }
        if (!use_lua && dmg_on && g_m_dis_mod) {
            struct NF {
                bool has;
                float v;
            } nf = {true, mult};
            void *a1[1] = {&nf};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_mod, infobuf, a1, &exc);
            GUARDED_END();
        }
        if (!use_lua && crit && g_m_dis_crit) {
            bool cb = true;
            void *a1[1] = {&cb};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_crit, infobuf, a1, &exc);
            GUARDED_END();
        }
        if (!use_lua && stun) {
            if (g_m_dis_stunf) {
                int sf = 9; /* DamageStunConstants.FactorUltimateStrong */
                void *a1[1] = {&sf};
                GUARDED_BEGIN();
                (void) inv(g_m_dis_stunf, infobuf, a1, &exc);
                GUARDED_END();
            }
            if (g_m_dis_stunr) {
                int sr = 1;
                void *a1[1] = {&sr};
                GUARDED_BEGIN();
                (void) inv(g_m_dis_stunr, infobuf, a1, &exc);
                GUARDED_END();
            }
            if (g_m_dis_stund) {
                float sd = 2.0f;
                void *a1[1] = {&sd};
                GUARDED_BEGIN();
                (void) inv(g_m_dis_stund, infobuf, a1, &exc);
                GUARDED_END();
            }
        }
        void *a3[1] = {infobuf};
        GUARDED_BEGIN();
        (void) inv(g_m_damage, mstats, a3, &exc);
        GUARDED_END();
        hit++;
    }
    g_last_pulse_skip = skip;
    return hit;
}

/* kill-path x-ray: try the monster's own DamagedBehaviour on the first live target.
   mode 1 = db.Damage(info)  mode 2 = db.Die(info) */
void feat_kill_try(int mode, char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "KILL resolve=0");
        return;
    }
    fn_inv_t2 inv = feat_inv();
    void *exc = nullptr;
    sig_atomic_t f0 = g_guard_faults;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    void *cmgr = nullptr;
    if (ptr_ok(stage) && g_m_stage_getcm) {
        GUARDED_BEGIN();
        cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
        GUARDED_END();
    }
    void *lst = nullptr;
    if (ptr_ok(cmgr)) {
        GUARDED_BEGIN();
        lst = inv(g_m_cm_monsters, cmgr, nullptr, &exc);
        GUARDED_END();
    }
    void *hero = nullptr;
    {
        void *plist = nullptr;
        if (ptr_ok(cmgr) && g_m_cm_players) {
            GUARDED_BEGIN();
            plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
            GUARDED_END();
        }
        if (ptr_ok(plist)) {
            int32_t ps = 0;
            memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
            void **pit = nullptr;
            memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
            if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                void *h = nullptr;
                memcpy(&h, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                if (ptr_ok(h)) hero = h;
            }
        }
    }
    sig_atomic_t f1 = g_guard_faults;
    void *mon = nullptr;
    void *mstats = nullptr;
    int32_t act = -1;
    int dead0 = -1;
    int hp0 = -1;
    if (ptr_ok(lst)) {
        int32_t size = 0;
        memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
        void **items = nullptr;
        memcpy(&items, reinterpret_cast<const uint8_t *>(lst) + 0x10, 8);
        if (size > 0 && size <= 256 && ptr_ok(items)) {
            for (int32_t i = 0; i < size && !mon; i++) {
                void *m = nullptr;
                memcpy(&m, reinterpret_cast<const uint8_t *>(items) + 0x20 +
                                static_cast<size_t>(i) * 8,
                       8);
                if (!ptr_ok(m)) continue;
                int32_t a2 = -2;
                if (g_m_char_getas) {
                    void *ab = nullptr;
                    GUARDED_BEGIN();
                    ab = inv(g_m_char_getas, m, nullptr, &exc);
                    GUARDED_END();
                    if (ptr_ok(ab)) {
                        memcpy(&a2, reinterpret_cast<const uint8_t *>(ab) + 0x10, 4);
                    }
                }
                act = a2;
                if (a2 != 3) continue;
                void *s = nullptr;
                if (g_m_char_getstats) {
                    GUARDED_BEGIN();
                    s = inv(g_m_char_getstats, m, nullptr, &exc);
                    GUARDED_END();
                }
                if (!ptr_ok(s)) continue;
                int dd = -1;
                if (g_m_stats_isdead) {
                    void *db2 = nullptr;
                    GUARDED_BEGIN();
                    db2 = inv(g_m_stats_isdead, s, nullptr, &exc);
                    GUARDED_END();
                    if (ptr_ok(db2)) {
                        uint8_t b = 0;
                        memcpy(&b, reinterpret_cast<const uint8_t *>(db2) + 0x10, 1);
                        dd = b;
                    }
                }
                dead0 = dd;
                if (dd != 0) continue;
                mon = m;
                mstats = s;
            }
        }
    }
    if (ptr_ok(mstats) && g_m_stats_gethp) {
        void *hb = nullptr;
        GUARDED_BEGIN();
        hb = inv(g_m_stats_gethp, mstats, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(hb)) {
            memcpy(&hp0, reinterpret_cast<const uint8_t *>(hb) + 0x10, 4);
        }
    }
    sig_atomic_t f2 = g_guard_faults;
    void *db = nullptr;
    if (ptr_ok(mon) && g_m_char_getovdb) {
        GUARDED_BEGIN();
        db = inv(g_m_char_getovdb, mon, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(db) && ptr_ok(mon) && g_m_char_getdb) {
        GUARDED_BEGIN();
        db = inv(g_m_char_getdb, mon, nullptr, &exc);
        GUARDED_END();
    }
    sig_atomic_t f3 = g_guard_faults;
    static __thread uint8_t kbuf[0x300];
    void *boxed = nullptr;
    if (ptr_ok(mon)) {
        memset(kbuf, 0, sizeof kbuf);
        if ((mode == 1 || mode == 2 || mode == 7 || mode == 8) && g_m_gdl) {
            int16_t dt = (mode >= 7) ? 1025 : 1; /* Melee|IgnoreDefense for the chained modes */
            void *sender = hero;
            void *target = mon;
            struct NF5 {
                bool has;
                float v;
            } mod = {false, 0.0f};
            struct V35 {
                float x, y, z;
            } dir = {0, 0, 0};
            bool cr = false;
            bool nm = false;
            bool nc = false;
            int sf = 0;
            float sd = 0.0f;
            int kbf = 0;
            struct V35 kdir = {0, 0, 0};
            float kbfc = 0.0f;
            void *he = nullptr;
            uint8_t sfxb[64] = {};
            void *a15[15] = {&dt,   &sender, &target, &mod,  &dir,  &cr,   &nm, &nc,
                             &sf,   &sd,     &kbf,    &kdir, &kbfc, &he,   sfxb};
            GUARDED_BEGIN();
            boxed = inv(g_m_gdl, nullptr, a15, &exc);
            GUARDED_END();
        } else if ((mode == 3 || mode == 4 || mode == 5 || mode == 6) && g_m_gtd) {
            int dm = 1000000;
            void *a2[2] = {&mon, &dm};
            GUARDED_BEGIN();
            boxed = inv(g_m_gtd, nullptr, a2, &exc);
            GUARDED_END();
        }
        if (ptr_ok(boxed)) {
            memcpy(kbuf, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
            if (g_m_dis_notmortal && mode >= 3 && mode <= 6) {
                bool nb = false;
                void *a1[1] = {&nb};
                GUARDED_BEGIN();
                (void) inv(g_m_dis_notmortal, kbuf, a1, &exc);
                GUARDED_END();
            }
        }
    }
    sig_atomic_t f4 = g_guard_faults;
    int r1 = -1;
    int fS = 0;
    if (ptr_ok(db) && ptr_ok(boxed)) {
        void *a1b[1] = {kbuf};
        if (mode >= 5) {
            /* chained: apply stats damage first (kills HP), then notify the
               death component so the death flow + kill count run. */
            if (ptr_ok(mstats) && g_m_damage) {
                GUARDED_BEGIN();
                (void) inv(g_m_damage, mstats, a1b, &exc);
                GUARDED_END();
            }
            fS = static_cast<int>(g_guard_faults - f4);
            sig_atomic_t f4b = g_guard_faults;
            if (mode == 5 || mode == 7) {
                void *rb = nullptr;
                GUARDED_BEGIN();
                rb = inv(g_m_mdb_damage, db, a1b, &exc);
                GUARDED_END();
                if (ptr_ok(rb)) {
                    uint8_t b = 0;
                    memcpy(&b, reinterpret_cast<const uint8_t *>(rb) + 0x10, 1);
                    r1 = b;
                } else if (g_guard_faults == f4b) {
                    r1 = -9;
                }
            } else if (g_m_mdb_die) {
                GUARDED_BEGIN();
                (void) inv(g_m_mdb_die, db, a1b, &exc);
                GUARDED_END();
                r1 = 99;
            }
        } else if ((mode == 1 || mode == 3) && g_m_mdb_damage) {
            void *rb = nullptr;
            GUARDED_BEGIN();
            rb = inv(g_m_mdb_damage, db, a1b, &exc);
            GUARDED_END();
            if (ptr_ok(rb)) {
                uint8_t b = 0;
                memcpy(&b, reinterpret_cast<const uint8_t *>(rb) + 0x10, 1);
                r1 = b;
            } else if (g_guard_faults == f4) {
                r1 = -9; /* returned null but no fault */
            }
        } else if ((mode == 2 || mode == 4) && g_m_mdb_die) {
            GUARDED_BEGIN();
            (void) inv(g_m_mdb_die, db, a1b, &exc);
            GUARDED_END();
            r1 = 99;
        }
    }
    sig_atomic_t f5 = g_guard_faults;
    int hp1 = -1;
    int dead1 = -1;
    if (ptr_ok(mstats)) {
        if (g_m_stats_gethp) {
            void *hb = nullptr;
            GUARDED_BEGIN();
            hb = inv(g_m_stats_gethp, mstats, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(hb)) {
                memcpy(&hp1, reinterpret_cast<const uint8_t *>(hb) + 0x10, 4);
            }
        }
        if (g_m_stats_isdead) {
            void *db3 = nullptr;
            GUARDED_BEGIN();
            db3 = inv(g_m_stats_isdead, mstats, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(db3)) {
                uint8_t b = 0;
                memcpy(&b, reinterpret_cast<const uint8_t *>(db3) + 0x10, 1);
                dead1 = b;
            }
        }
    }
    snprintf(out, cap,
             "KILL%d mon=0x%llx act=%d dead0=%d hp0=%d db=0x%llx box=0x%llx hero=0x%llx src=%c "
             "r=%d fS=%d | hp1=%d dead1=%d | fF=%d fDb=%d fInfo=%d fCall=%d fPost=%d",
             mode, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)), act, dead0,
             hp0, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(db)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(boxed)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hero)),
             (mode == 1 || mode == 2 || mode == 7 || mode == 8) ? 'L' : 'T', r1, fS, hp1, dead1,
             static_cast<int>(f1 - f0), static_cast<int>(f2 - f1), static_cast<int>(f3 - f2),
             static_cast<int>(f4 - f3), static_cast<int>(f5 - f4));
}

/* ---- population tools: read/track/damage individual monsters by list index ---- */
void feat_mopen(fn_inv_t2 &inv, void *&lst, int32_t &size, void **&items) {
    inv = feat_inv();
    lst = nullptr;
    size = 0;
    items = nullptr;
    void *exc = nullptr;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(stage) || !g_m_stage_getcm) return;
    void *cmgr = nullptr;
    GUARDED_BEGIN();
    cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(cmgr) || !g_m_cm_monsters) return;
    GUARDED_BEGIN();
    lst = inv(g_m_cm_monsters, cmgr, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(lst)) return;
    memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
    memcpy(&items, reinterpret_cast<const uint8_t *>(lst) + 0x10, 8);
    if (size <= 0 || size > 256 || !ptr_ok(items)) {
        size = 0;
        return;
    }
}

void *feat_mobj(void **items, int idx) {
    void *m = nullptr;
    memcpy(&m, reinterpret_cast<const uint8_t *>(items) + 0x20 + static_cast<size_t>(idx) * 8,
           8);
    return ptr_ok(m) ? m : nullptr;
}

void feat_mstat(void *mon, void *&stats, int &act, int &dead, int &hp) {
    stats = nullptr;
    act = -1;
    dead = -1;
    hp = -1;
    if (!ptr_ok(mon)) return;
    void *exc = nullptr;
    fn_inv_t2 inv = feat_inv();
    if (g_m_char_getas) {
        void *ab = nullptr;
        GUARDED_BEGIN();
        ab = inv(g_m_char_getas, mon, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(ab)) memcpy(&act, reinterpret_cast<const uint8_t *>(ab) + 0x10, 4);
    }
    if (g_m_char_getstats) {
        GUARDED_BEGIN();
        stats = inv(g_m_char_getstats, mon, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(stats)) return;
    if (g_m_stats_isdead) {
        void *db = nullptr;
        GUARDED_BEGIN();
        db = inv(g_m_stats_isdead, stats, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(db)) {
            uint8_t b = 0;
            memcpy(&b, reinterpret_cast<const uint8_t *>(db) + 0x10, 1);
            dead = b;
        }
    }
    if (g_m_stats_gethp) {
        void *hb = nullptr;
        GUARDED_BEGIN();
        hb = inv(g_m_stats_gethp, stats, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(hb)) memcpy(&hp, reinterpret_cast<const uint8_t *>(hb) + 0x10, 4);
    }
}

/* forward decls for helpers defined further below */
void feat_getpos(void *obj, float out[3]);

/* dump the whole monster list: idx:ptr:aN:dN:hN */
void feat_mlist(char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "MLIST resolve=0");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv) {
        snprintf(out, cap, "MLIST nofn");
        return;
    }
    if (size <= 0) {
        snprintf(out, cap, "MLIST empty");
        return;
    }
    size_t used = static_cast<size_t>(snprintf(out, cap, "MLIST n=%d", size));
    int live = 0;
    for (int32_t i = 0; i < size && used + 64 < cap; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if (act == 3 && dead == 0) live++;
        used += static_cast<size_t>(
            snprintf(out + used, cap - used, " |%d:%llx:a%d:d%d:h%d", i,
                     static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m)), act, dead,
                     hp));
    }
    snprintf(out + (used < cap ? used : cap - 1), cap > (used < cap ? used : cap - 1)
                                                     ? cap - (used < cap ? used : cap - 1)
                                                     : 1,
             " ||live=%d", live);
}

/* detail one monster + optional damage: mode 0 = read, 5 = trap combo dmg, 6 = trap combo die */
void feat_mdmg(int idx, int mode, char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "MDMG resolve=0");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "MDMG no-list");
        return;
    }
    if (idx < 0 || idx >= size) {
        snprintf(out, cap, "MDMG idx out of range (0..%d)", size - 1);
        return;
    }
    void *mon = feat_mobj(items, idx);
    if (!ptr_ok(mon)) {
        snprintf(out, cap, "MDMG idx=%d bad-ptr", idx);
        return;
    }
    void *mstats = nullptr;
    int act = -1, dead = -1, hp0 = -1;
    feat_mstat(mon, mstats, act, dead, hp0);
    if (mode == 0) {
        snprintf(out, cap, "MREAD idx=%d mon=0x%llx act=%d dead=%d hp=%d stats=0x%llx", idx,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)), act, dead, hp0,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mstats)));
        return;
    }
    if (!ptr_ok(mstats)) {
        snprintf(out, cap, "MDMG idx=%d no-stats", idx);
        return;
    }
    /* trap info build (1M damage, mortal) */
    void *exc = nullptr;
    void *boxed = nullptr;
    if (g_m_gtd) {
        int dm = 1000000;
        void *a2[2] = {&mon, &dm};
        GUARDED_BEGIN();
        boxed = inv(g_m_gtd, nullptr, a2, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(boxed)) {
        snprintf(out, cap, "MDMG idx=%d no-info", idx);
        return;
    }
    static __thread uint8_t mbuf[0x300];
    memcpy(mbuf, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
    if (g_m_dis_notmortal) {
        bool nb = false;
        void *a1[1] = {&nb};
        GUARDED_BEGIN();
        (void) inv(g_m_dis_notmortal, mbuf, a1, &exc);
        GUARDED_END();
    }
    /* db + combo */
    void *db = nullptr;
    if (g_m_char_getovdb) {
        GUARDED_BEGIN();
        db = inv(g_m_char_getovdb, mon, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(db) && g_m_char_getdb) {
        GUARDED_BEGIN();
        db = inv(g_m_char_getdb, mon, nullptr, &exc);
        GUARDED_END();
    }
    sig_atomic_t f0 = g_guard_faults;
    void *a1b[1] = {mbuf};
    GUARDED_BEGIN();
    (void) inv(g_m_damage, mstats, a1b, &exc);
    GUARDED_END();
    sig_atomic_t f1 = g_guard_faults;
    int r = -1;
    if (ptr_ok(db)) {
        if (mode == 6) {
            if (g_m_mdb_die) {
                GUARDED_BEGIN();
                (void) inv(g_m_mdb_die, db, a1b, &exc);
                GUARDED_END();
                r = 99;
            }
        } else if (g_m_mdb_damage) {
            void *rb = nullptr;
            GUARDED_BEGIN();
            rb = inv(g_m_mdb_damage, db, a1b, &exc);
            GUARDED_END();
            if (ptr_ok(rb)) {
                uint8_t b = 0;
                memcpy(&b, reinterpret_cast<const uint8_t *>(rb) + 0x10, 1);
                r = b;
            }
        }
        if (mode == 9 && g_m_mdb_die) {
            GUARDED_BEGIN();
            (void) inv(g_m_mdb_die, db, a1b, &exc);
            GUARDED_END();
            r = 99;
        }
    }
    sig_atomic_t f2 = g_guard_faults;
    /* read back */
    void *st2 = nullptr;
    int act2 = -1, dead2 = -1, hp1 = -1;
    feat_mstat(mon, st2, act2, dead2, hp1);
    int pos = -1;
    /* is it still at the same index? */
    if (feat_mobj(items, idx) == mon) pos = idx;
    snprintf(out, cap,
             "MDMG idx=%d mode=%d mon=0x%llx act=%d dead0=%d hp0=%d r=%d | ri=%d act2=%d dead2=%d "
             "hp1=%d fStats=%d fCall=%d",
             idx, mode, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)), act,
             dead, hp0, r, pos, act2, dead2, hp1, static_cast<int>(f1 - f0),
             static_cast<int>(f2 - f1));
}

/* apply the winning combo once to EVERY active alive monster */
void feat_msweep(int mode, char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "MSWEEP resolve=0");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "MSWEEP no-list");
        return;
    }
    void *hero = nullptr;
    sig_atomic_t f0 = g_guard_faults;
    int done = 0;
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act != 3 && act != 2) || dead != 0) continue;
        {
            float p[3];
            feat_getpos(m, p);
            if (fabsf(p[0]) > 900.0f || fabsf(p[2]) > 900.0f) continue;
        }
        {
            char one[512];
            feat_mdmg(i, mode, one, sizeof one);
            (void) one;
        }
        done++;
    }
    (void) hero;
    sig_atomic_t f1 = g_guard_faults;
    snprintf(out, cap, "MSWEEP mode=%d targets=%d fault=%d", mode, done,
             static_cast<int>(f1 - f0));
}

void feat_getpos(void *obj, float out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!ptr_ok(obj) || !g_m_char_getpos) return;
    void *exc = nullptr;
    fn_inv_t2 inv = feat_inv();
    void *rb = nullptr;
    GUARDED_BEGIN();
    rb = inv(g_m_char_getpos, obj, nullptr, &exc);
    GUARDED_END();
    if (!ptr_ok(rb)) return;
    memcpy(out, reinterpret_cast<const uint8_t *>(rb) + 0x10, 12);
}

/* dump positions of hero + all monsters (mapping who is where) */
void feat_mpos(char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "MPOS resolve=0");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "MPOS no-list");
        return;
    }
    float hp_[3] = {0, 0, 0};
    {
        void *stage = nullptr;
        void *exc = nullptr;
        sig_atomic_t f0 = g_guard_faults;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(stage) && g_m_stage_getcm) {
            void *cmgr = nullptr;
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cmgr) && g_m_cm_players) {
                void *plist = nullptr;
                GUARDED_BEGIN();
                plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
                GUARDED_END();
                if (ptr_ok(plist)) {
                    int32_t ps = 0;
                    memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                    void **pit = nullptr;
                    memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                    if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                        void *h = nullptr;
                        memcpy(&h, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                        if (ptr_ok(h)) feat_getpos(h, hp_);
                    }
                }
            }
        }
        (void) f0;
    }
    size_t used = static_cast<size_t>(
        snprintf(out, cap, "MPOS hero=(%.0f,%.0f,%.0f)", hp_[0], hp_[1], hp_[2]));
    for (int32_t i = 0; i < size && used + 60 < cap; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        float p[3];
        feat_getpos(m, p);
        used += static_cast<size_t>(snprintf(out + used, cap - used,
                                             " |%d:(%.0f,%.0f,%.0f)a%d", i, p[0], p[1], p[2],
                                             act));
    }
}

/* kill the NEAREST live monster to the hero (the one on screen) with full combo */
void feat_mnear(int mode, char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "MNEAR resolve=0");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "MNEAR no-list");
        return;
    }
    /* hero pos */
    float h_[3] = {0, 0, 0};
    void *hero = nullptr;
    {
        void *exc = nullptr;
        void *stage = nullptr;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(stage) && g_m_stage_getcm) {
            void *cmgr = nullptr;
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cmgr) && g_m_cm_players) {
                void *plist = nullptr;
                GUARDED_BEGIN();
                plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
                GUARDED_END();
                if (ptr_ok(plist)) {
                    int32_t ps = 0;
                    memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                    void **pit = nullptr;
                    memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                    if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                        memcpy(&hero, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                        if (ptr_ok(hero)) feat_getpos(hero, h_);
                    }
                }
            }
        }
    }
    int best = -1;
    float bd2 = 1e30f;
    float bp[3] = {0, 0, 0};
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act != 3 && act != 2) || dead != 0) continue;
        float p[3];
        feat_getpos(m, p);
        float dx = p[0] - h_[0], dy = p[1] - h_[1], dz = p[2] - h_[2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bd2) {
            bd2 = d2;
            best = i;
            bp[0] = p[0];
            bp[1] = p[1];
            bp[2] = p[2];
        }
    }
    if (best < 0) {
        snprintf(out, cap, "MNEAR hero=(%.0f,%.0f,%.0f) no-target", h_[0], h_[1], h_[2]);
        return;
    }
    char tmp[512];
    feat_mdmg(best, mode, tmp, sizeof tmp);
    snprintf(out, cap, "MNEAR hero=(%.0f,%.0f,%.0f) idx=%d pos=(%.0f,%.0f,%.0f) d=%.1f | %s",
             h_[0], h_[1], h_[2], best, bp[0], bp[1], bp[2], sqrtf(bd2), tmp);
}

/* full kill combo on one monster: trap info (1M, mortal) -> stats.Damage -> db.Damage -> db.Die.
   verified visually: on-screen monster 3 -> 2 -> 0 on 2026-10-06. */
int feat_combo(void *mon, void *mstats) {
    if (!ptr_ok(mon) || !ptr_ok(mstats) || !g_m_gtd) return -1;
    fn_inv_t2 inv = feat_inv();
    void *exc = nullptr;
    void *boxed = nullptr;
    int dm = static_cast<int>((g_feats[FEAT_DMG].on ? g_feats[FEAT_DMG].value : 1.0f) * 100000.0f);
    if (dm < 100000) dm = 100000;
    if (dm > 9999999) dm = 9999999;
    void *a2[2] = {&mon, &dm};
    GUARDED_BEGIN();
    boxed = inv(g_m_gtd, nullptr, a2, &exc);
    GUARDED_END();
    if (!ptr_ok(boxed)) return -2;
    static __thread uint8_t cbuf[0x300];
    memcpy(cbuf, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
    if (g_m_dis_notmortal) {
        bool nb = false;
        void *a1[1] = {&nb};
        GUARDED_BEGIN();
        (void) inv(g_m_dis_notmortal, cbuf, a1, &exc);
        GUARDED_END();
    }
    if (g_feats[FEAT_CRIT].on && g_m_dis_crit) {
        bool cb = true;
        void *a1[1] = {&cb};
        GUARDED_BEGIN();
        (void) inv(g_m_dis_crit, cbuf, a1, &exc);
        GUARDED_END();
    }
    void *a1b[1] = {cbuf};
    GUARDED_BEGIN();
    (void) inv(g_m_damage, mstats, a1b, &exc);
    GUARDED_END();
    void *db = nullptr;
    if (g_m_char_getovdb) {
        GUARDED_BEGIN();
        db = inv(g_m_char_getovdb, mon, nullptr, &exc);
        GUARDED_END();
    }
    if (!ptr_ok(db) && g_m_char_getdb) {
        GUARDED_BEGIN();
        db = inv(g_m_char_getdb, mon, nullptr, &exc);
        GUARDED_END();
    }
    if (ptr_ok(db)) {
        if (g_m_mdb_damage) {
            GUARDED_BEGIN();
            (void) inv(g_m_mdb_damage, db, a1b, &exc);
            GUARDED_END();
        }
        if (g_m_mdb_die) {
            GUARDED_BEGIN();
            (void) inv(g_m_mdb_die, db, a1b, &exc);
            GUARDED_END();
        }
    }
    return 0;
}

/* v3.12 one-hit-kill sweep: combo on every real (non-dummy) monster within 20 m of hero */
int feat_pulse_kill() {
    if (!feat_resolve()) return -1;
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) return -2;
    /* hero pos */
    float h_[3] = {0, 0, 0};
    {
        void *exc = nullptr;
        void *stage = nullptr;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(stage) && g_m_stage_getcm) {
            void *cmgr = nullptr;
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cmgr) && g_m_cm_players) {
                void *plist = nullptr;
                GUARDED_BEGIN();
                plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
                GUARDED_END();
                if (ptr_ok(plist)) {
                    int32_t ps = 0;
                    memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                    void **pit = nullptr;
                    memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                    if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                        void *hero = nullptr;
                        memcpy(&hero, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                        if (ptr_ok(hero)) feat_getpos(hero, h_);
                    }
                }
            }
        }
    }
    int hit = 0;
    float R = g_feats[FEAT_AURA].on ? g_feats[FEAT_AURA].value : 20.0f;
    if (R < 5.0f) R = 5.0f;
    if (R > 60.0f) R = 60.0f;
    const float r2 = R * R;
    const bool onehp = g_feats[FEAT_ONEHP].on && !g_feats[FEAT_OHK].on;
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act != 3 && act != 2) || dead != 0) continue;
        float p[3];
        feat_getpos(m, p);
        /* skip placeholder/dummy entities parked far outside the map */
        if (fabsf(p[0]) > 900.0f || fabsf(p[2]) > 900.0f) continue;
        float dx = p[0] - h_[0], dz = p[2] - h_[2];
        if (dx * dx + dz * dz > r2) continue;
        if (onehp) {
            /* ONE-SHOT per toggle: scratch each monster at most once until the
               toggle is reset — prevents repeated application (overkill/death). */
            if (g_scratch_reset) {
                g_scratch_reset = 0;
                g_scratched_n = 0;
            }
            bool seen = false;
            for (int k = 0; k < g_scratched_n; k++) {
                if (g_scratched[k] == m) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
            if (g_scratched_n < 64) g_scratched[g_scratched_n++] = m;
            if (hp <= 1 || !g_m_gtd || !ptr_ok(st)) continue;
            void *exc = nullptr;
            void *boxed = nullptr;
            int dmg = hp - 1;
            void *a2[2] = {&m, &dmg};
            GUARDED_BEGIN();
            boxed = inv(g_m_gtd, nullptr, a2, &exc);
            GUARDED_END();
            if (ptr_ok(boxed)) {
                static __thread uint8_t hbuf[0x300];
                memcpy(hbuf, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
                void *a1b[1] = {hbuf};
                GUARDED_BEGIN();
                (void) inv(g_m_damage, st, a1b, &exc);
                GUARDED_END();
                hit++;
            }
            continue;
        }
        (void) feat_combo(m, st);
        hit++;
    }
    return hit;
}

/* stun sweep: tiny damage + stun flags on real monsters within radius (no kills) */
int feat_pulse_stun() {
    if (!feat_resolve()) return -1;
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) return -2;
    float h_[3] = {0, 0, 0};
    {
        void *exc = nullptr;
        void *stage = nullptr;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(stage) && g_m_stage_getcm) {
            void *cmgr = nullptr;
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cmgr) && g_m_cm_players) {
                void *plist = nullptr;
                GUARDED_BEGIN();
                plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
                GUARDED_END();
                if (ptr_ok(plist)) {
                    int32_t ps = 0;
                    memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                    void **pit = nullptr;
                    memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                    if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                        void *hero = nullptr;
                        memcpy(&hero, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                        if (ptr_ok(hero)) feat_getpos(hero, h_);
                    }
                }
            }
        }
    }
    float R = g_feats[FEAT_AURA].on ? g_feats[FEAT_AURA].value : 20.0f;
    if (R < 5.0f) R = 5.0f;
    if (R > 60.0f) R = 60.0f;
    const float r2 = R * R;
    int hit = 0;
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act != 3 && act != 2) || dead != 0 || !ptr_ok(st)) continue;
        float p[3];
        feat_getpos(m, p);
        if (fabsf(p[0]) > 900.0f || fabsf(p[2]) > 900.0f) continue;
        float dx = p[0] - h_[0], dz = p[2] - h_[2];
        if (dx * dx + dz * dz > r2) continue;
        /* v6.2: pulse follows the selected vector only (g_stun_vec) */
        if (g_stun_vec == 1 && g_m_char_getcb && g_m_ssm_change && g_m_sstate_create) {
            void *exc = nullptr;
            void *cb = nullptr;
            GUARDED_BEGIN();
            cb = inv(g_m_char_getcb, m, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cb)) {
                void *sm = nullptr;
                memcpy(&sm, reinterpret_cast<const uint8_t *>(cb) + 0x58, 8);
                if (ptr_ok(sm)) {
                    float sdur2 = 3.0f;
                    bool ssup2 = true;
                    void *a3s[3] = {&m, &sdur2, &ssup2};
                    void *stt = nullptr;
                    GUARDED_BEGIN();
                    stt = inv(g_m_sstate_create, nullptr, a3s, &exc);
                    GUARDED_END();
                    if (ptr_ok(stt)) {
                        void *a1s[1] = {stt};
                        GUARDED_BEGIN();
                        (void) inv(g_m_ssm_change, sm, a1s, &exc);
                        GUARDED_END();
                        hit++;
                        continue;
                    }
                }
            }
        }
        /* v6.1-B (OnEvent) REMOVED in v6.2 — faulted in probe, suspected crash vector */
        /* v6.2-C: OFFICIAL command (vector 2 only) */
        if (g_stun_vec == 2 && g_m_scmd_create && g_m_scmd_exec) {
            void *exc = nullptr;
            void *cmd = nullptr;
            float sdur = 3.0f;
            bool ssuper = true;
            void *a3c[3] = {&m, &sdur, &ssuper};
            GUARDED_BEGIN();
            cmd = inv(g_m_scmd_create, nullptr, a3c, &exc);
            GUARDED_END();
            if (ptr_ok(cmd)) {
                int ct = 0;
                void *a1x[1] = {&ct};
                GUARDED_BEGIN();
                (void) inv(g_m_scmd_exec, cmd, a1x, &exc);
                GUARDED_END();
                hit++;
                continue; /* locked via official path; skip legacy damage-stun */
            }
        }
        /* v6.2: legacy damage-stun = vector 3 only */
        if (g_stun_vec != 3) continue;
        if (!g_m_gtd) continue;
        void *exc = nullptr;
        void *boxed = nullptr;
        int dmg = 10;
        void *a2[2] = {&m, &dmg};
        GUARDED_BEGIN();
        boxed = inv(g_m_gtd, nullptr, a2, &exc);
        GUARDED_END();
        if (!ptr_ok(boxed)) continue;
        static __thread uint8_t sbuf[0x300];
        memcpy(sbuf, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
        if (g_m_dis_stunf) {
            int sf = 9;
            void *a1[1] = {&sf};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_stunf, sbuf, a1, &exc);
            GUARDED_END();
        }
        if (g_m_dis_stunr) {
            int sr = 1;
            void *a1[1] = {&sr};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_stunr, sbuf, a1, &exc);
            GUARDED_END();
        }
        if (g_m_dis_stund) {
            float sd = 99.0f;
            void *a1[1] = {&sd};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_stund, sbuf, a1, &exc);
            GUARDED_END();
        }
        void *a1b[1] = {sbuf};
        GUARDED_BEGIN();
        (void) inv(g_m_damage, st, a1b, &exc);
        GUARDED_END();
        /* let the monster's damaged-behaviour reaction process the stun */
        void *db = nullptr;
        if (g_m_char_getovdb) {
            GUARDED_BEGIN();
            db = inv(g_m_char_getovdb, m, nullptr, &exc);
            GUARDED_END();
        }
        if (!ptr_ok(db) && g_m_char_getdb) {
            GUARDED_BEGIN();
            db = inv(g_m_char_getdb, m, nullptr, &exc);
            GUARDED_END();
        }
        if (ptr_ok(db) && g_m_mdb_damage) {
            GUARDED_BEGIN();
            (void) inv(g_m_mdb_damage, db, a1b, &exc);
            GUARDED_END();
        }
        hit++;
    }
    return hit;
}

/* drop enemy aggro: per real monster near hero -> battle.ResetAggro(hero) */
int feat_aggro_sweep() {
    if (!feat_resolve() || !g_m_bm_getbattlefor || !g_m_bi_resetaggro) return -1;
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) return -2;
    void *bm = nullptr;
    void *hero = nullptr;
    {
        void *exc = nullptr;
        void *stage = nullptr;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(stage) && g_m_stage_getbm) {
            GUARDED_BEGIN();
            bm = inv(g_m_stage_getbm, stage, nullptr, &exc);
            GUARDED_END();
        }
        if (ptr_ok(stage) && g_m_stage_getcm) {
            void *cmgr = nullptr;
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cmgr) && g_m_cm_players) {
                void *plist = nullptr;
                GUARDED_BEGIN();
                plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
                GUARDED_END();
                if (ptr_ok(plist)) {
                    int32_t ps = 0;
                    memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                    void **pit = nullptr;
                    memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                    if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                        memcpy(&hero, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                    }
                }
            }
        }
    }
    if (!ptr_ok(bm)) return -3;
    int n = 0;
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act != 3 && act != 2) || dead != 0) continue;
        float p[3];
        feat_getpos(m, p);
        if (fabsf(p[0]) > 900.0f || fabsf(p[2]) > 900.0f) continue;
        void *exc = nullptr;
        bool ig = false;
        void *ba[2] = {&m, &ig};
        void *bi = nullptr;
        GUARDED_BEGIN();
        bi = inv(g_m_bm_getbattlefor, bm, ba, &exc);
        GUARDED_END();
        if (ptr_ok(bi)) {
            void *subj = ptr_ok(hero) ? hero : m;
            void *rb[1] = {&subj};
            GUARDED_BEGIN();
            (void) inv(g_m_bi_resetaggro, bi, rb, &exc);
            GUARDED_END();
            n++;
        }
    }
    return n;
}

/* aggro x-ray: one monster + one hero resolution through BattleManager */
void feat_aggro_diag(char *out, size_t cap) {
    if (!feat_resolve() || !g_m_bm_getbattlefor || !g_m_bi_resetaggro) {
        snprintf(out, cap, "AGGRO1 fn-missing");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "AGGRO1 no-list");
        return;
    }
    void *exc = nullptr;
    sig_atomic_t f0 = g_guard_faults;
    void *bm = nullptr;
    void *hero = nullptr;
    {
        void *stage = nullptr;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(stage) && g_m_stage_getbm) {
            GUARDED_BEGIN();
            bm = inv(g_m_stage_getbm, stage, nullptr, &exc);
            GUARDED_END();
        }
        if (ptr_ok(stage) && g_m_stage_getcm) {
            void *cmgr = nullptr;
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(cmgr) && g_m_cm_players) {
                void *plist = nullptr;
                GUARDED_BEGIN();
                plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
                GUARDED_END();
                if (ptr_ok(plist)) {
                    int32_t ps = 0;
                    memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                    void **pit = nullptr;
                    memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                    if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                        memcpy(&hero, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                    }
                }
            }
        }
    }
    sig_atomic_t f1 = g_guard_faults;
    void *mon = nullptr;
    for (int32_t i = 0; i < size && !mon; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act == 3 || act == 2) && dead == 0) mon = m;
    }
    void *bi = nullptr;
    void *bih = nullptr;
    sig_atomic_t fb = f1;
    if (ptr_ok(bm)) {
        bool ig = false;
        if (ptr_ok(mon)) {
            void *ba[2] = {&mon, &ig};
            GUARDED_BEGIN();
            bi = inv(g_m_bm_getbattlefor, bm, ba, &exc);
            GUARDED_END();
        }
        fb = g_guard_faults;
        if (ptr_ok(hero)) {
            bool ig2 = false;
            void *ba2[2] = {&hero, &ig2};
            GUARDED_BEGIN();
            bih = inv(g_m_bm_getbattlefor, bm, ba2, &exc);
            GUARDED_END();
        }
        if (ptr_ok(bi)) {
            void *subj = ptr_ok(hero) ? hero : mon;
            void *rb[1] = {&subj};
            GUARDED_BEGIN();
            (void) inv(g_m_bi_resetaggro, bi, rb, &exc);
            GUARDED_END();
        }
        if (ptr_ok(bih) && bih != bi) {
            void *subj = ptr_ok(hero) ? hero : mon;
            void *rb[1] = {&subj};
            GUARDED_BEGIN();
            (void) inv(g_m_bi_resetaggro, bih, rb, &exc);
            GUARDED_END();
        }
    }
    sig_atomic_t f2 = g_guard_faults;
    snprintf(out, cap,
             "AGGRO1 bm=0x%llx hero=0x%llx mon=0x%llx bi=0x%llx biH=0x%llx | fBm=%d fBi=%d fHi=%d",
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(bm)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hero)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(bi)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(bih)),
             static_cast<int>(f1 - f0), static_cast<int>(fb - f1), static_cast<int>(f2 - fb));
}

/* v6.3: UI-thread state injection — must be called from the game main thread
   (menu_exec path). Applies the state vector around the hero. */
void feat_stunui(char *out, size_t cap) {
    int save = g_stun_vec;
    g_stun_vec = 1;
    int h = feat_pulse_stun();
    g_stun_vec = save;
    snprintf(out, cap, "STUNUI hit=%d vec=state", h);
}

/* POC-1: load the AArch64 payload through the native bridge (libnativebridge.so).
   houdini v3 refuses the deprecated v1 NativeBridgeLoadLibrary ("Shall not invoke
   deprecated interface") — the sanctioned entry is NativeBridgeLoadLibraryExt. */
void feat_payloadrun(char *out, size_t cap) {
    static void *s_nb = nullptr;
    static void *s_ext = nullptr;
    static void *s_base = nullptr;
    if (!s_nb) {
        s_nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_GLOBAL);
        if (s_nb) {
            s_ext = dlsym(s_nb, "NativeBridgeLoadLibraryExt");
            s_base = dlsym(s_nb, "NativeBridgeLoadLibrary");
        }
    }
    /* locate the app libdir via our own maps (robust across reinstalls) */
    char libdir_path[256] = {};
    {
        FILE *mf = fopen("/proc/self/maps", "r");
        if (mf) {
            char line[512];
            while (fgets(line, sizeof line, mf)) {
                char *p = strstr(line, "/libil2cpp.so");
                if (p) {
                    *p = '\0';
                    char *fs = strchr(line, '/');
                    if (fs) snprintf(libdir_path, sizeof libdir_path, "%s/libh64.so", fs);
                    break;
                }
            }
            fclose(mf);
        }
    }
    const char *paths[3] = {
        libdir_path[0] ? libdir_path : "/nonexistent",
        "/storage/emulated/0/Android/data/com.kakaogames.gdts/files/libh64.so",
        "/data/adb/modules/wsm_gt/payload/libh64.so",
    };
    size_t used = snprintf(out, cap, "PAYLOAD ext=0x%llx base=0x%llx",
                           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(s_ext)),
                           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(s_base)));
    if (!s_ext && !s_base) {
        snprintf(out + used, cap - used, " no-symbols");
        return;
    }
    typedef void *(*fn_ext_t)(const char *, int, const void *, const void *);
    typedef void *(*fn_base_t)(const char *, int);
    fn_ext_t fext = nullptr;
    fn_base_t fbase = nullptr;
    if (s_ext) memcpy(&fext, &s_ext, sizeof fext);
    if (s_base) memcpy(&fbase, &s_base, sizeof fbase);
    bool loaded = false;
    for (int i = 0; i < 3 && !loaded && used + 140 < cap; i++) {
        void *h = nullptr;
        int e = 0;
        if (fext) {
            errno = 0;
            GUARDED_BEGIN();
            h = fext(paths[i], 2, nullptr, nullptr);
            GUARDED_END();
            e = errno;
            if (ptr_ok(h)) loaded = true;
        }
        if (!loaded && fbase && i == 0) {
            errno = 0;
            GUARDED_BEGIN();
            h = fbase(paths[i], 2);
            GUARDED_END();
            e = errno;
            if (ptr_ok(h)) loaded = true;
        }
        used += static_cast<size_t>(
            snprintf(out + used, cap - used, " |[%d]%s h=0x%llx e=%d", i,
                     ptr_ok(h) ? "OK" : "no",
                     static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)), e));
    }
}

/* v6.4-poc: foreign-arch load test — mekanisme SAMA dengan zygisk memuat module libs:
   android_dlopen_ext("/jit-cache", .., {USE_LIBRARY_FD}) — bionic bisa menyerahkan
   ELF asing ke native bridge (houdini). Tes: nifuji arm64 vs libh64 kita. */
struct nb_dlextinfo { uint64_t flags; void *reserved_addr; size_t reserved_size; int relro_fd; int library_fd; int64_t library_fd_offset; void *library_namespace; };
static void *g_h64_handle = nullptr; /* POC-2: handle arm64 payload dari bridge */
void feat_nbdl(char *out, size_t cap, const char *arg) {
    typedef void *(*adext_t)(const char *, int, const void *);
    typedef void *(*fn_ext_t)(const char *, int, void *, const void *);
    typedef bool (*fn_init_t)();
    static adext_t s_adext = nullptr;
    static fn_ext_t s_fext = nullptr;
    static fn_init_t s_init = nullptr;
    static fn_init_t s_base_init = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        void *ld = dlopen("libdl.so", RTLD_NOW | RTLD_GLOBAL);
        if (ld) { void *s = dlsym(ld, "android_dlopen_ext"); memcpy(&s_adext, &s, sizeof s_adext); }
        void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_GLOBAL);
        if (nb) {
            void *s1 = dlsym(nb, "NativeBridgeLoadLibraryExt");
            void *s2 = dlsym(nb, "NativeBridgeInitialized");
            void *s3 = dlsym(nb, "NativeBridgeGetNativeBridgeCallbacks");
            memcpy(&s_fext, &s1, sizeof s_fext);
            memcpy(&s_init, &s2, sizeof s_init);
            memcpy(&s_base_init, &s3, sizeof s_base_init);
        }
    }
    static const char *defs[4] = {
        "/data/data/com.kakaogames.gdts/files/libh64.so",
        "/data/data/com.kakaogames.gdts/files/nj64.so",
        "/storage/emulated/0/Android/data/com.kakaogames.gdts/files/libh64.so",
        "/data/local/tmp/libh64.so",
    };
    const char *paths[4];
    int n = 4;
    if (arg && arg[0]) {
        /* trim leading spaces */
        while (*arg == ' ') arg++;
        paths[0] = arg;
        n = 1;
    } else {
        for (int i = 0; i < 4; i++) paths[i] = defs[i];
    }
    size_t used = snprintf(out, cap, "NBDL ext=%d fext=%d init=%d",
                           s_adext ? 1 : 0, s_fext ? 1 : 0,
                           s_init ? (s_init() ? 1 : 0) : -1);
    for (int i = 0; i < n && used + 200 < cap; i++) {
        void *h1 = nullptr; int e1 = 0;
        errno = 0;
        GUARDED_BEGIN();
        h1 = dlopen(paths[i], RTLD_LAZY | RTLD_LOCAL);
        GUARDED_END();
        e1 = errno;
        void *h2 = nullptr; int e2 = 0;
        int fd = open(paths[i], O_RDONLY | O_CLOEXEC);
        if (fd >= 0 && s_adext) {
            nb_dlextinfo info;
            memset(&info, 0, sizeof info);
            info.flags = 0x10; /* ANDROID_DLEXT_USE_LIBRARY_FD */
            info.library_fd = fd;
            errno = 0;
            GUARDED_BEGIN();
            h2 = s_adext("/jit-cache", RTLD_LAZY | RTLD_LOCAL, &info);
            GUARDED_END();
            e2 = errno;
        } else if (fd < 0) {
            e2 = errno;
        }
        void *h3 = nullptr; int e3 = 0;
        if (fd >= 0 && s_fext) {
            nb_dlextinfo info3;
            memset(&info3, 0, sizeof info3);
            info3.flags = 0x10;
            info3.library_fd = fd;
            errno = 0;
            GUARDED_BEGIN();
            h3 = s_fext("/jit-cache", 2, nullptr, &info3);
            GUARDED_END();
            e3 = errno;
        }
        void *h4 = nullptr; int e4 = 0;
        if (s_fext) {
            errno = 0;
            GUARDED_BEGIN();
            h4 = s_fext(paths[i], 2, nullptr, nullptr);
            GUARDED_END();
            e4 = errno;
        }
        if (ptr_ok(h4)) g_h64_handle = h4;
        used += static_cast<size_t>(snprintf(out + used, cap - used,
            " |%s fd=%d dl=%s e%d ad=%s e%d bA=%s e%d bB=%s e%d", paths[i], fd,
            ptr_ok(h1) ? "OK" : "no", e1,
            ptr_ok(h2) ? "OK" : "no", e2,
            ptr_ok(h3) ? "OK" : "no", e3,
            ptr_ok(h4) ? "OK" : "no", e4));
        if (fd >= 0) close(fd);
    }
}

/* POC-2: x86→arm64 via NativeBridgeGetTrampoline + bus data roundtrip (arm64→x86). */
static uint64_t g_h64_bus[WSM_BUS_WORDS];
void feat_nbpoc2(char *out, size_t cap) {
    typedef void *(*fn_ext2_t)(const char *, int, void *, const void *);
    /* reset bus + publikasikan pointer utk ctor arm64 (baca dari file) */
    memset(g_h64_bus, 0, sizeof g_h64_bus);
    size_t used = snprintf(out, cap, "POC2B");
    {
        FILE *bf = fopen("/storage/emulated/0/Android/data/com.kakaogames.gdts/files/h64_bus.txt", "w");
        if (bf) {
            fprintf(bf, "0x%llx\n", (unsigned long long)(uintptr_t)&g_h64_bus[0]);
            fclose(bf);
            used += snprintf(out + used, cap - used, " busfile=1");
        } else {
            used += snprintf(out + used, cap - used, " busfile=0");
        }
    }
    if (!g_h64_handle) {
        char p[512] = {}; bool found = false;
        FILE *mf = fopen("/proc/self/maps", "r");
        if (mf) {
            char line[1024];
            while (fgets(line, sizeof line, mf)) {
                char *q = strstr(line, "/libil2cpp.so");
                if (q) {
                    *q = 0;
                    char *fs = strchr(line, '/');
                    if (fs) { snprintf(p, sizeof p, "%s/libh64z.so", fs); found = true; }
                    break;
                }
            }
            fclose(mf);
        }
        if (found) {
            void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_GLOBAL);
            void *sf = nb ? dlsym(nb, "NativeBridgeLoadLibraryExt") : nullptr;
            fn_ext2_t fext = nullptr; memcpy(&fext, &sf, sizeof fext);
            errno = 0;
            if (fext) { GUARDED_BEGIN(); g_h64_handle = fext(p, 2, nullptr, nullptr); GUARDED_END(); }
            used += snprintf(out + used, cap - used, " load h=0x%llx e=%d",
                             (unsigned long long)(uintptr_t)g_h64_handle, errno);
        } else {
            used += snprintf(out + used, cap - used, " nopath");
        }
    }
    if (!g_h64_handle) { ELOGI("POC2B: no handle after load attempt"); return; }
    ELOGI("POC2B: handle=%p", g_h64_handle);
    /* tunggu thread arm64 hidup (heartbeat bus[10]) */
    unsigned long long hb = 0;
    for (int i = 0; i < 400 && !hb; i++) { usleep(5000); hb = g_h64_bus[10]; }
    used += snprintf(out + used, cap - used, " thread=%s hb=%llu tid=%llu mag=0x%llx",
                     hb ? "UP" : "DOWN", hb,
                     (unsigned long long)g_h64_bus[2], (unsigned long long)g_h64_bus[3]);
    ELOGI("POC2B thread=%s hb=%llu tmag=0x%llx", hb ? "UP" : "DOWN", hb,
          (unsigned long long)g_h64_bus[11]);
    /* cmd ping */
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 1;
    for (int i = 0; i < 200 && g_h64_bus[1] != 2; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " ping=%d pid=%llu m=0x%llx",
                     (int)g_h64_bus[1], (unsigned long long)g_h64_bus[2],
                     (unsigned long long)g_h64_bus[3]);
    /* cmd compute 6x7 */
    g_h64_bus[4] = 6;
    g_h64_bus[5] = 7;
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 2;
    for (int i = 0; i < 200 && g_h64_bus[1] != 3; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " mul=%llu hb2=%llu",
                     (unsigned long long)g_h64_bus[6], (unsigned long long)g_h64_bus[10]);
    ELOGI("POC2B done: %s", out);
}

/* POC-3: agent memory ops + LIVE ARM64 CODE PATCH (mprotect→patch→call→restore). */
static uint64_t g_poc3_nonce = 0;
static uint64_t g_poc3_target = 0;
void feat_nbpoc3(char *out, size_t cap) {
    typedef void *(*fn_ext3_t)(const char *, int, void *, const void *);
    memset(g_h64_bus, 0, sizeof g_h64_bus);
    size_t used = snprintf(out, cap, "POC3");
    {
        FILE *bf = fopen("/storage/emulated/0/Android/data/com.kakaogames.gdts/files/h64_bus.txt", "w");
        if (bf) {
            fprintf(bf, "0x%llx\n", (unsigned long long)(uintptr_t)&g_h64_bus[0]);
            fclose(bf);
        }
    }
    if (!g_h64_handle) {
        char p[512] = {}; bool found = false;
        FILE *mf = fopen("/proc/self/maps", "r");
        if (mf) {
            char line[1024];
            while (fgets(line, sizeof line, mf)) {
                char *q = strstr(line, "/libil2cpp.so");
                if (q) {
                    *q = 0;
                    char *fs = strchr(line, '/');
                    if (fs) { snprintf(p, sizeof p, "%s/libh64z.so", fs); found = true; }
                    break;
                }
            }
            fclose(mf);
        }
        if (found) {
            void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_GLOBAL);
            void *sf = nb ? dlsym(nb, "NativeBridgeLoadLibraryExt") : nullptr;
            fn_ext3_t fext = nullptr; memcpy(&fext, &sf, sizeof fext);
            errno = 0;
            if (fext) { GUARDED_BEGIN(); g_h64_handle = fext(p, 2, nullptr, nullptr); GUARDED_END(); }
            used += snprintf(out + used, cap - used, " load=0x%llx e=%d",
                             (unsigned long long)(uintptr_t)g_h64_handle, errno);
        }
    }
    if (!g_h64_handle) { used += snprintf(out + used, cap - used, " NOHANDLE"); return; }
    unsigned long long hb = 0;
    for (int i = 0; i < 400 && !hb; i++) { usleep(5000); hb = g_h64_bus[10]; }
    if (!hb) { used += snprintf(out + used, cap - used, " THREAD_DOWN"); return; }
    /* A: agent READ64 of our nonce */
    g_poc3_nonce = 0x5EEDF00DCAFEBABEULL;
    g_h64_bus[1] = 0; g_h64_bus[4] = (uint64_t)(uintptr_t)&g_poc3_nonce; g_h64_bus[0] = 3;
    for (int i = 0; i < 200 && g_h64_bus[1] != 3; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " | A:rd=0x%llx%s",
                     (unsigned long long)g_h64_bus[6],
                     g_h64_bus[6] == g_poc3_nonce ? "(OK)" : "(BAD)");
    /* B: agent WRITE64 into our scratch */
    g_poc3_target = 0;
    g_h64_bus[1] = 0; g_h64_bus[4] = (uint64_t)(uintptr_t)&g_poc3_target;
    g_h64_bus[5] = 0xB0BAF00DDEADBEEFULL; g_h64_bus[0] = 4;
    for (int i = 0; i < 200 && g_h64_bus[1] != 3; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " | B:wr=%s", 
                     g_poc3_target == 0xB0BAF00DDEADBEEFULL ? "OK" : "BAD");
    /* C: agent READ64 of libil2cpp base (ELF magic) */
    {
        uint64_t base = 0;
        FILE *mf = fopen("/proc/self/maps", "r");
        if (mf) {
            char line[1024];
            while (fgets(line, sizeof line, mf)) {
                if (strstr(line, "/libil2cpp.so")) {
                    unsigned long long a = 0;
                    if (sscanf(line, "%llx", &a) == 1) base = a;
                    break;
                }
            }
            fclose(mf);
        }
        g_h64_bus[1] = 0; g_h64_bus[4] = base; g_h64_bus[0] = 3;
        for (int i = 0; i < 200 && g_h64_bus[1] != 3; i++) usleep(5000);
        uint64_t direct = base ? *(volatile uint64_t *)(uintptr_t)base : 0;
        used += snprintf(out + used, cap - used, " | C:elf=0x%llx vs x86=0x%llx%s",
                         (unsigned long long)g_h64_bus[6], (unsigned long long)direct,
                         (g_h64_bus[6] == direct && direct != 0) ? "(MATCH)" : "(DIFF)");
    }
    /* D: PATCHSELF — live arm64 code patch + call + restore */
    g_h64_bus[1] = 0; g_h64_bus[0] = 5;
    for (int i = 0; i < 1200 && g_h64_bus[1] != 3; i++) usleep(5000);
    used += snprintf(out + used, cap - used,
                     " | D:old=0x%llx ret1=0x%llx nm=%lld nv=%lld"
                     " | E:victim old=0x%llx firstcall=0x%llx"
                     " | MC: hitm=%lld hitv=%lld regions=%lld first=0x%llx",
                     (unsigned long long)g_h64_bus[6], (unsigned long long)g_h64_bus[8],
                     (long long)g_h64_bus[9], (long long)(g_h64_bus[13] - g_h64_bus[9]),
                     (unsigned long long)g_h64_bus[30], (unsigned long long)g_h64_bus[31],
                     (long long)g_h64_bus[40], (long long)g_h64_bus[41],
                     (long long)g_h64_bus[42], (unsigned long long)g_h64_bus[43]);
    ELOGI("POC3 done: %s", out);
    ELOGI("POC3 DBG p=0x%llx base=0x%llx F=0x%llx | m0=0x%llx/o0x%llx/s0x%llx m1=0x%llx/o0x%llx m2=0x%llx/o0x%llx m3=0x%llx/o0x%llx",
          (unsigned long long)g_h64_bus[18], (unsigned long long)g_h64_bus[19],
          (unsigned long long)g_h64_bus[20],
          (unsigned long long)g_h64_bus[21], (unsigned long long)g_h64_bus[25],
          (unsigned long long)g_h64_bus[29],
          (unsigned long long)g_h64_bus[22], (unsigned long long)g_h64_bus[26],
          (unsigned long long)g_h64_bus[23], (unsigned long long)g_h64_bus[27],
          (unsigned long long)g_h64_bus[24], (unsigned long long)g_h64_bus[28]);
}

/* POC-4: dump methodPointer + prologue bytes kandidat fungsi game (baca direct x86). */
void feat_hookdump(char *out, size_t cap) {
    struct Cand { const char *name; void *mi; };
    Cand cands[] = {
        {"getpos", g_m_char_getpos},
        {"gethp", g_m_stats_gethp},
        {"setpos2", g_m_char_setpos2},
        {"damage", g_m_mdb_damage},
        {"die", g_m_mdb_die},
    };
    feat_resolve(); /* ensure g_m_* terisi */
    size_t used = snprintf(out, cap, "HOOKDUMP");
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]) && used + 130 < cap; i++) {
        if (!cands[i].mi) continue;
        uint64_t code = 0;
        GUARDED_BEGIN();
        if (ptr_ok(cands[i].mi)) code = *(uint64_t *)((char *)cands[i].mi + 0x10);
        GUARDED_END();
        used += snprintf(out + used, cap - used, " | %s mi=%llx code=%llx",
                         cands[i].name, (unsigned long long)(uintptr_t)cands[i].mi,
                         (unsigned long long)code);
        if (ptr_ok((void *)(uintptr_t)code) && used + 90 < cap) {
            uint32_t w[6] = {};
            GUARDED_BEGIN();
            memcpy(w, (void *)(uintptr_t)code, 24);
            GUARDED_END();
            used += snprintf(out + used, cap - used, " b=%08x %08x %08x %08x %08x %08x",
                             w[0], w[1], w[2], w[3], w[4], w[5]);
        }
    }
}

/* POC-4: pasang trampoline counter di MonsterDamageBehaviour.Damage (game!), hitung, restore. */
void feat_hookcount(char *out, size_t cap, const char *arg) {
    int secs = 15;
    if (arg && arg[0]) {
        int s = atoi(arg);
        if (s > 0 && s <= 300) secs = s;
    }
    typedef void *(*fn_ext4_t)(const char *, int, void *, const void *);
    memset(g_h64_bus, 0, sizeof g_h64_bus);
    size_t used = snprintf(out, cap, "HOOKCOUNT secs=%d", secs);
    {
        FILE *bf = fopen("/storage/emulated/0/Android/data/com.kakaogames.gdts/files/h64_bus.txt", "w");
        if (bf) {
            fprintf(bf, "0x%llx\n", (unsigned long long)(uintptr_t)&g_h64_bus[0]);
            fclose(bf);
        }
    }
    if (!g_h64_handle) {
        char p[512] = {}; bool found = false;
        FILE *mf = fopen("/proc/self/maps", "r");
        if (mf) {
            char line[1024];
            while (fgets(line, sizeof line, mf)) {
                char *q = strstr(line, "/libil2cpp.so");
                if (q) {
                    *q = 0;
                    char *fs = strchr(line, '/');
                    if (fs) { snprintf(p, sizeof p, "%s/libh64z.so", fs); found = true; }
                    break;
                }
            }
            fclose(mf);
        }
        if (found) {
            void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_GLOBAL);
            void *sf = nb ? dlsym(nb, "NativeBridgeLoadLibraryExt") : nullptr;
            fn_ext4_t fext = nullptr; memcpy(&fext, &sf, sizeof fext);
            errno = 0;
            if (fext) { GUARDED_BEGIN(); g_h64_handle = fext(p, 2, nullptr, nullptr); GUARDED_END(); }
            used += snprintf(out + used, cap - used, " load=0x%llx e=%d",
                             (unsigned long long)(uintptr_t)g_h64_handle, errno);
        }
    }
    if (!g_h64_handle) { used += snprintf(out + used, cap - used, " NOHANDLE"); return; }
    unsigned long long hb = 0;
    for (int i = 0; i < 400 && !hb; i++) { usleep(5000); hb = g_h64_bus[10]; }
    if (!hb) { used += snprintf(out + used, cap - used, " THREAD_DOWN"); return; }
    feat_resolve();
    void *mi = g_m_mdb_damage;
    {
        const char *t = arg;
        if (t) {
            while (*t == ' ') t++;
            while (*t >= '0' && *t <= '9') t++;
            while (*t == ' ') t++;
            if (*t == 's') mi = g_m_damage;
            else if (*t == 'm') mi = g_m_mdb_damage;
        }
    }
    uint64_t code = 0;
    GUARDED_BEGIN();
    if (ptr_ok(mi)) code = *(uint64_t *)mi;
    GUARDED_END();
    if (!code) { used += snprintf(out + used, cap - used, " NOCODE"); return; }
    used += snprintf(out + used, cap - used, " code=0x%llx", (unsigned long long)code);
    g_h64_bus[1] = 0;
    g_h64_bus[60] = code;
    g_h64_bus[0] = 6;
    for (int i = 0; i < 400 && g_h64_bus[1] != 5; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " nmaps=%lld patched=%lld | INSTALLED, tunggu %ds...",
                     (long long)g_h64_bus[68], (long long)g_h64_bus[69], secs);
    ELOGI("HOOKCOUNT: %s", out);
    for (int t = 0; t < secs; t++) sleep(1); /* interrupt-proof */
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 7;
    for (int i = 0; i < 400 && g_h64_bus[1] != 6; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " | HOTCOUNT=%llu", (unsigned long long)g_h64_bus[70]);
    ELOGI("HOOKCOUNT done: %s", out);
}

/* POC-4: self-test trampoline di payload (CMD8) — bukti mekanik. */
void feat_hookself(char *out, size_t cap) {
    typedef void *(*fn_ext5_t)(const char *, int, void *, const void *);
    memset(g_h64_bus, 0, sizeof g_h64_bus);
    {
        FILE *bf = fopen("/storage/emulated/0/Android/data/com.kakaogames.gdts/files/h64_bus.txt", "w");
        if (bf) {
            fprintf(bf, "0x%llx\n", (unsigned long long)(uintptr_t)&g_h64_bus[0]);
            fclose(bf);
        }
    }
    if (!g_h64_handle) {
        char p[512] = {}; bool found = false;
        FILE *mf = fopen("/proc/self/maps", "r");
        if (mf) {
            char line[1024];
            while (fgets(line, sizeof line, mf)) {
                char *q = strstr(line, "/libil2cpp.so");
                if (q) {
                    *q = 0;
                    char *fs = strchr(line, '/');
                    if (fs) { snprintf(p, sizeof p, "%s/libh64z.so", fs); found = true; }
                    break;
                }
            }
            fclose(mf);
        }
        if (found) {
            void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_GLOBAL);
            void *sf = nb ? dlsym(nb, "NativeBridgeLoadLibraryExt") : nullptr;
            fn_ext5_t fext = nullptr; memcpy(&fext, &sf, sizeof fext);
            errno = 0;
            if (fext) { GUARDED_BEGIN(); g_h64_handle = fext(p, 2, nullptr, nullptr); GUARDED_END(); }
        }
    }
    if (!g_h64_handle) { snprintf(out, cap, "HOOKSELF NOHANDLE"); return; }
    unsigned long long hb = 0;
    for (int i = 0; i < 400 && !hb; i++) { usleep(5000); hb = g_h64_bus[10]; }
    if (!hb) { snprintf(out, cap, "HOOKSELF THREAD_DOWN"); return; }
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 8;
    for (int i = 0; i < 400 && g_h64_bus[1] != 7; i++) usleep(5000);
    snprintf(out, cap, "HOOKSELF count=%llu (harap 3) sum=0x%llx restored=0x%llx np=%lld",
             (unsigned long long)g_h64_bus[71], (unsigned long long)g_h64_bus[72],
             (unsigned long long)g_h64_bus[73], (long long)g_h64_bus[74]);
    ELOGI("HOOKSELF: %s", out);
}

/* ---- POC-4 v3: hookall + hookread + autohook ---- */
static bool hook_ensure_payload(void) {
    if (g_bus_unhealthy) return false;
    if (!g_h64_handle) {
        const char *fd_env = getenv("WSM_ARM64_FD");
        if (!fd_env) return false;
        char *end = nullptr; long fd = strtol(fd_env, &end, 10);
        if (!end || *end || fd < 0 || fd > 1048576) return false;
        memset(g_h64_bus, 0, sizeof g_h64_bus);
        g_h64_bus[WSM_BUS_IDENTITY] = WSM_BUS_MAGIC;
        g_h64_bus[WSM_BUS_VERSION] = WSM_BUS_PROTOCOL;
        g_h64_bus[WSM_BUS_PID] = (uint64_t)getpid();
        const char *nonce = getenv("WSM_NONCE");
        g_h64_bus[WSM_BUS_NONCE] = nonce ? strtoull(nonce, nullptr, 16) : 0;
        char bus_env[32]; snprintf(bus_env, sizeof bus_env, "%llx", (unsigned long long)(uintptr_t)g_h64_bus);
        setenv("WSM_H64_BUS", bus_env, 1);
        char path[64]; snprintf(path, sizeof path, "/proc/self/fd/%ld", fd);
#if defined(__aarch64__)
        g_h64_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#else
        void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_LOCAL);
        void *sf = nb ? dlsym(nb, "NativeBridgeLoadLibraryExt") : nullptr;
        typedef void *(*fn_ext_t)(const char *, int, void *, const void *);
        fn_ext_t fext = nullptr; memcpy(&fext, &sf, sizeof fext);
        if (fext) { GUARDED_BEGIN(); g_h64_handle = fext(path, RTLD_NOW, nullptr, nullptr); GUARDED_END(); }
#endif
        if (!g_h64_handle) { ELOGI("PAYLOAD unavailable: %s", dlerror()); return false; }
        // The mapping remains alive; close this descriptor only after the constructor runs.
        close((int)fd); unsetenv("WSM_ARM64_FD"); unsetenv("WSM_H64_BUS");
    }
    for (int i = 0; i < 400 && !__atomic_load_n(&g_h64_bus[10], __ATOMIC_ACQUIRE); ++i) usleep(5000);
    return g_h64_bus[2] == (uint64_t)getpid() && g_h64_bus[3] == 0xA864CAFEULL && g_h64_bus[10];
}

static int hook_install_slot(int slot, void *mi, unsigned long long *code_out) {
    uint64_t code = 0;
    GUARDED_BEGIN();
    if (ptr_ok(mi)) code = *(uint64_t *)mi;
    GUARDED_END();
    if (code_out) *code_out = code;
    if (!code) return -1;
    g_h64_bus[1] = 0;
    g_h64_bus[59] = (uint64_t)slot;
    g_h64_bus[60] = code;
    g_h64_bus[0] = 6;
    for (int i = 0; i < 400 && g_h64_bus[1] != 5; i++) usleep(5000);
    int rc = (int)(int64_t)g_h64_bus[68];
    int np = (int)g_h64_bus[69];
    return (rc < 0) ? rc : np;
}

/* v28: pasang hook di ALAMAT ABSOLUT (lewat bus yang sama) */
static int hook_install_abs(int slot, unsigned long long addr) {
    g_h64_bus[1] = 0;
    g_h64_bus[59] = (uint64_t)slot;
    g_h64_bus[60] = addr;
    g_h64_bus[0] = 6;
    for (int i = 0; i < 400 && g_h64_bus[1] != 5; i++) usleep(5000);
    int rc = (int)(int64_t)g_h64_bus[68];
    int np = (int)g_h64_bus[69];
    return (rc < 0) ? rc : np;
}

/* v28b: TOLAK target yang 16 byte pertamanya mengandung instruksi relatif-PC.
   Blok kita mengeksekusi orig16 di ALAMAT BLOK (halaman lain) — instruksi relatif-PC
   (adrp/adr/ldr-literal/b/bl/b.cond/cbz/tbz/br/blr) akan menghitung alamat SALAH saat
   dieksekusi dari blok → lompat ke sampah → houdini SIGSEGV 0xdead1007 (pembelajaran GT!).
   `ret` (0xD65F0000-class) DIIZINKAN: fungsi mini {body; ret} tetap benar saat di-relokasi. */
static bool reloc_unsafe(uint64_t code) {
    if (!code) return true;
    uint32_t w[4] = {0, 0, 0, 0};
    GUARDED_BEGIN();
    for (int i = 0; i < 4; i++) w[i] = *(volatile uint32_t *)(uintptr_t)(code + (uint64_t)i * 4);
    GUARDED_END();
    for (int i = 0; i < 4; i++) {
        uint32_t x = w[i];
        /* v29b: kalau 8 byte pertama = PATCH MILIK KITA (ldr x17; br x17), berarti target sudah
           terpasang & orig-nya sudah tervalidasi saat di-save → aman (bukan prologue asli). */
        if (i == 0 && w[0] == 0x58000051u && w[1] == 0xD61F0220u) return false;
        if ((x & 0x9F000000u) == 0x90000000u) return true; /* adrp */
        if ((x & 0x9F000000u) == 0x10000000u) return true; /* adr */
        if ((x & 0xFF000000u) == 0x58000000u) return true; /* ldr lit 64 */
        if ((x & 0xFF000000u) == 0x18000000u) return true; /* ldr lit 32 */
        if ((x & 0xFC000000u) == 0x14000000u) return true; /* b */
        if ((x & 0xFC000000u) == 0x94000000u) return true; /* bl */
        if ((x & 0xFF000010u) == 0x54000000u) return true; /* b.cond */
        if ((x & 0x7F000000u) == 0x34000000u) return true; /* cbz */
        if ((x & 0x7F000000u) == 0x35000000u) return true; /* cbnz */
        if ((x & 0x7F000000u) == 0x36000000u) return true; /* tbz */
        if ((x & 0x7F000000u) == 0x37000000u) return true; /* tbnz */
        if ((x & 0xFFFFFC1Fu) == 0xD61F0000u) return true; /* br */
        if ((x & 0xFFFFFC1Fu) == 0xD63F0000u) return true; /* blr */
    }
    return false;
}

/* v28: VALIDASI RESOLVER — tiap alamat hasil resolver harus == dump absolute (non-ASLR).
   Log sekali saat semua target kunci tersedia; mismatch = sinyal resolver nyasar (lihat pelajaran A2). */
static void resolv_check_once(void) {
    static int done = 0;
    if (done) return;
    struct { void *mi; unsigned long long abs; const char *lbl; } t[] = {
        { g_m_fos_applydmg,  0x400028FB64B4ULL, "FOS.ApplyDamage" },
        { g_m_fos_damage,    0x400028FB7398ULL, "FOS.Damage" },
        { g_m_fos_checkdie,  0x400028FB63ACULL, "FOS.CheckWillDie" },
        { g_m_damage,        0x40002B88C2C4ULL, "CSB.Damage" },
        { g_m_odr,           0x40002B88CCBCULL, "CSB.OnDamageRecorder" },
        { g_m_stage_inst,    0x4000297AFA08ULL, "Stage.get_Instance" },
        { g_m_cm_players,    0x400028CA32F0ULL, "CM.GetAllPlayers" },
        { g_m_char_getstats, 0x40002B82F8D4ULL, "Char.get_CSB" },
    };
    int n = (int)(sizeof t / sizeof t[0]), have = 0;
    for (int i = 0; i < n; i++) {
        if (!t[i].mi) continue;
        have++;
        uint64_t code = 0;
        GUARDED_BEGIN();
        if (ptr_ok(t[i].mi)) code = *(uint64_t *)((char *)t[i].mi + 0x10);
        GUARDED_END();
        if (code != t[i].abs)
            ELOGI("RESOLVCHECK %s: resolved=0x%llx expect=0x%llx MISMATCH", t[i].lbl,
                  (unsigned long long)code, t[i].abs);
        else
            ELOGI("RESOLVCHECK %s: OK 0x%llx", t[i].lbl, (unsigned long long)code);
    }
    if (have == n) done = 1;
}

[[maybe_unused]] static void *autohook_thread(void *) {
    /* v3: pasang hook ke SEMUA kandidat damage sedini mungkin (sebelum game memanggilnya).
       Gate: marker di folder app (namespace-game visible) atau modules path. */
    if (access("/storage/emulated/0/Android/data/com.kakaogames.gdts/files/autohook", F_OK) != 0 &&
        access("/data/adb/modules/wsm_gt/autohook", F_OK) != 0) {
        ELOGI("AUTOHOOK v3: marker tidak ada, skip (stealth OK)");
        return nullptr;
    }
    for (int tries = 0; tries < 12000; tries++) {
        usleep(25000); /* poll 25ms: pasang sedini mungkin setelah il2cpp muncul */
        if (!hook_ensure_payload()) continue;
        if (!feat_resolve()) continue;
        resolv_check_once();
        int ok = 0, skipped = 0; char msg[1280] = {};
        size_t mu = 0;
        for (int s = 0; s < NHOOKT; s++) {
            unsigned long long c = 0;
            void *mi = *g_hook_targets[s].ptr;
            GUARDED_BEGIN();
            if (ptr_ok(mi)) c = *(uint64_t *)((char *)mi + 0x10);
            GUARDED_END();
            int np;
            if (!c) {
                np = -1;
            } else {
                unsigned long long target = c;
                bool direct = false;
                if (g_hook_abs[s] && c != g_hook_abs[s]) {
                    target = g_hook_abs[s]; direct = true;
                    ELOGI("AUTOHOOK SKIP-RESOLVE [%d]%s resolved=0x%llx -> DUMP 0x%llx (direct, no rebind)", s,
                          g_hook_targets[s].label, c, target);
                }
                if (reloc_unsafe(target)) {
                    ELOGI("AUTOHOOK SKIP-UNSAFE [%d]%s @0x%llx: prologue relatif-PC (tidak di-relokasi)", s,
                          g_hook_targets[s].label, target);
                    np = -5; skipped++;
                } else if (direct) {
                    np = hook_install_abs(s, target);
                } else {
                    np = hook_install_slot(s, mi, &c);
                }
                c = target;
            }
            if (np >= 0) ok++;
            mu += snprintf(msg + mu, sizeof msg - mu, " [%d]%s/%d@0x%llx", s,
                           g_hook_targets[s].label, np, c);
        }
        ELOGI("AUTOHOOK v28b: ok=%d/%d skip-unsafe=%d%s", ok, NHOOKT, skipped, msg);
        return nullptr;
    }
    ELOGI("AUTOHOOK v28b: GAVE UP");
    return nullptr;
}

/* v3: hookall — pasang 4 kandidat sekaligus, tunggu, restore, lapor counts */
void feat_hookall(char *out, size_t cap, const char *arg) {
    int secs = 200;
    if (arg && arg[0]) {
        int s = atoi(arg);
        if (s > 0 && s <= 300) secs = s;
    }
    if (!hook_ensure_payload()) { snprintf(out, cap, "HOOKALL NOHANDLE/DOWN"); return; }
    if (!feat_resolve()) { snprintf(out, cap, "HOOKALL resolve=0"); return; }
    resolv_check_once();
    size_t used = 0;
    for (int s = 0; s < NHOOKT; s++) {
        unsigned long long c = 0;
        void *mi = *g_hook_targets[s].ptr;
        GUARDED_BEGIN();
        if (ptr_ok(mi)) c = *(uint64_t *)((char *)mi + 0x10);
        GUARDED_END();
        int np;
        if (!c) {
            np = -1;
        } else {
            unsigned long long target = c;
            bool direct = false;
            if (g_hook_abs[s] && c != g_hook_abs[s]) {
                target = g_hook_abs[s]; direct = true;
                ELOGI("HOOKALL SKIP-RESOLVE [%d]%s resolved=0x%llx -> DUMP 0x%llx", s,
                      g_hook_targets[s].label, c, target);
            }
            if (reloc_unsafe(target)) {
                ELOGI("HOOKALL SKIP-UNSAFE [%d]%s @0x%llx: prologue relatif-PC", s,
                      g_hook_targets[s].label, target);
                np = -5;
            } else if (direct) {
                np = hook_install_abs(s, target);
            } else {
                np = hook_install_slot(s, mi, &c);
            }
            c = target;
        }
        used += snprintf(out + used, cap - used, " [%d]%s/%d@0x%llx", s, g_hook_targets[s].label, np, c);
    }
    used += snprintf(out + used, cap - used, " | INSTALLED, tunggu %ds...", secs);
    ELOGI("HOOKALL: %s", out);
    for (int t = 0; t < secs; t++) sleep(1);
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 7;
    for (int i = 0; i < 400 && g_h64_bus[1] != 6; i++) usleep(5000);
    used += snprintf(out + used, cap - used, " | c=[");
    for (int s = 0; s < NHOOKT; s++)
        used += snprintf(out + used, cap - used, "%s%llu", s ? "," : "",
                         (unsigned long long)g_h64_bus[70 + s]);
    used += snprintf(out + used, cap - used, "]");
    ELOGI("HOOKALL done: %s", out);
}

/* v3: kname — baca "ns.name" dari objek il2cpp mana pun (klass@0 -> name@0x10, ns@0x18) */
static void kname(void *obj, char *out, size_t cap) {
    snprintf(out, cap, "?");
    if (!ptr_ok(obj)) return;
    void *kl = nullptr;
    GUARDED_BEGIN();
    memcpy(&kl, obj, 8);
    GUARDED_END();
    if (!ptr_ok(kl)) return;
    uintptr_t nameref = 0, nsref = 0;
    GUARDED_BEGIN();
    memcpy(&nameref, reinterpret_cast<const uint8_t *>(kl) + 0x10, 8);
    memcpy(&nsref, reinterpret_cast<const uint8_t *>(kl) + 0x18, 8);
    GUARDED_END();
    char nm[80] = "?", ns[64] = "?";
    if (ptr_ok(reinterpret_cast<void *>(nameref))) {
        memcpy(nm, reinterpret_cast<void *>(nameref), 79);
        nm[79] = 0;
        for (int i = 0; i < 79; i++) { unsigned char c = static_cast<unsigned char>(nm[i]); if (c == 0) break; if (c < 32 || c > 126) { nm[i] = 0; break; } }
    }
    if (ptr_ok(reinterpret_cast<void *>(nsref))) {
        memcpy(ns, reinterpret_cast<void *>(nsref), 63);
        ns[63] = 0;
        for (int i = 0; i < 63; i++) { unsigned char c = static_cast<unsigned char>(ns[i]); if (c == 0) break; if (c < 32 || c > 126) { ns[i] = 0; break; } }
    }
    snprintf(out, cap, "%s.%s", ns, nm);
}

/* v3: mname — nama method dari MethodInfo (name@+8) */
static void mname_of(void *mi, char *out, size_t cap) {
    snprintf(out, cap, "?");
    if (!ptr_ok(mi)) return;
    uintptr_t nameref = 0;
    GUARDED_BEGIN();
    memcpy(&nameref, reinterpret_cast<const uint8_t *>(mi) + 8, 8);
    GUARDED_END();
    if (!ptr_ok(reinterpret_cast<void *>(nameref))) return;
    memcpy(out, reinterpret_cast<void *>(nameref), cap - 1);
    out[cap - 1] = 0;
    for (size_t i = 0; i < cap - 1; i++) { unsigned char c = static_cast<unsigned char>(out[i]); if (c == 0) break; if (c < 32 || c > 126) { out[i] = 0; break; } }
}

/* v3: klassof — class RUNTIME dari monster hidup + stats + damagedBehaviour + declaring class tiap method hook */
void feat_klassof(char *out, size_t cap) {
    if (!feat_resolve()) { snprintf(out, cap, "KO resolve=0"); return; }
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) { snprintf(out, cap, "KO no-list"); return; }
    void *mon = nullptr, *st = nullptr;
    int act = -1, dead = -1, hp = -1;
    for (int32_t i = 0; i < size && !mon; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        feat_mstat(m, st, act, dead, hp);
        if (hp > 0 && (act == 3 || act == 2)) { mon = m; break; }
    }
    if (!mon) { snprintf(out, cap, "KO no-monster"); return; }
    void *db = nullptr;
    if (g_m_char_getdb) {
        void *e = nullptr;
        GUARDED_BEGIN();
        db = inv(g_m_char_getdb, mon, nullptr, &e);
        GUARDED_END();
    }
    char nm1[128], nm2[128], nm3[128];
    kname(mon, nm1, sizeof nm1);
    kname(st, nm2, sizeof nm2);
    kname(db, nm3, sizeof nm3);
    /* declaring class tiap method hook */
    char dc1[128] = "?", dc2[128] = "?", dc3[128] = "?", mn1[64] = "?", mn2[64] = "?";
    void *mi = nullptr;
    if (ptr_ok(g_m_damage)) { mi = *(void **)g_m_damage; kname(mi, dc1, sizeof dc1); mname_of(g_m_damage, mn1, sizeof mn1); }
    if (ptr_ok(g_m_mdb_damage)) { mi = *(void **)g_m_mdb_damage; kname(mi, dc2, sizeof dc2); mname_of(g_m_mdb_damage, mn2, sizeof mn2); }
    if (ptr_ok(g_m_mdb_die)) { mi = *(void **)g_m_mdb_die; kname(mi, dc3, sizeof dc3); }
    snprintf(out, cap, "KO mon=%s | stats=%s | db=%s || hook0:%s.%s hook1:%s.%s hook2:%s",
             nm1, nm2, nm3, dc1, mn1, dc2, mn2, dc3);
    ELOGI("KLASSOF: %s", out);
}

/* ===== v22: HW WATCHPOINT via debug registers (fork child ptrace parent) =====
   REVISI v22c: child = ASYNC-SAFE (tanpa malloc/fopen/opendir — cegah fork-deadlock).
   Semua persiapan (fd log + scan tid) = dilakukan PARENT sebelum fork. */
static pid_t g_dr_child = 0;
static uintptr_t g_dr_addr = 0;
static int g_dr_logfd = -1;
static pid_t g_dr_tid = 0;

#define DR_BASE 848 /* offsetof(struct user, u_debugreg) pada x86_64 linux ABI */
#define DR_LOG_PATH "/storage/emulated/0/Android/data/com.kakaogames.gdts/files/wsmtrace.log"

/* struktur regs x86_64 (ABI kernel, fixed) */
struct wsm_regs64 {
    unsigned long long r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
    unsigned long long rax, rcx, rdx, rsi, rdi, orig_rax;
    unsigned long long rip, cs, eflags, rsp, ss, fs_base, gs_base, ds, es, fs, gs;
};

/* formatting manual (async-safe) */
static void ua_puts(int fd, const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (n) write(fd, s, n);
}
static void ua_hex(int fd, const char *tag, unsigned long long v) {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    if (!v) { buf[--i] = '0'; }
    while (v) {
        unsigned d = (unsigned)(v & 15);
        buf[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
    }
    ua_puts(fd, tag);
    write(fd, buf + i, 23 - i);
}
static void ua_dec(int fd, const char *tag, unsigned long long v) {
    char buf[24];
    int i = 24;
    buf[--i] = 0;
    if (!v) buf[--i] = '0';
    while (v) { buf[--i] = (char)('0' + v % 10); v /= 10; }
    ua_puts(fd, tag);
    write(fd, buf + i, 23 - i);
}

static void dr_child_main(void) {
    int lf = g_dr_logfd;
    if (lf < 0) _exit(9);
    ua_puts(lf, "DRWATCH child start tid=");
    ua_dec(lf, "", (unsigned long long)g_dr_tid);
    ua_puts(lf, "\n");
    pid_t tid = g_dr_tid ? g_dr_tid : getppid();
    if (ptrace(PTRACE_ATTACH, tid, 0, 0) != 0) {
        ua_puts(lf, "attach fail errno=");
        ua_dec(lf, "", (unsigned long long)errno);
        ua_puts(lf, "\n");
        _exit(8);
    }
    int st = 0;
    waitpid(tid, &st, __WALL);
    unsigned long dr0 = (unsigned long)g_dr_addr;
    unsigned long dr7 = 0xD0001UL; /* L0 + RW0=write(01) + LEN0=4byte(11b) — WAJIB align 4 (0x5C ok) */
    ptrace(PTRACE_POKEUSER, tid, (void *)(DR_BASE + 0 * 8), (void *)dr0);
    ptrace(PTRACE_POKEUSER, tid, (void *)(DR_BASE + 7 * 8), (void *)dr7);
    ua_puts(lf, "DRWATCH armed dr0=");
    ua_hex(lf, "", (unsigned long long)dr0);
    ua_puts(lf, " dr7=");
    ua_hex(lf, "", (unsigned long long)dr7);
    ua_puts(lf, "\n");
    ptrace(PTRACE_CONT, tid, 0, 0);
    int hits = 0;
    for (;;) {
        int s2 = 0;
        pid_t w = waitpid(-1, &s2, __WALL);
        if (w <= 0) break;
        if (WIFSTOPPED(s2)) {
            int sig = WSTOPSIG(s2);
            if (sig == SIGTRAP) {
                struct wsm_regs64 rg;
                memset(&rg, 0, sizeof rg);
                ptrace(PTRACE_GETREGS, w, 0, &rg);
                hits++;
                ua_puts(lf, "HIT#");
                ua_dec(lf, "", (unsigned long long)hits);
                ua_puts(lf, " tid=");
                ua_dec(lf, "", (unsigned long long)w);
                ua_hex(lf, " rip=", rg.rip);
                ua_hex(lf, " rax=", rg.rax);
                ua_hex(lf, " rbx=", rg.rbx);
                ua_hex(lf, " rcx=", rg.rcx);
                ua_hex(lf, " rdx=", rg.rdx);
                ua_hex(lf, " rsi=", rg.rsi);
                ua_puts(lf, "\n");
                ptrace(PTRACE_POKEUSER, w, (void *)(DR_BASE + 7 * 8), (void *)0UL);
                ptrace(PTRACE_SINGLESTEP, w, 0, 0);
                int s3 = 0;
                waitpid(w, &s3, __WALL);
                ptrace(PTRACE_POKEUSER, w, (void *)(DR_BASE + 7 * 8), (void *)dr7);
                ptrace(PTRACE_CONT, w, 0, 0);
                if (hits >= 40) {
                    ptrace(PTRACE_DETACH, w, 0, 0);
                    ua_puts(lf, "DRWATCH done\n");
                    _exit(0);
                }
            } else if (sig == SIGSTOP || sig == SIGCONT) {
                ptrace(PTRACE_CONT, w, 0, 0);
            } else {
                ptrace(PTRACE_CONT, w, 0, sig);
            }
        } else if (WIFEXITED(s2) || WIFSIGNALED(s2)) {
            ua_puts(lf, "DRWATCH tracee end\n");
            break;
        }
    }
    _exit(0);
}

/* parent-side: buka log + scan tid UnityMain (di parent = aman malloc) */
static void dr_prepare(const char *what) {
    g_dr_logfd = open(DR_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    g_dr_tid = 0;
    pid_t self = getpid();
    char tdir[128];
    snprintf(tdir, sizeof tdir, "/proc/%d/task", self);
    DIR *d = opendir(tdir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char cp[192];
            snprintf(cp, sizeof cp, "/proc/%d/task/%s/comm", self, e->d_name);
            FILE *cf = fopen(cp, "r");
            if (cf) {
                char nm[64] = {};
                if (fgets(nm, sizeof nm, cf) && strncmp(nm, "UnityMain", 9) == 0)
                    g_dr_tid = atoi(e->d_name);
                fclose(cf);
                if (g_dr_tid) break;
            }
        }
        closedir(d);
    }
    (void)what;
}

static void dr_start(char *out, size_t cap, const char *suffix) {
    if (g_dr_child > 0) { kill(g_dr_child, SIGKILL); g_dr_child = 0; }
    dr_prepare(suffix);
    if (g_dr_logfd >= 0)
        ua_puts(g_dr_logfd, "DRWATCH prepare ok\n");
    pid_t c = fork();
    if (c == 0) { dr_child_main(); _exit(0); }
    if (c < 0) { snprintf(out, cap, "DR fork fail %d", errno); return; }
    g_dr_child = c;
    snprintf(out, cap, "DR armed child=%d tid=%d target=0x%llx%s", (int)c, (int)g_dr_tid,
             (unsigned long long)g_dr_addr, suffix ? suffix : "");
    ELOGI("DRON: %s", out);
}

void feat_dron(char *out, size_t cap) {
    if (!feat_resolve()) { snprintf(out, cap, "DR resolve=0"); return; }
    prctl(PR_SET_PTRACER, (unsigned long)-1, 0, 0, 0);
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) { snprintf(out, cap, "DR no-list"); return; }
    void *st = nullptr;
    int act = -1, dead = -1, hp = -1;
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *sti = nullptr;
        feat_mstat(m, sti, act, dead, hp);
        if (sti && hp > 0) {
            if (act == 3) { st = sti; break; } /* sedang bertarung = prioritas */
            if (act == 2) st = sti;            /* fallback: yang terakhir */
        }
    }
    if (!st) { snprintf(out, cap, "DR no-live-stats"); return; }
    g_dr_addr = (uintptr_t)st + 0x5C;
    dr_start(out, cap, nullptr);
}

void feat_dronew(char *out, size_t cap, const char *arg) {
    /* DRONEW: target = char 'i' (hero/instack char) atau default monster;
       arg optional: "hero" = pakai pemain pertama dari GetAllPlayers */
    if (!feat_resolve()) { snprintf(out, cap, "DR resolve=0"); return; }
    prctl(PR_SET_PTRACER, (unsigned long)-1, 0, 0, 0);
    void *st = nullptr;
    if (arg && strstr(arg, "hero")) {
        fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
        feat_mopen(inv, lst, size, items);
        if (inv && size > 0 && g_m_cm_players) {
            void *plist = nullptr; void *e = nullptr;
            GUARDED_BEGIN();
            plist = inv(g_m_cm_players, nullptr, nullptr, &e);
            GUARDED_END();
            if (ptr_ok(plist)) {
                int32_t n = 0; void **it = nullptr;
                memcpy(&n, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                memcpy(&it, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                if (ptr_ok(it) && n > 0) {
                    void *p0 = nullptr;
                    memcpy(&p0, reinterpret_cast<const uint8_t *>(it) + 0x20, 8);
                    if (ptr_ok(p0) && g_m_char_getstats) {
                        GUARDED_BEGIN();
                        st = inv(g_m_char_getstats, p0, nullptr, &e);
                        GUARDED_END();
                    }
                }
            }
        }
        if (!st) { snprintf(out, cap, "DRNEW hero-stats fail"); return; }
    } else {
        fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
        feat_mopen(inv, lst, size, items);
        if (!inv || size <= 0) { snprintf(out, cap, "DR no-list"); return; }
        int act = -1, dead = -1, hp = -1;
        for (int32_t i = 0; i < size; i++) {
            void *m = feat_mobj(items, i);
            if (!m) continue;
            void *sti = nullptr;
            feat_mstat(m, sti, act, dead, hp);
            if (sti && hp > 0) {
            if (act == 3) { st = sti; break; } /* sedang bertarung = prioritas */
            if (act == 2) st = sti;            /* fallback: yang terakhir */
        }
        }
        if (!st) { snprintf(out, cap, "DR no-live-stats"); return; }
    }
    g_dr_addr = (uintptr_t)st + 0x5C;
    dr_start(out, cap, (arg && strstr(arg, "hero")) ? " (hero)" : "");
}

void feat_drread(char *out, size_t cap) {
    FILE *f = fopen("/storage/emulated/0/Android/data/com.kakaogames.gdts/files/wsmtrace.log", "r");
    if (!f) { snprintf(out, cap, "DRREAD no-log"); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    long start = sz > 1000 ? sz - 1000 : 0;
    fseek(f, start, SEEK_SET);
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = 0;
    fclose(f);
    for (size_t i = 0; i < n; i++)
        if (out[i] == '\n') out[i] = '|';
    ELOGI("DRREAD: %s", out);
}

void feat_droff(char *out, size_t cap) {
    if (g_dr_child > 0) { kill(g_dr_child, SIGKILL); g_dr_child = 0; }
    snprintf(out, cap, "DR off");
}

/* v3: guardhp — pasang watchpoint SIGSEGV di field HP (st+0x5C, ObscuredInt 8B) */
void feat_guardhp(char *out, size_t cap) {
    if (!hook_ensure_payload()) { snprintf(out, cap, "GUARD NOHANDLE"); return; }
    if (!feat_resolve()) { snprintf(out, cap, "GUARD resolve=0"); return; }
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) { snprintf(out, cap, "GUARD no-list"); return; }
    void *st = nullptr;
    int act = -1, dead = -1, hp = -1;
    for (int32_t i = 0; i < size; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *sti = nullptr;
        feat_mstat(m, sti, act, dead, hp);
        if (sti && hp > 0) {
            if (act == 3) { st = sti; break; } /* sedang bertarung = prioritas */
            if (act == 2) st = sti;            /* fallback: yang terakhir */
        }
    }
    if (!st) { snprintf(out, cap, "GUARD no-live-stats"); return; }
    unsigned long long addr = (unsigned long long)(uintptr_t)st + 0x5C;
    g_h64_bus[1] = 0;
    g_h64_bus[80] = addr;
    g_h64_bus[0] = 12;
    for (int i = 0; i < 400 && g_h64_bus[1] != 5; i++) usleep(5000);
    snprintf(out, cap, "GUARD armed st=0x%llx hp_field=0x%llx", (unsigned long long)(uintptr_t)st, addr);
    ELOGI("GUARDHP: %s", out);
}

void feat_guardread(char *out, size_t cap) {
    if (!g_h64_handle || !g_h64_bus[10]) { snprintf(out, cap, "GUARDREAD no-payload"); return; }
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 14;
    for (int i = 0; i < 200 && g_h64_bus[1] != 8; i++) usleep(5000);
    snprintf(out, cap, "GUARDREAD hits=%llu pc=0x%llx lr=0x%llx x0=0x%llx addr=0x%llx",
             (unsigned long long)g_h64_bus[81], (unsigned long long)g_h64_bus[82],
             (unsigned long long)g_h64_bus[83], (unsigned long long)g_h64_bus[84],
             (unsigned long long)g_h64_bus[85]);
    ELOGI("GUARDREAD: %s", out);
}

void feat_guardoff(char *out, size_t cap) {
    if (!g_h64_handle || !g_h64_bus[10]) { snprintf(out, cap, "GUARDOFF no-payload"); return; }
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 13;
    for (int i = 0; i < 200 && g_h64_bus[1] != 5; i++) usleep(5000);
    snprintf(out, cap, "GUARDOFF done");
}

/* v3: peek — baca N kata 32-bit dari alamat (untuk verifikasi patch) */
void feat_peek(char *out, size_t cap, const char *arg) {
    if (!arg) { snprintf(out, cap, "PEEK no-arg"); return; }
    unsigned long long a = strtoull(arg, nullptr, 16);
    size_t used = 0;
    used += snprintf(out + used, cap - used, "PEEK 0x%llx:", a);
    for (int i = 0; i < 4; i++) {
        uint32_t w = 0;
        GUARDED_BEGIN();
        w = *(uint32_t *)(uintptr_t)(a + i * 4);
        GUARDED_END();
        used += snprintf(out + used, cap - used, " %08x", w);
    }
    ELOGI("PEEK: %s", out);
}

/* v3: hookverify — patch isdead (mov w0,#1;ret) + panggil via invoke + bandingkan */
void feat_hookverify(char *out, size_t cap) {
    if (!hook_ensure_payload()) { snprintf(out, cap, "HV NOHANDLE"); return; }
    if (!feat_resolve()) { snprintf(out, cap, "HV resolve=0"); return; }
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) { snprintf(out, cap, "HV no-list"); return; }
    void *mon = nullptr, *st = nullptr;
    int act = -1, dead0 = -1, hp = -1;
    for (int32_t i = 0; i < size && !mon; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        feat_mstat(m, st, act, dead0, hp);
        if ((act == 3 || act == 2) && dead0 == 0 && hp > 0) { mon = m; break; }
    }
    if (!mon) { snprintf(out, cap, "HV no-live-monster"); return; }
    uint64_t code = 0;
    GUARDED_BEGIN();
    if (ptr_ok(g_m_stats_isdead)) code = *(uint64_t *)((char *)g_m_stats_isdead + 0x10);
    GUARDED_END();
    if (!code) { snprintf(out, cap, "HV no-code"); return; }
    /* rawpatch: mov w0,#1 ; ret */
    g_h64_bus[1] = 0;
    g_h64_bus[59] = 0;
    g_h64_bus[60] = code;
    g_h64_bus[61] = 0x52800020u;
    g_h64_bus[62] = 0xD65F03C0u;
    g_h64_bus[0] = 11;
    for (int i = 0; i < 400 && g_h64_bus[1] != 5; i++) usleep(5000);
    int np = (int)g_h64_bus[69];
    /* panggil post */
    int act2 = -1, dead1 = -1, hp2 = -1;
    void *st2 = nullptr;
    feat_mstat(mon, st2, act2, dead1, hp2);
    /* restore semua slot */
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 7;
    for (int i = 0; i < 400 && g_h64_bus[1] != 6; i++) usleep(5000);
    int act3 = -1, dead2 = -1, hp3 = -1;
    void *st3 = nullptr;
    feat_mstat(mon, st3, act3, dead2, hp3);
    snprintf(out, cap, "HV isdead pre=%d post=%d afterrestore=%d np=%d code=0x%llx (post!=pre => PATCH SEEN!)",
             dead0, dead1, dead2, np, (unsigned long long)code);
    ELOGI("HOOKVERIFY: %s", out);
}

/* v3: hookread — lapor counts live tanpa restore */
void feat_hookabs(char *out, size_t cap, const char *arg) {
    /* v23: hook di ALAMAT ABSOLUT (untuk target hasil watchpoint yang resolver-nya nggak ketemu).
       Format: "hookabs <slot> <hexaddr>" */
    if (!hook_ensure_payload()) { snprintf(out, cap, "HAB NOHANDLE"); return; }
    int slot = 0;
    unsigned long long addr = 0;
    if (arg) {
        while (*arg == ' ') arg++;
        slot = atoi(arg);
        while (*arg && *arg != ' ') arg++;
        while (*arg == ' ') arg++;
        addr = strtoull(arg, nullptr, 16);
    }
    if (!addr) { snprintf(out, cap, "HAB bad-args"); return; }
    if (reloc_unsafe(addr)) {
        snprintf(out, cap, "HAB REFUSE: prologue @0x%llx relatif-PC (unsafe utk relokasi)", addr);
        ELOGI("HOOKABS: %s", out);
        return;
    }
    g_h64_bus[1] = 0;
    g_h64_bus[59] = (uint64_t)slot;
    g_h64_bus[60] = addr;
    g_h64_bus[0] = 6;
    for (int i = 0; i < 400 && g_h64_bus[1] != 5; i++) usleep(5000);
    snprintf(out, cap, "HAB slot=%d addr=0x%llx np=%lld rc=%lld", slot, addr,
             (long long)g_h64_bus[69], (long long)(int64_t)g_h64_bus[68]);
    ELOGI("HOOKABS: %s", out);
}

/* ==================== v30: FITUR PRODUKSI ==================== */
static volatile uint64_t g_bus_seq = 0;
/* v31: handshake token anti-stale — tunggu ack KHUSUS command ini (bus[9]), bukan bus[1] global */
static uint64_t bus_arm(void) {
    if (g_bus_unhealthy || !g_h64_handle) return 0;
    if (__atomic_load_n(&g_h64_bus[0], __ATOMIC_ACQUIRE) != 0) { g_bus_unhealthy = true; return 0; }
    uint64_t t = ++g_bus_seq;
    g_h64_bus[1] = 0; g_h64_bus[68] = (uint64_t)(int64_t)-1; g_h64_bus[69] = 0;
    g_h64_bus[7] = t;
    return t;
}
static bool bus_done(uint64_t t) {
    if (!t) return false;
    const uint64_t deadline = now_ms() + 4000;
    while (now_ms() < deadline) {
        if (__atomic_load_n(&g_h64_bus[9], __ATOMIC_ACQUIRE) == t) return true;
        usleep(5000);
    }
    g_bus_unhealthy = true; // Quarantine a timed-out channel; never overwrite an in-flight request.
    return false;
}
static volatile int g_speed_on = 0, g_nocd_on = 0, g_loot_on = 0, g_stunall_on = 0;
static bool g_critdmg_on = false;
static float g_critdmg_value = 2.0f;
static volatile unsigned long long g_speed_hero = 0;
static volatile int g_loot_last_n = -1, g_loot_last_done = 0;
static volatile uint64_t g_loot_stage = 0, g_loot_dm = 0, g_loot_list = 0;
static volatile int g_stun_last_n = -1, g_stun_last_done = 0;
static void *g_cls_cext = nullptr, *g_m_cb_cantsuper = nullptr, *g_m_cext_cantrig = nullptr;
static void *g_cls_dm = nullptr, *g_m_stage_getdm = nullptr;
static void *g_cls_di = nullptr, *g_m_di_findtarget = nullptr, *g_m_di_setauto = nullptr;

static bool feat_resolve2(void) {
    if (g_m_cb_cantsuper && g_m_cext_cantrig && g_m_stage_getdm && g_m_di_findtarget && g_m_di_setauto) return true;
    if (!g_img) return false;
    fn_cfn_t2 cfn = feat_cfn();
    fn_cgm_t2 cgm = feat_cgm();
    if (!cfn || !cgm) return false;
    void *img = reinterpret_cast<void *>(g_img);
    GUARDED_BEGIN();
    if (!g_cls_cb) g_cls_cb = cfn(img, "Oak", "CharacterBehaviour");
    if (!g_cls_cext) g_cls_cext = cfn(img, "Oak", "ICharacterBehaviourExtensions");
    if (!g_cls_dm) g_cls_dm = cfn(img, "Oak", "DropManager");
    if (!g_cls_di) g_cls_di = cfn(img, "Oak", "DropItem");
    GUARDED_END();
    if (!g_cls_di) return false;
    GUARDED_BEGIN();
    if (g_cls_cb && !g_m_cb_cantsuper) g_m_cb_cantsuper = cgm(g_cls_cb, "CanTriggerSuperBattleAction", 1);
    if (g_cls_cext && !g_m_cext_cantrig) g_m_cext_cantrig = cgm(g_cls_cext, "CanTriggerBattleAction", -1);
    if (!g_m_stage_getdm && g_cls_stage) g_m_stage_getdm = cgm(g_cls_stage, "get_DropManager", 0);
    if (!g_m_di_findtarget) g_m_di_findtarget = cgm(g_cls_di, "FindAndSetConsumeTarget", 0);
    if (!g_m_di_setauto) g_m_di_setauto = cgm(g_cls_di, "set_AutoConsumeOnDropped", 1);
    GUARDED_END();
    return (g_m_stage_getdm != nullptr) && (g_m_di_findtarget != nullptr);
}

static unsigned long long engine_find_hero_stats(void) {
    if (!feat_resolve()) return 0;
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    (void)lst; (void)size; (void)items;
    unsigned long long hs = 0;
    if (inv && g_m_stage_inst && g_m_stage_getcm && g_m_cm_players && g_m_char_getstats) {
        void *e = nullptr;
        void *stage = nullptr, *cmgr = nullptr, *plist = nullptr;
        GUARDED_BEGIN(); stage = inv(g_m_stage_inst, nullptr, nullptr, &e); GUARDED_END();
        if (ptr_ok(stage)) { GUARDED_BEGIN(); cmgr = inv(g_m_stage_getcm, stage, nullptr, &e); GUARDED_END(); }
        if (ptr_ok(cmgr)) { GUARDED_BEGIN(); plist = inv(g_m_cm_players, cmgr, nullptr, &e); GUARDED_END(); }
        if (ptr_ok(plist)) {
            int32_t n = 0; void **it = nullptr;
            memcpy(&n, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
            memcpy(&it, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
            uint64_t array_length = 0;
            if (ptr_ok(it)) memcpy(&array_length, reinterpret_cast<const uint8_t *>(it) + 0x18, 8);
            if (ptr_ok(it) && n > 0 && n <= 64 && array_length >= static_cast<uint64_t>(n) && array_length <= 1000000) {
                void *p0 = nullptr;
                memcpy(&p0, reinterpret_cast<const uint8_t *>(it) + 0x20, 8);
                if (ptr_ok(p0)) {
                    GUARDED_BEGIN();
                    hs = (unsigned long long)(uintptr_t)inv(g_m_char_getstats, p0, nullptr, &e);
                    GUARDED_END();
                }
            }
        }
    }
    return hs;
}

/* pasang blok MKBLK via bus (style 1=scale, 2=ret-true) */
static int v30_mkblk(int slot, unsigned long long code, int style, uint32_t off, uint32_t fbits,
                     unsigned long long hs) {
    if (!g_target_base || !code) return -98;
    uint64_t tok = bus_arm();
    if (!tok) return -99;
    g_h64_bus[59] = (uint64_t)slot;
    g_h64_bus[60] = code;
    g_h64_bus[61] = (uint64_t)style;
    g_h64_bus[62] = (uint64_t)off;
    g_h64_bus[63] = (uint64_t)fbits;
    g_h64_bus[88] = (uint64_t)hs;
    uint64_t pre_ack = g_h64_bus[9];
    __atomic_store_n(&g_h64_bus[0], 18ULL, __ATOMIC_RELEASE);
    bool ok = bus_done(tok);
    int rc2 = (int)(int64_t)g_h64_bus[68];
    int np2 = (int)g_h64_bus[69];
    ELOGI("MKBLK-E: slot=%d tok=%llu pre_ack=%llu ok=%d rc=%d np=%d", slot,
          (unsigned long long)tok, (unsigned long long)pre_ack, ok ? 1 : 0, rc2, np2);
    if (!ok) { ELOGI("MKBLK: TIMEOUT slot=%d addr=0x%llx", slot, (unsigned long long)code); return -99; }
    if (rc2 == -5 || rc2 == -11) g_bus_unhealthy = true; // Failed rollback/rebind leaves patch ownership uncertain.
    int rc = rc2;
    int np = np2;
    return (rc < 0) ? rc : np;
}

static int v30_restore1(int slot) {
    if (!g_h64_handle) return 0;
    uint64_t tok = bus_arm();
    if (!tok) return -99;
    g_h64_bus[59] = (uint64_t)slot;
    __atomic_store_n(&g_h64_bus[0], 17ULL, __ATOMIC_RELEASE);
    if (!bus_done(tok)) return -99;
    int rc = (int)(int64_t)g_h64_bus[68];
    if (rc < 0) g_bus_unhealthy = true; // Never claim OFF when restoration failed.
    return rc < 0 ? rc : (int)g_h64_bus[69];
}

static bool getter_prologue_supported(uint64_t code) {
    if (!code) return false;
    uint32_t words[4]{};
    GUARDED_BEGIN(); memcpy(words, reinterpret_cast<const void *>(code), sizeof words); GUARDED_END();
    for (uint32_t word : words) if (!wsm_arm64_copy_supported(word)) return false;
    return true;
}
void feat_speed(char *out, size_t cap, const char *arg) {
    const char *a = arg ? arg : "";
    while (*a == ' ') a++;
    if (strncmp(a, "off", 3) == 0 || strncmp(a, "0", 1) == 0) {
        int n1 = v30_restore1(20), n2 = v30_restore1(21), n3 = v30_restore1(22);
        bool ok = n1 >= 0 && n2 >= 0 && n3 >= 0;
        if (ok) g_speed_on = 0;
        snprintf(out, cap, "%s SPEED OFF (nr=%d/%d/%d)", ok ? "OK" : "ERR", n1, n2, n3);
        ELOGI("SPEED: %s", out);
        return;
    }
    if (!hook_ensure_payload()) { snprintf(out, cap, "SPEED NOHANDLE"); return; }
    if (!feat_resolve()) { snprintf(out, cap, "SPEED resolve=0"); return; }
    fn_cgm_t2 cgm = feat_cgm();
    if (!cgm) { snprintf(out, cap, "SPEED no-cgm"); return; }
    if (g_speed_on) {
        for (int slot = 20; slot <= 22; ++slot) if (v30_restore1(slot) < 0) { snprintf(out, cap, "ERR SPEED restore failed"); return; }
        g_speed_on = 0;
    }
    unsigned long long hs = engine_find_hero_stats();
    if (!hs) { snprintf(out, cap, "ERR SPEED hero unavailable"); return; }
    float multiplier = strtof(a, nullptr);
    if (!wsm::finite_range(multiplier, 1, 5)) multiplier = 2;
    uint32_t multiplier_bits; memcpy(&multiplier_bits, &multiplier, 4);
    size_t used = snprintf(out, cap, "SPEED ON hs=0x%llx", hs);
    int okc = 0;
    struct { int slot; const char *getter; } jobs[3] = {
        {20, "get_WalkSpeed"}, {21, "get_DashSpeed"}, {22, "get_SoftDashSpeed"}
    };
    for (int j = 0; j < 3; j++) {
        void *mi = nullptr;
        mi = strict_binding(g_cls_stats, {jobs[j].getter, "System.Single", {nullptr, nullptr, nullptr}, 0, false});
        uint64_t code = 0;
        GUARDED_BEGIN(); if (ptr_ok(mi)) code = *(uint64_t *)mi; GUARDED_END();
        if (!code) { used += snprintf(out + used, cap - used, " %s=NO-MI", jobs[j].getter); continue; }
        uint32_t w0 = 0, w1 = 0;
        GUARDED_BEGIN();
        w0 = *(volatile uint32_t *)(uintptr_t)code;
        w1 = *(volatile uint32_t *)(uintptr_t)(code + 4);
        GUARDED_END();
        const bool leaf=(w0 & 0xFFC003FFu)==0xBD400000u && w1==0xD65F03C0u;
        if(!leaf && !getter_prologue_supported(code)) {
            used+=snprintf(out+used,cap-used," %s=unsupported-PC-relative-prologue",jobs[j].getter);continue;
        }
        uint32_t foff=leaf?(uint32_t)(((w0>>10)&0xFFFu)<<2):0;
        int np=v30_mkblk(jobs[j].slot,code,leaf?1:3,foff,multiplier_bits,hs);
        if (np > 0) okc++;
        used += snprintf(out + used, cap - used, " %s@0x%llx/off%x np=%d", jobs[j].getter,
                         (unsigned long long)code, foff, np);
    }
    g_speed_on = (okc == 3);
    if (!g_speed_on) { for (int slot = 20; slot <= 22; ++slot) (void)v30_restore1(slot); }
    g_speed_value = multiplier;
    g_speed_hero = hs;
    ELOGI("SPEED: %s", out);
}

void feat_critdmg(char *out, size_t cap, float multiplier) {
    if (multiplier == 0) {
        const int rc = v30_restore1(26);
        if (rc >= 0) g_critdmg_on = false;
        snprintf(out, cap, "%s CRITDMG OFF restore=%d", rc >= 0 ? "OK" : "ERR", rc);
        return;
    }
    if (!wsm::finite_range(multiplier, 1, 5) || !feat_resolve() || !hook_ensure_payload()) {
        snprintf(out, cap, "ERR CRITDMG range, metadata or payload unavailable"); return;
    }
    const uint64_t hero = engine_find_hero_stats();
    void *mi = strict_binding(g_cls_stats,
        {"get_CriticalMultiplierScale", "System.Single", {nullptr, nullptr, nullptr}, 0, false});
    const uint64_t code = method_code(mi);
    if (!hero || !code) { snprintf(out, cap, "ERR CRITDMG hero/getter unavailable"); return; }
    if (g_critdmg_on) {
        if (v30_restore1(26) < 0) { snprintf(out, cap, "ERR CRITDMG restore failed"); return; }
        g_critdmg_on = false;
    }
    uint32_t w0 = 0, w1 = 0;
    GUARDED_BEGIN(); memcpy(&w0, reinterpret_cast<void *>(code), 4); memcpy(&w1, reinterpret_cast<void *>(code + 4), 4); GUARDED_END();
    const bool leaf = (w0 & 0xFFC003FFu) == 0xBD400000u && w1 == 0xD65F03C0u;
    if (!leaf && !getter_prologue_supported(code)) { snprintf(out, cap, "ERR CRITDMG unsupported control-flow/literal prologue"); return; }
    uint32_t bits; memcpy(&bits, &multiplier, 4);
    const uint32_t offset = leaf ? ((w0 >> 10) & 0xFFFu) << 2 : 0;
    const int patches = v30_mkblk(26, code, leaf ? 1 : 3, offset, bits, hero);
    g_critdmg_on = patches > 0;
    if (g_critdmg_on) g_critdmg_value = multiplier;
    else (void)v30_restore1(26);
    snprintf(out, cap, "%s CRITDMG hero-only scale=%.2f patches=%d", g_critdmg_on ? "OK" : "ERR", (double)multiplier, patches);
}

void feat_nocd(char *out, size_t cap, const char *arg) {
    const char *a = arg ? arg : "";
    while (*a == ' ') a++;
    if (strncmp(a, "off", 3) == 0 || strncmp(a, "0", 1) == 0) {
        int n1 = v30_restore1(19), n2 = v30_restore1(23), n3 = v30_restore1(25);
        bool ok = n1 >= 0 && n2 >= 0 && n3 >= 0;
        if (ok) g_nocd_on = 0;
        snprintf(out, cap, "%s NOCD OFF (nr=%d/%d/%d)", ok ? "OK" : "ERR", n1, n2, n3);
        ELOGI("NOCD: %s", out);
        return;
    }
    if (!hook_ensure_payload()) { snprintf(out, cap, "NOCD NOHANDLE"); return; }
    // Resolve each target through IL2CPP metadata. File offsets differ from RVAs.
    // CooltimeLeft returns 0.0f; trigger predicates return true.
    (void)feat_resolve2();
    fn_cfn_t2 cfn=feat_cfn();fn_cgm_t2 cgm=feat_cgm(); void *myth=nullptr,*cool=nullptr;
    GUARDED_BEGIN();
    if(cfn&&cgm) { myth=cfn((void *)(uintptr_t)g_img,"Oak","MythBattleAction"); if(myth)cool=cgm(myth,"Oak.IMythBattleAction.get_CooltimeLeft",0); }
    GUARDED_END();
    uint64_t super_code=method_code(g_m_cb_cantsuper),ext_code=method_code(g_m_cext_cantrig),cool_code=method_code(cool);
    if(!super_code||!ext_code||!cool_code){snprintf(out,cap,"ERR NOCD metadata targets unavailable super=%llx ext=%llx cool=%llx",(unsigned long long)super_code,(unsigned long long)ext_code,(unsigned long long)cool_code);return;}
    size_t used = snprintf(out, cap, "NOCD ON");
    int okc = 0;
    struct { int slot; unsigned long long code; int style; const char *name; } jobs[3] = {
        {19, super_code, 2, "CanTriggerSuper"},
        {23, ext_code, 2, "CanTriggerExt"},
        {25, cool_code, 6, "CooltimeLeft=0"},
    };
    for (int j = 0; j < 3; j++) {
        if (j > 0) usleep(150 * 1000); /* v31b: pacing antar-command (cegah race bus back-to-back) */
        int np = v30_mkblk(jobs[j].slot, jobs[j].code, jobs[j].style, 0, 0, 0);
        if (np > 0) okc++;
        used += snprintf(out + used, cap - used, " %s@0x%llx np=%d", jobs[j].name,
                         (unsigned long long)jobs[j].code, np);
    }
    g_nocd_on = (okc == 3);
    if (!g_nocd_on) { (void)v30_restore1(19); (void)v30_restore1(23); (void)v30_restore1(25); }
    ELOGI("NOCD: %s", out);
}

static void loot_beat(void) {
    if (!feat_resolve() || !feat_resolve2()) return;
    fn_inv_t2 inv = feat_inv();
    if (!inv || !g_m_stage_inst || !g_m_stage_getdm) return;
    void *exc = nullptr;
    void *stage = nullptr, *dm = nullptr, *list = nullptr;
    GUARDED_BEGIN(); stage = inv(g_m_stage_inst, nullptr, nullptr, &exc); GUARDED_END();
    g_loot_stage = reinterpret_cast<uint64_t>(stage);
    if (!ptr_ok(stage)) { g_loot_last_n = 0; g_loot_last_done = 0; return; }
    GUARDED_BEGIN(); dm = inv(g_m_stage_getdm, stage, nullptr, &exc); GUARDED_END();
    g_loot_dm = reinterpret_cast<uint64_t>(dm);
    if (!ptr_ok(dm)) { g_loot_last_n = 0; g_loot_last_done = 0; return; }
    memcpy(&list, reinterpret_cast<const uint8_t *>(dm) + 0x28, 8); /* droppedItemList */
    g_loot_list = reinterpret_cast<uint64_t>(list);
    if (!ptr_ok(list)) { g_loot_last_n = 0; g_loot_last_done = 0; return; }
    int32_t n = 0; void **it = nullptr;
    memcpy(&n, reinterpret_cast<const uint8_t *>(list) + 0x18, 4);
    memcpy(&it, reinterpret_cast<const uint8_t *>(list) + 0x10, 8);
    if (n < 0 || n > 128 || !ptr_ok(it)) { g_loot_last_n = n; g_loot_last_done = 0; return; }
    int done = 0;
    for (int32_t i = 0; i < n && i < 64; i++) {
        void *item = nullptr;
        memcpy(&item, reinterpret_cast<const uint8_t *>(it) + 0x20 + (uint64_t)i * 8, 8);
        if (!ptr_ok(item)) continue;
        GUARDED_BEGIN();
        (void) inv(g_m_di_findtarget, item, nullptr, &exc);
        GUARDED_END();
        if (g_m_di_setauto) {
            bool btrue = true;
            void *a1[1] = {&btrue};
            GUARDED_BEGIN();
            (void) inv(g_m_di_setauto, item, a1, &exc);
            GUARDED_END();
        }
        done++;
    }
    g_loot_last_n = n;
    g_loot_last_done = done;
}

void feat_loot(char *out, size_t cap, const char *arg) {
    const char *a = arg ? arg : "";
    while (*a == ' ') a++;
    if (strncmp(a, "off", 3) == 0 || strncmp(a, "0", 1) == 0) {
        g_loot_on = 0;
        snprintf(out, cap, "LOOT OFF");
        ELOGI("LOOT: %s", out);
        return;
    }
    if (!feat_resolve() || !feat_resolve2()) { snprintf(out, cap, "LOOT resolve=0"); return; }
    if (strncmp(a, "probe", 5) == 0) {
        loot_beat();
        snprintf(out, cap, "LOOT PROBE items=%d done=%d st=0x%llx dm=0x%llx li=0x%llx",
                 g_loot_last_n, g_loot_last_done,
                 (unsigned long long)g_loot_stage, (unsigned long long)g_loot_dm,
                 (unsigned long long)g_loot_list);
        ELOGI("LOOT: %s", out);
        return;
    }
    if (!g_m_di_findtarget || !g_m_di_setauto) { snprintf(out, cap, "ERR LOOT methods unavailable"); return; }
    loot_beat();
    if (!g_loot_stage || !g_loot_dm || !g_loot_list || g_loot_last_n < 0 || g_loot_last_n > 128) { snprintf(out, cap, "ERR LOOT scene unavailable"); return; }
    g_loot_on = 1;
    snprintf(out, cap, "LOOT ON items=%d done=%d st=0x%llx dm=0x%llx li=0x%llx",
             g_loot_last_n, g_loot_last_done,
             (unsigned long long)g_loot_stage, (unsigned long long)g_loot_dm,
             (unsigned long long)g_loot_list);
    ELOGI("LOOT: %s", out);
}

static void stun_beat(void) {
    if (!feat_resolve()) return;
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) { g_stun_last_n = 0; g_stun_last_done = 0; return; }
    if (!g_m_scmd_create || !g_m_scmd_exec) { g_stun_last_n = size; g_stun_last_done = -1; return; }
    int done = 0;
    for (int32_t i = 0; i < size && i < 96; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if (dead != 0 || !ptr_ok(st)) continue;
        float p[3];
        feat_getpos(m, p);
        if (fabsf(p[0]) > 900.0f || fabsf(p[2]) > 900.0f) continue;
        void *exc = nullptr;
        float sdur = 3.0f;
        bool ssuper = true;
        void *a3[3] = {&m, &sdur, &ssuper};
        void *cmd = nullptr;
        GUARDED_BEGIN();
        cmd = inv(g_m_scmd_create, nullptr, a3, &exc);
        GUARDED_END();
        if (!ptr_ok(cmd)) continue;
        int ct = 0;
        void *a1[1] = {&ct};
        GUARDED_BEGIN();
        (void) inv(g_m_scmd_exec, cmd, a1, &exc);
        GUARDED_END();
        done++;
    }
    g_stun_last_n = (int)size;
    g_stun_last_done = done;
}

void feat_stunall(char *out, size_t cap, const char *arg) {
    const char *a = arg ? arg : "";
    while (*a == ' ') a++;
    if (strncmp(a, "off", 3) == 0 || strncmp(a, "0", 1) == 0) {
        int nr = 0;
        if (hook_ensure_payload()) nr = v30_restore1(24);
        if (nr >= 0) g_stunall_on = 0;
        snprintf(out, cap, "%s STUNALL OFF (nr=%d)", nr >= 0 ? "OK" : "ERR", nr);
        ELOGI("STUNALL: %s", out);
        return;
    }
    if (!feat_resolve()) { snprintf(out, cap, "STUNALL resolve=0"); return; }
    if (strncmp(a, "fire", 4) == 0) {
        stun_beat();
        snprintf(out, cap, "STUNALL FIRE mon=%d done=%d", g_stun_last_n, g_stun_last_done);
        ELOGI("STUNALL: %s", out);
        return;
    }
    /* v31: FREEZE = hook MonsterBattleAIState.PickNTriggerBattleAction -> return false.
       Musuh tidak pernah memilih/memicu action = diam total (bukan sekadar gemetar). */
    int npf = -1;
    fn_cfn_t2 cfn=feat_cfn();fn_cgm_t2 cgm=feat_cgm();void *cls=nullptr,*mi=nullptr;
    GUARDED_BEGIN(); if(cfn&&cgm){cls=cfn((void *)(uintptr_t)g_img,"Oak","MonsterBattleAIState");if(cls)mi=cgm(cls,"PickNTriggerBattleAction",0);} GUARDED_END();
    uint64_t code=method_code(mi);
    if(code && hook_ensure_payload()) npf=v30_mkblk(24,code,5,0,0,0);
    g_stunall_on = npf > 0; // AI hook only; unstable StunCommand path is retired.
    snprintf(out, cap, "STUNALL ON freeze=%d mon=%d done=%d", npf, g_stun_last_n, g_stun_last_done);
    ELOGI("STUNALL: %s", out);
}

[[maybe_unused]] static void *aux_thread(void *) {
    sleep(8); /* beri napas boot */
    for (;;) {
        sleep(1);
        if (g_loot_on) {
            GUARDED_BEGIN();
            loot_beat();
            GUARDED_END();
        }
        if (g_stunall_on) {
            GUARDED_BEGIN();
            stun_beat();
            GUARDED_END();
        }
    }
    return nullptr;
}

void feat_godoff(char *out, size_t cap) {
    if (!g_h64_handle) { g_godmode_on = false; snprintf(out, cap, "OK GOD OFF no-payload"); return; }
    int nr = v30_restore1(18);
    if (nr >= 0) g_godmode_on = false;
    snprintf(out, cap, "%s GOD OFF (nr=%d)", nr >= 0 ? "OK" : "ERR", nr);
    ELOGI("GODOFF: %s", out);
}

void feat_hookread2(char *out, size_t cap) {
    if (!g_h64_handle || !g_h64_bus[10]) { snprintf(out, cap, "HR2 no-payload"); return; }
    uint64_t tok = bus_arm();
    g_h64_bus[0] = 10;
    (void)bus_done(tok);
    size_t used = 0;
    used += snprintf(out + used, cap - used, "HR2[");
    for (int s = 0; s < 24; s++)
        used += snprintf(out + used, cap - used, "%s%llu", s ? "," : "",
                         (unsigned long long)g_h64_bus[70 + s]);
    snprintf(out + used, cap - used, "]");
    ELOGI("HR2: %s", out);
}

static const char *v30_kind(uint32_t x, char *d, size_t dc) {
    if (x == 0xD65F03C0u) { snprintf(d, dc, "ret"); return d; }
    if (x == 0xD503201Fu) { snprintf(d, dc, "nop"); return d; }
    if ((x & 0xFF000000u) == 0xBD000000u) { snprintf(d, dc, "ldr-s[0x%x]", ((x >> 10) & 0xFFFu) << 2); return d; }
    if ((x & 0xFFC00000u) == 0xF9400000u) { snprintf(d, dc, "ldr-x[%u]", ((x >> 10) & 0xFFFu) * 8); return d; }
    if ((x & 0xFFC00000u) == 0xF9000000u) { snprintf(d, dc, "str-x[%u]", ((x >> 10) & 0xFFFu) * 8); return d; }
    if ((x & 0xFFE00000u) == 0x52800000u) { snprintf(d, dc, "mov-w"); return d; }
    if ((x & 0x9F000000u) == 0x90000000u) { snprintf(d, dc, "adrp"); return d; }
    if ((x & 0x9F000000u) == 0x10000000u) { snprintf(d, dc, "adr"); return d; }
    if ((x & 0xFC000000u) == 0x14000000u) { snprintf(d, dc, "b"); return d; }
    if ((x & 0xFC000000u) == 0x94000000u) { snprintf(d, dc, "bl"); return d; }
    if ((x & 0xFF000010u) == 0x54000000u) { snprintf(d, dc, "b.cond"); return d; }
    if ((x & 0xFFFFFC1Fu) == 0xD61F0000u) { snprintf(d, dc, "br"); return d; }
    if ((x & 0xFFFFFC1Fu) == 0xD63F0000u) { snprintf(d, dc, "blr"); return d; }
    if ((x & 0xFFFFFC1Fu) == 0xD65F0000u) { snprintf(d, dc, "ret-rn"); return d; }
    if ((x & 0xFF000000u) == 0x58000000u) { snprintf(d, dc, "ldr-lit"); return d; }
    if ((x & 0xFF200000u) == 0x1E200000u) { snprintf(d, dc, "fpu"); return d; }
    if ((x & 0x7F000000u) == 0x34000000u) { snprintf(d, dc, "cbz"); return d; }
    if ((x & 0x7F000000u) == 0x35000000u) { snprintf(d, dc, "cbnz"); return d; }
    snprintf(d, dc, "?");
    return d;
}

void feat_shape(char *out, size_t cap, const char *arg) {
    unsigned long long a = 0;
    if (arg) a = strtoull(arg, nullptr, 16);
    if (!a) { snprintf(out, cap, "ERR shape <hexaddr>"); return; }
    uint32_t w[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    GUARDED_BEGIN();
    for (int i = 0; i < 8; i++) w[i] = *(volatile uint32_t *)(uintptr_t)(a + (uint64_t)i * 4);
    GUARDED_END();
    size_t used = 0;
    used += snprintf(out + used, cap - used, "SHAPE 0x%llx:", a);
    char d[24];
    for (int i = 0; i < 8; i++) {
        used += snprintf(out + used, cap - used, " %x(%s)", w[i], v30_kind(w[i], d, sizeof d));
    }
    ELOGI("SHAPE: %s", out);
}

/* ==================== akhir v30 ==================== */

void feat_godmode(char *out, size_t cap, const char *arg) {
    /* v25: GOD MODE — hook kondisional di ApplyDamage: target==hero_stats → damage 0 */
    if (!hook_ensure_payload()) { snprintf(out, cap, "GOD NOHANDLE"); return; }
    if (!feat_resolve()) { snprintf(out, cap, "GOD resolve=0"); return; }
    fn_inv_t2 inv = nullptr; void *lst = nullptr; int32_t size = 0; void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    void *hs = nullptr;
    if (inv && g_m_stage_inst && g_m_stage_getcm && g_m_cm_players && g_m_char_getstats) {
        void *e = nullptr;
        void *stage = nullptr, *cmgr = nullptr, *plist = nullptr;
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &e);
        GUARDED_END();
        if (ptr_ok(stage)) {
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &e);
            GUARDED_END();
        }
        if (ptr_ok(cmgr)) {
            GUARDED_BEGIN();
            plist = inv(g_m_cm_players, cmgr, nullptr, &e);
            GUARDED_END();
        }
        if (ptr_ok(plist)) {
            int32_t n = 0; void **it = nullptr;
            memcpy(&n, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
            memcpy(&it, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
            if (ptr_ok(it) && n > 0) {
                void *p0 = nullptr;
                memcpy(&p0, reinterpret_cast<const uint8_t *>(it) + 0x20, 8);
                if (ptr_ok(p0)) {
                    GUARDED_BEGIN();
                    hs = inv(g_m_char_getstats, p0, nullptr, &e);
                    GUARDED_END();
                }
            }
        }
    }
    if (!hs) { snprintf(out, cap, "GOD no-hero-stats"); return; }
    unsigned long long addr = method_code(g_m_fos_applydmg);
    if(!addr){snprintf(out,cap,"ERR GOD method unavailable");return;} // IL2CPP metadata methodPointer, not invoker or file offset.
    if (arg && *arg) {
        const char *p = arg;
        while (*p == ' ') p++;
        /* v28: parse hanya kalau benar-benar hex — suffix salah ketik DIABAIKAN (jangan jadi alamat 0) */
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) addr = strtoull(p, nullptr, 16);
        else if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
            addr = strtoull(p, nullptr, 16);
    }
    if (reloc_unsafe(addr)) {
        snprintf(out, cap, "GOD REFUSE: prologue @0x%llx relatif-PC (akan lompat ke sampah)", addr);
        ELOGI("GODMODE: %s", out);
        return;
    }
    uint64_t tok = bus_arm();
    if (!tok) { snprintf(out, cap, "ERR GOD channel unavailable"); return; }
    g_h64_bus[59] = 18; /* v28: SLOT BARU — jangan sentuh slot autohook (13): hindari rebind poison */
    g_h64_bus[60] = addr;
    g_h64_bus[88] = (uint64_t)(uintptr_t)hs;
    __atomic_store_n(&g_h64_bus[0], 15ULL, __ATOMIC_RELEASE);
    if (!bus_done(tok)) { snprintf(out, cap, "GOD TIMEOUT"); ELOGI("GODMODE: %s", out); return; }
    if ((int64_t)g_h64_bus[68]==-5 || (int64_t)g_h64_bus[68]==-11) g_bus_unhealthy=true;
    snprintf(out, cap, "GOD armed hero_stats=0x%llx addr=0x%llx slot=18 np=%lld rc=%lld",
             (unsigned long long)(uintptr_t)hs, addr,
             (long long)g_h64_bus[69], (long long)(int64_t)g_h64_bus[68]);
    ELOGI("GODMODE: %s", out);
}

void feat_hookread(char *out, size_t cap) {
    if (!g_h64_handle || !g_h64_bus[10]) { snprintf(out, cap, "HOOKREAD no-payload"); return; }
    g_h64_bus[1] = 0;
    g_h64_bus[0] = 10;
    for (int i = 0; i < 200 && g_h64_bus[1] != 8; i++) usleep(5000);
    size_t used = 0;
    used += snprintf(out + used, cap - used, "HOOKREAD[");
    for (int s = 0; s < NHOOKT; s++) {
        used += snprintf(out + used, cap - used, "%s%s=%llu", s ? " " : "",
                         g_hook_targets[s].label, (unsigned long long)g_h64_bus[70 + s]);
        if (s == 6) used += snprintf(out + used, cap - used, " |");
    }
    snprintf(out + used, cap - used, "]");
    ELOGI("HOOKREAD: %s", out);
}

/* v6.1: stun chain probe — inspect every link of the stun pipeline on one monster */
void feat_stunprobe(char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "STUNPROBE resolve=0");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "STUNPROBE no-list");
        return;
    }
    void *mon = nullptr;
    for (int32_t i = 0; i < size && !mon; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        void *st = nullptr;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act == 3 || act == 2) && dead == 0 && hp > 0) mon = m;
    }
    if (!ptr_ok(mon)) {
        snprintf(out, cap, "STUNPROBE no-target");
        return;
    }
    void *exc = nullptr;
    sig_atomic_t f0 = g_guard_faults;
    void *cb = nullptr;
    if (g_m_char_getcb) {
        GUARDED_BEGIN();
        cb = inv(g_m_char_getcb, mon, nullptr, &exc);
        GUARDED_END();
    }
    void *sm = nullptr;
    if (ptr_ok(cb)) memcpy(&sm, reinterpret_cast<const uint8_t *>(cb) + 0x58, 8);
    void *stt = nullptr;
    if (g_m_sstate_create) {
        float sd = 3.0f;
        bool ss = true;
        void *a3s[3] = {&mon, &sd, &ss};
        GUARDED_BEGIN();
        stt = inv(g_m_sstate_create, nullptr, a3s, &exc);
        GUARDED_END();
    }
    sig_atomic_t f1 = g_guard_faults;
    int chg = 0;
    if (ptr_ok(stt) && ptr_ok(sm) && g_m_ssm_change) {
        void *a1s[1] = {stt};
        GUARDED_BEGIN();
        (void) inv(g_m_ssm_change, sm, a1s, &exc);
        GUARDED_END();
        chg = 1;
    }
    sig_atomic_t f2 = g_guard_faults;
    void *cmd = nullptr;
    if (g_m_scmd_create) {
        float sd4 = 3.0f;
        bool ss4 = true;
        void *a3c[3] = {&mon, &sd4, &ss4};
        GUARDED_BEGIN();
        cmd = inv(g_m_scmd_create, nullptr, a3c, &exc);
        GUARDED_END();
    }
    sig_atomic_t f3 = g_guard_faults;
    snprintf(out, cap,
             "STUNPROBE m=0x%llx cb=0x%llx sm=0x%llx st=0x%llx chg=%d | cmd=0x%llx | f=%d,%d,%d",
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(cb)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(sm)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stt)), chg,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(cmd)),
             static_cast<int>(f1 - f0), static_cast<int>(f2 - f1), static_cast<int>(f3 - f2));
}

/* hook POC: read a live MethodInfo and locate the executable pointer (methodPointer).
   validates the layout assumption for the ARM64-payload patching plan. */
void feat_hookprobe(char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "HOOKPROBE resolve=0");
        return;
    }
    void *mi = g_m_char_getas;
    if (!mi) mi = g_m_stats_isdead;
    if (!mi) {
        snprintf(out, cap, "HOOKPROBE no-method");
        return;
    }
    uintptr_t klass = 0, nameref = 0;
    memcpy(&klass, reinterpret_cast<const uint8_t *>(mi), 8);
    memcpy(&nameref, reinterpret_cast<const uint8_t *>(mi) + 8, 8);
    char mname[64] = "?";
    if (ptr_ok(reinterpret_cast<void *>(nameref))) {
        memcpy(mname, reinterpret_cast<void *>(nameref), 63);
        mname[63] = 0;
        for (int i = 0; i < 63; i++) {
            unsigned char ch = static_cast<unsigned char>(mname[i]);
            if (ch == 0) break;
            if (ch < 32 || ch > 126) {
                mname[i] = 0;
                break;
            }
        }
    }
    size_t used = static_cast<size_t>(
        snprintf(out, cap, "HOOKPROBE mi=0x%llx klass=0x%llx name='%s'",
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mi)),
                 static_cast<unsigned long long>(klass), mname));
    for (int i = 2; i < 14 && used + 32 < cap; i++) {
        uintptr_t v = 0;
        memcpy(&v, reinterpret_cast<const uint8_t *>(mi) + static_cast<size_t>(i) * 8, 8);
        if (v >= 0x400000000000ull && v < 0x410000000000ull) {
            used += static_cast<size_t>(snprintf(out + used, cap - used, " | code@+0x%x=0x%llx",
                                                 i * 8, static_cast<unsigned long long>(v)));
        }
    }
}

/* v5.0: teleport hero — write position directly (explicit-impl setter first, plain fallback),
   then read back to verify. absolute!=0 => dx,dz are world coords. */
void feat_teleport(float dx, float dz, int absolute, char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "TP resolve=0");
        return;
    }
    fn_inv_t2 inv = feat_inv();
    void *exc = nullptr;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    void *hero = nullptr;
    if (ptr_ok(stage) && g_m_stage_getcm) {
        void *cmgr = nullptr;
        GUARDED_BEGIN();
        cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(cmgr) && g_m_cm_players) {
            void *plist = nullptr;
            GUARDED_BEGIN();
            plist = inv(g_m_cm_players, cmgr, nullptr, &exc);
            GUARDED_END();
            if (ptr_ok(plist)) {
                int32_t ps = 0;
                memcpy(&ps, reinterpret_cast<const uint8_t *>(plist) + 0x18, 4);
                void **pit = nullptr;
                memcpy(&pit, reinterpret_cast<const uint8_t *>(plist) + 0x10, 8);
                if (ps > 0 && ps <= 64 && ptr_ok(pit)) {
                    memcpy(&hero, reinterpret_cast<const uint8_t *>(pit) + 0x20, 8);
                }
            }
        }
    }
    if (!ptr_ok(hero)) {
        snprintf(out, cap, "TP no-hero");
        return;
    }
    float p0[3];
    feat_getpos(hero, p0);
    float tx = absolute ? dx : p0[0] + dx;
    float tz = absolute ? dz : p0[2] + dz;
    if (fabsf(tx) > 4000.0f || fabsf(tz) > 4000.0f) {
        snprintf(out, cap, "TP out-of-range (max 4000m) x=%.0f z=%.0f", tx, tz);
        return;
    }
    struct V3T {
        float x, y, z;
    } target;
    target.x = tx;
    target.y = p0[1];
    target.z = tz;
    void *setm = g_m_char_setpos1 ? g_m_char_setpos1 : g_m_char_setpos2;
    if (!setm) {
        snprintf(out, cap, "TP no-setter (set1=0x%llx set2=0x%llx)",
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_m_char_setpos1)),
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_m_char_setpos2)));
        return;
    }
    sig_atomic_t f0 = g_guard_faults;
    void *a1[1] = {&target};
    GUARDED_BEGIN();
    (void) inv(setm, hero, a1, &exc);
    GUARDED_END();
    sig_atomic_t f1 = g_guard_faults;
    float p1[3];
    feat_getpos(hero, p1);
    snprintf(out, cap,
             "TP src=%c from=(%.1f,%.1f) want=(%.1f,%.1f) now=(%.1f,%.1f) fault=%d ok=%d",
             g_m_char_setpos1 ? 'E' : 'P', p0[0], p0[2], target.x, target.z, p1[0], p1[2],
             static_cast<int>(f1 - f0),
             (fabsf(p1[0] - target.x) < 2.0f && fabsf(p1[2] - target.z) < 2.0f) ? 1 : 0);
}

/* v5.0: official command kill test — MonsterDeadCommand.Create(info) -> Execute(0) */
void feat_kcmd(char *out, size_t cap) {
    if (!feat_resolve() || !g_m_mdc_create || !g_m_cmd_exec) {
        snprintf(out, cap, "KCMD fn-missing");
        return;
    }
    fn_inv_t2 inv = nullptr;
    void *lst = nullptr;
    int32_t size = 0;
    void **items = nullptr;
    feat_mopen(inv, lst, size, items);
    if (!inv || size <= 0) {
        snprintf(out, cap, "KCMD no-list");
        return;
    }
    void *mon = nullptr;
    void *st = nullptr;
    for (int32_t i = 0; i < size && !mon; i++) {
        void *m = feat_mobj(items, i);
        if (!m) continue;
        int act = -1, dead = -1, hp = -1;
        feat_mstat(m, st, act, dead, hp);
        if ((act == 3 || act == 2) && dead == 0 && hp > 0) mon = m;
    }
    if (!ptr_ok(mon)) {
        snprintf(out, cap, "KCMD no-target");
        return;
    }
    void *exc = nullptr;
    /* build trap info (mortal) */
    void *boxed = nullptr;
    int dm = 1000000;
    void *a2[2] = {&mon, &dm};
    GUARDED_BEGIN();
    boxed = inv(g_m_gtd, nullptr, a2, &exc);
    GUARDED_END();
    if (!ptr_ok(boxed)) {
        snprintf(out, cap, "KCMD no-info");
        return;
    }
    static __thread uint8_t ib[0x300];
    memcpy(ib, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
    if (g_m_dis_notmortal) {
        bool nb = false;
        void *a1[1] = {&nb};
        GUARDED_BEGIN();
        (void) inv(g_m_dis_notmortal, ib, a1, &exc);
        GUARDED_END();
    }
    sig_atomic_t f0 = g_guard_faults;
    void *mdc = nullptr;
    void *a1b[1] = {ib};
    GUARDED_BEGIN();
    mdc = inv(g_m_mdc_create, nullptr, a1b, &exc);
    GUARDED_END();
    sig_atomic_t f1 = g_guard_faults;
    int execf = 0;
    if (ptr_ok(mdc)) {
        int ct = 0;
        void *a2b[1] = {&ct};
        GUARDED_BEGIN();
        (void) inv(g_m_cmd_exec, mdc, a2b, &exc);
        GUARDED_END();
        execf = static_cast<int>(g_guard_faults - f1);
    }
    int act2 = -1, dead2 = -1, hp2 = -1;
    void *st2 = nullptr;
    feat_mstat(mon, st2, act2, dead2, hp2);
    snprintf(out, cap,
             "KCMD mon=0x%llx mdc=0x%llx | fInfo=%d fExec=%d | act2=%d dead2=%d hp2=%d",
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mdc)),
             static_cast<int>(f1 - f0), execf, act2, dead2, hp2);
}

/* per-step x-ray of the pulse pipeline for the first active monster */
void feat_pulse1(char *out, size_t cap) {
    if (!feat_resolve()) {
        snprintf(out, cap, "PULSE1 resolve=0");
        return;
    }
    fn_inv_t2 inv = feat_inv();
    void *exc = nullptr;
    sig_atomic_t f0 = g_guard_faults;
    void *stage = nullptr;
    GUARDED_BEGIN();
    stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
    GUARDED_END();
    sig_atomic_t f1 = g_guard_faults;
    void *cmgr = nullptr;
    if (g_m_stage_getcm) {
        GUARDED_BEGIN();
        cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
        GUARDED_END();
    }
    sig_atomic_t f2 = g_guard_faults;
    void *lst = nullptr;
    if (ptr_ok(cmgr)) {
        GUARDED_BEGIN();
        lst = inv(g_m_cm_monsters, cmgr, nullptr, &exc);
        GUARDED_END();
    }
    sig_atomic_t f3 = g_guard_faults;
    void *mon = nullptr;
    int32_t act = -1;
    if (ptr_ok(lst)) {
        int32_t size = 0;
        memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
        void **items = nullptr;
        memcpy(&items, reinterpret_cast<const uint8_t *>(lst) + 0x10, 8);
        if (size > 0 && size <= 256 && ptr_ok(items)) {
            for (int32_t i = 0; i < size && !mon; i++) {
                void *m = nullptr;
                memcpy(&m, reinterpret_cast<const uint8_t *>(items) + 0x20 +
                                static_cast<size_t>(i) * 8,
                       8);
                if (!ptr_ok(m)) continue;
                int32_t a2 = -2;
                if (g_m_char_getas) {
                    void *ab = nullptr;
                    GUARDED_BEGIN();
                    ab = inv(g_m_char_getas, m, nullptr, &exc);
                    GUARDED_END();
                    if (ptr_ok(ab)) {
                        memcpy(&a2, reinterpret_cast<const uint8_t *>(ab) + 0x10, 4);
                    }
                }
                if (a2 == 3) {
                    mon = m;
                    act = a2;
                }
            }
        }
    }
    sig_atomic_t f4 = g_guard_faults;
    void *mstats = nullptr;
    if (ptr_ok(mon) && g_m_char_getstats) {
        GUARDED_BEGIN();
        mstats = inv(g_m_char_getstats, mon, nullptr, &exc);
        GUARDED_END();
    }
    sig_atomic_t f5 = g_guard_faults;
    int dead = -1;
    if (ptr_ok(mstats) && g_m_stats_isdead) {
        void *db = nullptr;
        GUARDED_BEGIN();
        db = inv(g_m_stats_isdead, mstats, nullptr, &exc);
        GUARDED_END();
        if (ptr_ok(db)) {
            uint8_t dd = 0;
            memcpy(&dd, reinterpret_cast<const uint8_t *>(db) + 0x10, 1);
            dead = dd;
        }
    }
    sig_atomic_t f6 = g_guard_faults;
    void *boxed = nullptr;
    if (ptr_ok(mon) && g_m_gdl) {
        int16_t dt = 1025;
        void *sender = nullptr;
        void *target = mon;
        struct NF4 {
            bool has;
            float v;
        } mod = {false, 10.0f};
        struct V34 {
            float x, y, z;
        } dir = {0, 0, 0};
        bool cr = false;
        bool nm = false;
        bool nc = false;
        int sf = 0;
        float sd = 0.0f;
        int kbf = 0;
        struct V34 kdir = {0, 0, 0};
        float kbfc = 0.0f;
        void *he = nullptr;
        uint8_t sfxb[64] = {};
        void *a15[15] = {&dt,   &sender, &target, &mod,  &dir,  &cr,   &nm, &nc,
                         &sf,   &sd,     &kbf,    &kdir, &kbfc, &he,   sfxb};
        GUARDED_BEGIN();
        boxed = inv(g_m_gdl, nullptr, a15, &exc);
        GUARDED_END();
    }
    sig_atomic_t f7 = g_guard_faults;
    sig_atomic_t f8 = f7;
    if (ptr_ok(boxed) && ptr_ok(mstats)) {
        static __thread uint8_t bb[0x300];
        memset(bb, 0, sizeof bb);
        memcpy(bb, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 0x2F8);
        if (g_m_dis_notmortal) {
            bool nb = false;
            void *a1[1] = {&nb};
            GUARDED_BEGIN();
            (void) inv(g_m_dis_notmortal, bb, a1, &exc);
            GUARDED_END();
        }
        void *a3[1] = {bb};
        GUARDED_BEGIN();
        (void) inv(g_m_damage, mstats, a3, &exc);
        GUARDED_END();
        f8 = g_guard_faults;
    }
    snprintf(out, cap,
             "PULSE1 stage=0x%llx cmgr=0x%llx lst=0x%llx mon=0x%llx act=%d stats=0x%llx dead=%d "
             "box=0x%llx | fStage=%d fCm=%d fLs=%d fAct=%d fStats=%d fDead=%d fGDL=%d fDmg=%d",
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stage)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(cmgr)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(lst)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mon)), act,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mstats)), dead,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(boxed)),
             static_cast<int>(f1 - f0), static_cast<int>(f2 - f1), static_cast<int>(f3 - f2),
             static_cast<int>(f4 - f3), static_cast<int>(f5 - f4), static_cast<int>(f6 - f5),
             static_cast<int>(f7 - f6), static_cast<int>(f8 - f7));
}

/* one-shot diagnostic: capture each link of the chain into the ack text */
void feat_diag(char *out, size_t cap) {
    int r_ok = feat_resolve() ? 1 : 0;
    fn_inv_t2 inv = feat_inv();
    void *exc = nullptr;
    void *stage = nullptr, *cmgr = nullptr, *lst = nullptr, *obj0 = nullptr;
    void *statsM = nullptr;
    char objcls[40] = "-";
    char statscls[40] = "-";
    unsigned opts_v = 0;
    int opts_ok = 0;
    int size = -1;
    sig_atomic_t f0 = g_guard_faults;
    if (inv) {
        GUARDED_BEGIN();
        stage = inv(g_m_stage_inst, nullptr, nullptr, &exc);
        GUARDED_END();
    }
    if (ptr_ok(stage)) {
        if (g_m_stage_getcm) {
            GUARDED_BEGIN();
            cmgr = inv(g_m_stage_getcm, stage, nullptr, &exc);
            GUARDED_END();
        }
        if (!ptr_ok(cmgr)) {
            /* field-read fallback removed: that path faults on this build */
        }
    }
    if (ptr_ok(cmgr) && inv) {
        GUARDED_BEGIN();
        lst = inv(g_m_cm_players, cmgr, nullptr, &exc);
        GUARDED_END();
    }
    if (ptr_ok(lst)) {
        memcpy(&size, reinterpret_cast<const uint8_t *>(lst) + 0x18, 4);
        void **items = nullptr;
        memcpy(&items, reinterpret_cast<const uint8_t *>(lst) + 0x10, 8);
        if (ptr_ok(items) && size > 0 && size <= 64) {
            memcpy(&obj0, reinterpret_cast<const uint8_t *>(items) + 0x20, 8);
            if (ptr_ok(obj0)) {
                clsname_of(obj0, objcls, sizeof objcls);
                if (g_m_char_getstats) {
                    GUARDED_BEGIN();
                    statsM = inv(g_m_char_getstats, obj0, nullptr, &exc);
                    GUARDED_END();
                }
                if (ptr_ok(statsM)) clsname_of(statsM, statscls, sizeof statscls);
                /* readback proof: options value straight from the stats object */
                void *statsE = ptr_ok(statsM) ? statsM : nullptr;
                if (statsE && g_m_getopts) {
                    void *boxed = nullptr;
                    GUARDED_BEGIN();
                    boxed = inv(g_m_getopts, statsE, nullptr, &exc);
                    GUARDED_END();
                    if (ptr_ok(boxed)) {
                        memcpy(&opts_v, reinterpret_cast<const uint8_t *>(boxed) + 0x10, 4);
                        opts_ok = 1;
                    }
                }
            }
        }
    }
    snprintf(out, cap,
             "FEATDIAG resolve=%d stage=0x%llx cmgr=0x%llx players=0x%llx size=%d "
             "obj0=0x%llx objcls=%s statsM=0x%llx(%d) statscls=%s "
             "opts=0x%x(%d) faults=%d",
             r_ok, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stage)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(cmgr)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(lst)), size,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(obj0)), objcls,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(statsM)),
             ptr_ok(statsM) ? 1 : 0, statscls,
             static_cast<unsigned>(opts_v), opts_ok,
             static_cast<int>(g_guard_faults - f0));
}

void *feat_thread(void *) {
    ELOGI("FEAT v6 single owner (settle 2s)");
    usleep(2000 * 1000); /* let the app/stage settle */
    if (g_attach && g_dom) {
        typedef void *(*fn_attach_t3)(void *);
        fn_attach_t3 af = nullptr;
        memcpy(&af, &g_attach, sizeof af);
        GUARDED_BEGIN();
        af(reinterpret_cast<void *>(g_dom));
        GUARDED_END();
        ELOGI("FEAT thread attached to il2cpp domain");
    } else {
        ELOGI("FEAT attach skipped fn=0x%llx dom=0x%llx",
              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_attach)),
              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_dom)));
    }
    bool ready_logged = false;
    uint64_t feature_due = 0;
    sig_atomic_t last_faults = g_guard_faults;
    for (;;) {
        if (!ready_logged && feat_resolve()) {
            ELOGI("FEAT resolver ready (Stage/CharacterManager/CharacterStatsBehaviour)");
            ready_logged = true;
        }
        modern_tick();
        if (g_scene_hero && !g_session_fault && now_ms() >= feature_due && __atomic_load_n(&g_foreground, __ATOMIC_ACQUIRE)) {
        feature_due = now_ms() + 900;
        BEAT_BEGIN();
        int ver = g_feat_version;
        if (ver != g_feat_applied_version) {
            int n = feat_apply(1, 0);
            g_feat_applied_version = ver;
            g_last_char_count = (n >= 0) ? n : -1;
            ELOGI("FEAT apply v=%d chars=%d", ver, n);
        } else if (g_pending_ts >= 0) {
            float tv = g_pending_ts_val;
            g_pending_ts = -1;
            char task[160];
            ctl_ts_apply(tv, task, sizeof task);
            ELOGI("FEAT ts deferred -> %s", task);
        } else {
            bool stam = false, mana = false;
            (void) feat_want_mask(&stam, &mana);
            if (stam || mana) {
                (void) feat_apply(0, 1);
            }
            if (g_feats[FEAT_OHK].on || g_feats[FEAT_ONEHP].on) {
                int h = feat_pulse_kill();
                static int plog2 = 0;
                if (++plog2 % 15 == 1) {
                    ELOGI("FEAT kill hit=%d ohk=%d", h, g_feats[FEAT_OHK].on ? 1 : 0);
                }
            }
            if (g_feats[FEAT_STUN].on) {
                int h = feat_pulse_stun();
                static int plog = 0;
                if (++plog % 15 == 1) {
                    ELOGI("FEAT stun hit=%d stunall=1", h);
                }
            }
            if (g_feats[FEAT_AGGRO].on) {
                int an = feat_aggro_sweep();
                static int alog = 0;
                if (++alog % 15 == 1) {
                    ELOGI("FEAT aggro n=%d", an);
                }
            }
            if (g_pending_sweep) {
                g_pending_sweep = 0;
                char stmp[256];
                feat_msweep(9, stmp, sizeof stmp);
                ELOGI("FEAT sweep -> %s", stmp);
            }
            static int beat = 0;
            if (++beat >= 6) { /* ~5.4 s: detect new characters entering the stage */
                beat = 0;
                int c = feat_count();
                if (c >= 0 && c != g_last_char_count) {
                    g_last_char_count = c;
                    g_feat_version++;
                }
            }
        }
        BEAT_END();
        }
        modern_publish();
        if (g_guard_faults != last_faults) {
            g_session_fault = true;
            ELOGI("FEAT guard: +%d faulted (total=%d) pc=0x%llx addr=0x%llx",
                  static_cast<int>(g_guard_faults - last_faults),
                  static_cast<int>(g_guard_faults),
                  static_cast<unsigned long long>(g_fault_pcs[(g_fault_pc_idx - 1) & 15]),
                  static_cast<unsigned long long>(g_fault_last_addr));
            char reset[512];
            BEAT_BEGIN(); modern_reset(reset, sizeof reset); BEAT_END();
            last_faults = g_guard_faults;
        }
        usleep(250 * 1000);
    }
    return nullptr;
}

void ctl_exec(const char *raw, char *ack, size_t cap) {
    typedef void *(*fn_inv_t)(void *, void *, void **, void **);
    typedef void *(*fn_sn_t)(const char *);
    fn_inv_t inv = nullptr;
    fn_sn_t sn = nullptr;
    memcpy(&inv, &ctl_fn_invoke, sizeof inv);
    memcpy(&sn, &ctl_fn_strnew, sizeof sn);
    float val = 0.0f;
    char key[64] = {};

    if (strncmp(raw, "mod ", 4) == 0) {
        if (sscanf(raw + 4, "%f %63s", &val, key) < 2) { snprintf(ack, cap, "ERR parse"); return; }
        if (!ctl_instance) { snprintf(ack, cap, "ERR instance-null (masuk gameplay dulu)"); return; }
        if (!inv || !sn || !ctl_mi_mod) { snprintf(ack, cap, "ERR missing fn/method"); return; }
        void *s = nullptr;
        GUARDED_BEGIN();
        s = sn(key);
        GUARDED_END();
        if (!s) { snprintf(ack, cap, "ERR strnew failed"); return; }
        bool allow = true;
        void *args[3] = { &val, s, &allow };
        void *exc = nullptr;
        void *res = nullptr;
        GUARDED_BEGIN();
        res = inv(ctl_mi_mod, ctl_instance, args, &exc);
        GUARDED_END();
        snprintf(ack, cap, "OK mod %.2f key=%s modsN=%llu exc=%d", static_cast<double>(val), key,
                 static_cast<unsigned long long>(ctl_mods_count()), exc != nullptr ? 1 : 0);
        (void) res;
        return;
    }
    if (strncmp(raw, "unmod ", 6) == 0) {
        if (sscanf(raw + 6, "%63s", key) < 1) { snprintf(ack, cap, "ERR parse"); return; }
        if (!ctl_instance) { snprintf(ack, cap, "ERR instance-null"); return; }
        if (!inv || !sn || !ctl_mi_unmod) { snprintf(ack, cap, "ERR missing fn/method"); return; }
        void *s = nullptr;
        GUARDED_BEGIN();
        s = sn(key);
        GUARDED_END();
        if (!s) { snprintf(ack, cap, "ERR strnew failed"); return; }
        void *args[1] = { s };
        void *exc = nullptr;
        void *res = nullptr;
        GUARDED_BEGIN();
        res = inv(ctl_mi_unmod, ctl_instance, args, &exc);
        GUARDED_END();
        snprintf(ack, cap, "OK unmod key=%s modsN=%llu exc=%d", key,
                 static_cast<unsigned long long>(ctl_mods_count()), exc != nullptr ? 1 : 0);
        (void) res;
        return;
    }
    if (strncmp(raw, "setmax ", 7) == 0) {
        if (sscanf(raw + 7, "%f", &val) < 1) { snprintf(ack, cap, "ERR parse"); return; }
        if (!ctl_instance || !inv || !ctl_mi_setmax) { snprintf(ack, cap, "ERR not-ready"); return; }
        void *args[1] = { &val };
        void *exc = nullptr;
        GUARDED_BEGIN();
        (void) inv(ctl_mi_setmax, ctl_instance, args, &exc);
        GUARDED_END();
        snprintf(ack, cap, "OK setmax %.4f exc=%d", static_cast<double>(val), exc != nullptr ? 1 : 0);
        return;
    }
    if (strncmp(raw, "resetmax", 8) == 0) {
        if (!ctl_instance || !inv || !ctl_mi_resetmax) { snprintf(ack, cap, "ERR not-ready"); return; }
        void *exc = nullptr;
        GUARDED_BEGIN();
        (void) inv(ctl_mi_resetmax, ctl_instance, nullptr, &exc);
        GUARDED_END();
        snprintf(ack, cap, "OK resetmax exc=%d", exc != nullptr ? 1 : 0);
        return;
    }
    if (strncmp(raw, "clearmods", 9) == 0) {
        if (!ctl_instance || !inv || !ctl_mi_clear) { snprintf(ack, cap, "ERR not-ready"); return; }
        void *exc = nullptr;
        GUARDED_BEGIN();
        (void) inv(ctl_mi_clear, ctl_instance, nullptr, &exc);
        GUARDED_END();
        snprintf(ack, cap, "OK clearmods modsN=%llu exc=%d",
                 static_cast<unsigned long long>(ctl_mods_count()), exc != nullptr ? 1 : 0);
        return;
    }
    if (strncmp(raw, "status", 6) == 0) {
        char fl[160] = {};
        size_t fu = 0;
        for (int i = 0; i < FEAT_COUNT && fu + 24 < sizeof fl; i++) {
            if (g_feats[i].on) {
                fu += static_cast<size_t>(
                    snprintf(fl + fu, sizeof fl - fu, "%s,", g_feats[i].id));
            }
        }
        snprintf(ack, cap, "STATUS instance=0x%llx modsN=%llu feats=[%s]",
                 static_cast<unsigned long long>(reinterpret_cast<uint64_t>(ctl_instance)),
                 static_cast<unsigned long long>(ctl_mods_count()), fl[0] ? fl : "-");
        return;
    }
    if (strcmp(raw, "panic") == 0) {
        const bool had_timescale=g_feats[FEAT_TIMESCALE].on || g_timescale_owned;
        for (int i = 0; i < FEAT_COUNT; ++i) g_feats[i].on = false;
        g_speed_on = g_nocd_on = g_loot_on = g_stunall_on = 0;
        g_critdmg_on = false;
        bool restored = true;
        if (g_h64_handle) for (int slot = 0; slot < WSM_HOOK_SLOTS; ++slot) {
            if (v30_restore1(slot) < 0) { restored = false; break; }
        }
        if (__atomic_load_n(&g_identity_ok, __ATOMIC_ACQUIRE)) {
            (void) feat_apply(1, 0);
            if(had_timescale){char tmp[256];ctl_ts_apply(0,tmp,sizeof tmp);if(strncmp(tmp,"OK ",3))restored=false;}
        }
        ++g_feat_version; g_feat_applied_version = g_feat_version;
        snprintf(ack, cap, "%s PANIC restore=%s", restored ? "OK" : "ERR", restored ? "complete" : "failed; restart target");
        return;
    }
    if (strncmp(raw, "tpr ", 4) == 0) {
        float dx = 0, dz = 0;
        if (sscanf(raw + 4, "%f %f", &dx, &dz) == 2) feat_teleport(dx, dz, 0, ack, cap);
        else snprintf(ack, cap, "ERR tpr <dx> <dz>");
        return;
    }
    if (strncmp(raw, "tp ", 3) == 0) {
        float x = 0, z = 0;
        if (sscanf(raw + 3, "%f %f", &x, &z) == 2) feat_teleport(x, z, 1, ack, cap);
        else snprintf(ack, cap, "ERR tp <x> <z>");
        return;
    }
    if (strncmp(raw, "payloadrun", 10) == 0) {
        feat_payloadrun(ack, cap);
        return;
    }
    if (strncmp(raw, "nbdl", 4) == 0) {
        feat_nbdl(ack, cap, raw + 4);
        return;
    }
    if (strncmp(raw, "nbpoc2", 6) == 0) {
        feat_nbpoc2(ack, cap);
        return;
    }
    if (strncmp(raw, "nbpoc3", 6) == 0) {
        feat_nbpoc3(ack, cap);
        return;
    }
    if (strncmp(raw, "hookdump", 8) == 0) {
        feat_hookdump(ack, cap);
        return;
    }
    if (strncmp(raw, "hookcount", 9) == 0) {
        feat_hookcount(ack, cap, raw + 9);
        return;
    }
    if (strncmp(raw, "hookall", 7) == 0) {
        feat_hookall(ack, cap, raw + 7);
        return;
    }
    if (strncmp(raw, "hookread2", 9) == 0) {
        feat_hookread2(ack, cap);
        return;
    }
    if (strncmp(raw, "hookread", 8) == 0) {
        feat_hookread(ack, cap);
        return;
    }
    if (strncmp(raw, "peek ", 5) == 0) {
        feat_peek(ack, cap, raw + 5);
        return;
    }
    if (strncmp(raw, "hookverify", 10) == 0) {
        feat_hookverify(ack, cap);
        return;
    }
    if (strncmp(raw, "klassof", 7) == 0) {
        feat_klassof(ack, cap);
        return;
    }
    if (strncmp(raw, "guardhp", 7) == 0) {
        feat_guardhp(ack, cap);
        return;
    }
    if (strncmp(raw, "guardread", 9) == 0) {
        feat_guardread(ack, cap);
        return;
    }
    if (strncmp(raw, "guardoff", 8) == 0) {
        feat_guardoff(ack, cap);
        return;
    }
    if (strncmp(raw, "dron", 4) == 0) {
        const char *a = raw + 4;
        if (a && strstr(a, "hero")) feat_dronew(ack, cap, "hero");
        else feat_dron(ack, cap);
        return;
    }
    if (strncmp(raw, "drread", 6) == 0) {
        feat_drread(ack, cap);
        return;
    }
    if (strncmp(raw, "droff", 5) == 0) {
        feat_droff(ack, cap);
        return;
    }
    if (strncmp(raw, "hookabs", 7) == 0) {
        feat_hookabs(ack, cap, raw + 7);
        return;
    }
    if (strncmp(raw, "godmode", 7) == 0) {
        feat_godmode(ack, cap, raw + 7);
        return;
    }
    if (strncmp(raw, "godoff", 6) == 0) {
        feat_godoff(ack, cap);
        return;
    }
    if (strncmp(raw, "speed ", 6) == 0) {
        feat_speed(ack, cap, raw + 6);
        return;
    }
    if (strncmp(raw, "nocd ", 5) == 0) {
        feat_nocd(ack, cap, raw + 5);
        return;
    }
    if (strncmp(raw, "loot ", 5) == 0) {
        feat_loot(ack, cap, raw + 5);
        return;
    }
    if (strncmp(raw, "stunall ", 8) == 0) {
        feat_stunall(ack, cap, raw + 8);
        return;
    }
    if (strncmp(raw, "shape ", 6) == 0) {
        feat_shape(ack, cap, raw + 6);
        return;
    }
    if (strncmp(raw, "hookself", 8) == 0) {
        feat_hookself(ack, cap);
        return;
    }
    if (strncmp(raw, "stunui", 6) == 0) {
        feat_stunui(ack, cap);
        return;
    }
    if (strncmp(raw, "stunvec ", 8) == 0) {
        int v = 0;
        if (sscanf(raw + 8, "%d", &v) == 1 && v >= 1 && v <= 3) {
            g_stun_vec = v;
            snprintf(ack, cap, "OK stunvec=%d (%s)", v,
                     v == 1 ? "state-inject" : (v == 2 ? "command" : "legacy-damage"));
        } else {
            snprintf(ack, cap, "ERR stunvec <1|2|3>");
        }
        return;
    }
    if (strncmp(raw, "stunprobe", 9) == 0) {
        feat_stunprobe(ack, cap);
        return;
    }
    if (strncmp(raw, "kcmd", 4) == 0) {
        feat_kcmd(ack, cap);
        return;
    }
    if (strncmp(raw, "hookprobe", 9) == 0) {
        feat_hookprobe(ack, cap);
        return;
    }
    if (strncmp(raw, "aggro1", 6) == 0) {
        feat_aggro_diag(ack, cap);
        return;
    }
    if (strncmp(raw, "sweep", 5) == 0) {
        if (g_menu_ctx) {
            g_pending_sweep = 1;
            snprintf(ack, cap, "QUEUED sweep (engine <=1s)");
        } else {
            feat_msweep(9, ack, cap);
        }
        return;
    }
    if (strncmp(raw, "kill1", 5) == 0) {
        feat_kill_try(1, ack, cap);
        return;
    }
    if (strncmp(raw, "kill2", 5) == 0) {
        feat_kill_try(2, ack, cap);
        return;
    }
    if (strncmp(raw, "kill3", 5) == 0) {
        feat_kill_try(3, ack, cap);
        return;
    }
    if (strncmp(raw, "kill4", 5) == 0) {
        feat_kill_try(4, ack, cap);
        return;
    }
    if (strncmp(raw, "kill5", 5) == 0) {
        feat_kill_try(5, ack, cap);
        return;
    }
    if (strncmp(raw, "kill6", 5) == 0) {
        feat_kill_try(6, ack, cap);
        return;
    }
    if (strncmp(raw, "kill7", 5) == 0) {
        feat_kill_try(7, ack, cap);
        return;
    }
    if (strncmp(raw, "kill8", 5) == 0) {
        feat_kill_try(8, ack, cap);
        return;
    }
    if (strncmp(raw, "mpos", 4) == 0) {
        feat_mpos(ack, cap);
        return;
    }
    if (strncmp(raw, "mnear", 5) == 0) {
        int mode = 9;
        (void) sscanf(raw + 5, "%d", &mode);
        feat_mnear(mode, ack, cap);
        return;
    }
    if (strncmp(raw, "mlist", 5) == 0) {
        feat_mlist(ack, cap);
        return;
    }
    if (strncmp(raw, "mread", 5) == 0) {
        int idx = -1;
        if (sscanf(raw + 5, "%d", &idx) == 1) feat_mdmg(idx, 0, ack, cap);
        else snprintf(ack, cap, "ERR mread <idx>");
        return;
    }
    if (strncmp(raw, "mdmg", 4) == 0) {
        int idx = -1, mode = 5;
        int n = sscanf(raw + 4, "%d %d", &idx, &mode);
        if (n >= 1) feat_mdmg(idx, mode, ack, cap);
        else snprintf(ack, cap, "ERR mdmg <idx> [5|6]");
        return;
    }
    if (strncmp(raw, "msweep", 6) == 0) {
        int mode = 5;
        (void) sscanf(raw + 6, "%d", &mode);
        feat_msweep(mode, ack, cap);
        return;
    }
    if (strncmp(raw, "pulse1", 6) == 0) {
        feat_pulse1(ack, cap);
        return;
    }
    if (strncmp(raw, "pulsesrc", 8) == 0) {
        int v = 0;
        if (sscanf(raw + 8, "%d", &v) == 1) g_pulse_src = v;
        snprintf(ack, cap, "OK pulsesrc=%d (0=trap 1=lua)", g_pulse_src);
        return;
    }
    if (strncmp(raw, "featdiag", 8) == 0) {
        feat_diag(ack, cap);
        return;
    }
    if (strncmp(raw, "feat ", 5) == 0) {
        char id[48] = {}; float value = 0;
        if (sscanf(raw + 5, "%47s %f", id, &value) != 2 || !isfinite(value)) { snprintf(ack, cap, "ERR parse"); return; }
        for (int i = 0; i < FEAT_COUNT; ++i) {
            if (strcmp(id, g_feats[i].id) != 0) continue;
            FeatSlot before = g_feats[i];
            g_feats[i].on = value > 0;
            if (i == FEAT_DMG || i == FEAT_AURA || i == FEAT_TIMESCALE) {
                if (value > 0) g_feats[i].value = value;
            }
            if (i == FEAT_TIMESCALE) {
                ctl_ts_apply(value, ack, cap);
                if (strncmp(ack, "OK", 2)) g_feats[i] = before;
                return;
            }
            if (i == FEAT_ONEHP) { g_scratch_reset = 1; g_scratched_n = 0; }
            int applied = feat_apply(1, 0);
            ++g_feat_version; g_feat_applied_version = g_feat_version;
            if (applied <= 0) { g_feats[i] = before; snprintf(ack, cap, "ERR no live players"); return; }
            g_last_char_count = applied;
            snprintf(ack, cap, "OK feat=%s %s value=%.2f players=%d", id, value > 0 ? "ON" : "OFF", (double)g_feats[i].value, applied);
            return;
        }
        snprintf(ack, cap, "ERR unsupported feature"); return;
    }
    snprintf(ack, cap, "ERR unknown cmd");
}

#include "modern_control.inc"

void *control_thread(void *) {
    ELOGI("CTL thread up (paths=%zu)", kCtlCount);
    char last[kCtlCount][256] = {};
    char raw[256];
    char ack[4096];
    for (;;) {
        BEAT_BEGIN();
        usleep(1000 * 1000); /* 1 s poll */
        for (size_t i = 0; i < kCtlCount; i++) {
            int n = ctl_read(kCtlPaths[i].cmd, raw, sizeof raw);
            if (n <= 0) continue;
            if (strcmp(raw, last[i]) == 0) continue; /* dedupe: skip this path, other paths may hold new cmds */
            strncpy(last[i], raw, sizeof last[i] - 1);
            last[i][sizeof last[i] - 1] = '\0';
            for (char *p = raw; *p != '\0'; p++) {
                if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
            }
            if (raw[0] == '\0') break;
            ack[0] = '\0';
            unsigned long long request = 0; int offset = 0;
            if (raw[0] == '@' && sscanf(raw, "@%llu %n", &request, &offset) == 1 && offset > 0) {
                char response[4000]; modern_request(raw + offset, response, sizeof response);
                snprintf(ack, sizeof ack, "{\"request\":%llu,%s", request, response[0] == '{' ? response + 1 : "\"state\":\"fault\"}");
            } else modern_request(raw, ack, sizeof ack);
            ELOGI("CTL[%zu] %s -> %s", i, raw, ack);
            ctl_write(kCtlPaths[i].ack, ack);
            break;
        }
        BEAT_END();
    }
    return nullptr;
}

/* ---- G12 in-game menu: load the wsm.WsmMenu DEX in-memory and show the
   overlay; buttons call back into ctl_exec via a registered native method. ---- */
jstring menu_exec(JNIEnv *env, jclass, jstring cmd) {
    char ack[4096] = {};
    const char *c = cmd ? env->GetStringUTFChars(cmd, nullptr) : nullptr;
    if (!c) return env->NewStringUTF("{\"state\":\"rejected\"}");
    if (strncmp(c, "__identity ", 11) == 0) {
        char package[80], version[48], tail; unsigned long long code = 0;
        bool valid = sscanf(c + 11, "%79s %47s %llu %c", package, version, &code, &tail) == 3 &&
            wsm::identity_matches(package, version, code);
        __atomic_store_n(&g_identity_ok, valid ? 1 : 0, __ATOMIC_RELEASE);
        snprintf(ack, sizeof ack, "{\"state\":\"%s\"}", valid ? "applied" : "rejected");
    } else if (strncmp(c, "__activity ", 11) == 0) {
        bool resumed = strcmp(c + 11, "resumed") == 0;
        __atomic_store_n(&g_foreground, resumed ? 1 : 0, __ATOMIC_RELEASE);
        if (!resumed) { uint64_t seq; g_runtime.submit("panic", seq); }
        snprintf(ack, sizeof ack, "{\"state\":\"applied\"}");
    } else modern_request(c, ack, sizeof ack);
    env->ReleaseStringUTFChars(cmd, c);
    return env->NewStringUTF(ack);
}

void *menu_thread(void *arg) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    usleep(6000 * 1000); /* let the activity/app settle */
    if (!g_vm) {
        close(fd);
        ELOGI("MENU no vm");
        return nullptr;
    }
    JNIEnv *env = nullptr;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
        close(fd);
        ELOGI("MENU attach failed");
        return nullptr;
    }
    struct MenuScope {
        JNIEnv *env; JavaVM *vm; int fd; bool frame;
        ~MenuScope() { if (env->ExceptionCheck()) env->ExceptionClear(); if (frame) env->PopLocalFrame(nullptr); close(fd); unsetenv("WSM_DEX_FD"); vm->DetachCurrentThread(); }
    } scope{env, g_vm, fd, false};
    if (env->PushLocalFrame(96) < 0) return nullptr;
    scope.frame = true;
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > (4 << 20)) {
        ELOGI("MENU dex fstat fail (fd=%d)", fd);
        return nullptr;
    }
    size_t dsize = static_cast<size_t>(st.st_size);
    void *map = mmap(nullptr, dsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        ELOGI("MENU dex mmap fail");
        return nullptr;
    }
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(dsize));
    if (!arr) {
        munmap(map, dsize);
        ELOGI("MENU oom");
        return nullptr;
    }
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(dsize), reinterpret_cast<const jbyte *>(map));
    munmap(map, dsize);

    jclass bbCls = env->FindClass("java/nio/ByteBuffer");
    jmethodID wrap = env->GetStaticMethodID(bbCls, "wrap", "([B)Ljava/nio/ByteBuffer;");
    jobject buf = env->CallStaticObjectMethod(bbCls, wrap, arr);
    env->DeleteLocalRef(arr);

    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID getSys = env->GetStaticMethodID(clCls, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    jobject parent = env->CallStaticObjectMethod(clCls, getSys);
    jclass idlCls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID ctor = env->GetMethodID(idlCls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject loader = env->NewObject(idlCls, ctor, buf, parent);
    if (env->ExceptionCheck() || !loader) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ELOGI("MENU dex loader failed");
        return nullptr;
    }
    jmethodID loadClass = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring nm = env->NewStringUTF("wsm.WsmMenu");
    jclass menuCls = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, nm));
    if (env->ExceptionCheck() || !menuCls) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ELOGI("MENU class load failed");
        return nullptr;
    }
    ELOGI("MENU dex loaded (wsm.WsmMenu)");

    JNINativeMethod nms[1];
    nms[0].name = const_cast<char *>("exec");
    nms[0].signature = const_cast<char *>("(Ljava/lang/String;)Ljava/lang/String;");
    nms[0].fnPtr = reinterpret_cast<void *>(menu_exec);
    if (env->RegisterNatives(menuCls, nms, 1) == 0) {
        ELOGI("MENU native registered");
    } else {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ELOGI("MENU RegisterNatives failed");
        return nullptr;
    }

    /* Preferred: attach into the game's Activity (no overlay permission needed —
       GT does not declare SYSTEM_ALERT_WINDOW). Fallback: overlay via app ctx. */
    jobject act = nullptr;
    {
        jclass atCls0 = env->FindClass("android/app/ActivityThread");
        if (atCls0) {
            jmethodID curAT = env->GetStaticMethodID(atCls0, "currentActivityThread", "()Landroid/app/ActivityThread;");
            jobject atObj = curAT ? env->CallStaticObjectMethod(atCls0, curAT) : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                ELOGI("MENU currentActivityThread blocked");
            }
            if (atObj) {
                jclass atObjCls = env->GetObjectClass(atObj);
                jfieldID fActs = env->GetFieldID(atObjCls, "mActivities", "Landroid/util/ArrayMap;");
                if (!fActs && env->ExceptionCheck()) {
                    env->ExceptionClear();
                    ELOGI("MENU mActivities field blocked");
                }
                if (fActs) {
                    jobject acts = env->GetObjectField(atObj, fActs);
                    if (acts) {
                        jclass amCls = env->GetObjectClass(acts);
                        jmethodID mSize = env->GetMethodID(amCls, "size", "()I");
                        jmethodID mVal = env->GetMethodID(amCls, "valueAt", "(I)Ljava/lang/Object;");
                        jint n = (mSize && mVal) ? env->CallIntMethod(acts, mSize) : 0;
                        for (jint i = 0; i < n && !act; i++) {
                            jobject rec = env->CallObjectMethod(acts, mVal, i);
                            if (rec) {
                                jclass recCls = env->GetObjectClass(rec);
                                jfieldID fAct = env->GetFieldID(recCls, "activity", "Landroid/app/Activity;");
                                if (!fAct && env->ExceptionCheck()) {
                                    env->ExceptionClear();
                                    ELOGI("MENU record.activity field blocked");
                                }
                                if (fAct) act = env->GetObjectField(rec, fAct);
                            }
                        }
                    }
                }
            }
        }
    }
    if (act) {
        jmethodID showAct = env->GetStaticMethodID(menuCls, "showInActivity", "(Landroid/app/Activity;)V");
        if (showAct) {
            env->CallStaticVoidMethod(menuCls, showAct, act);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                ELOGI("MENU showInActivity threw");
            } else {
                ELOGI("MENU showInActivity called");
            }
            return nullptr;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        ELOGI("MENU showInActivity missing");
    }
    ELOGI("MENU activity path unavailable — overlay fallback");

    jclass atCls = env->FindClass("android/app/ActivityThread");
    jobject ctx = nullptr;
    if (atCls) {
        jmethodID curApp = env->GetStaticMethodID(atCls, "currentApplication", "()Landroid/app/Application;");
        if (curApp) ctx = env->CallStaticObjectMethod(atCls, curApp);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ELOGI("MENU currentApplication blocked");
        }
    }
    if (!ctx) {
        ELOGI("MENU no context — overlay skipped");
        return nullptr;
    }

    jmethodID showM = env->GetStaticMethodID(menuCls, "show", "(Landroid/content/Context;)V");
    if (showM) {
        env->CallStaticVoidMethod(menuCls, showM, ctx);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            ELOGI("MENU show threw");
        } else {
            ELOGI("MENU show called");
        }
    } else {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ELOGI("MENU show method missing");
    }
    return nullptr;
}

bool safe_elf_parse(uint64_t base, ElfSyms *out) {
    struct sigaction sa{}, old{};
    sa.sa_sigaction = elf_segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, &old);
    g_elf_guard = 1;
    bool ok = false;
    if (sigsetjmp(g_elf_jmp, 1) == 0) {
        ok = elf_parse(base, out);
    }
    g_elf_guard = 0;
    sigaction(SIGSEGV, &old, nullptr);
    return ok;
}

bool safe_elf_lookup(const ElfSyms &e, const char *want, uint64_t *addr_out) {
    struct sigaction sa{}, old{};
    sa.sa_sigaction = elf_segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, &old);
    g_elf_guard = 1;
    bool ok = false;
    *addr_out = 0;
    if (sigsetjmp(g_elf_jmp, 1) == 0) {
        ok = elf_lookup(e, want, addr_out);
    }
    g_elf_guard = 0;
    sigaction(SIGSEGV, &old, nullptr);
    return ok;
}

/* Outbound JNI probe: call fixture class if it exists (silent in the game process). */
bool jni_probe() {
    if (!g_vm) return false;
    JNIEnv *env = nullptr;
    bool attached = false;
    jint r = g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (r == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            ELOGI("probe: attach failed");
            return false;
        }
        attached = true;
    } else if (r != JNI_OK || !env) {
        return false;
    }
    bool ok = false;
    if (env->PushLocalFrame(24) == JNI_OK) {
        jclass at = env->FindClass("android/app/ActivityThread");
        jmethodID curr = at ? env->GetStaticMethodID(
                                  at, "currentApplication", "()Landroid/app/Application;")
                            : nullptr;
        jobject app = nullptr;
        if (curr) {
            const uint64_t dl = now_ms() + 15000;
            while (now_ms() < dl) {
                app = env->CallStaticObjectMethod(at, curr);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    app = nullptr;
                }
                if (app) break;
                usleep(250 * 1000);
            }
        }
        if (app) {
            jclass app_cls = env->GetObjectClass(app);
            jmethodID get_cl =
                app_cls ? env->GetMethodID(app_cls, "getClassLoader",
                                           "()Ljava/lang/ClassLoader;")
                        : nullptr;
            jobject cl = get_cl ? env->CallObjectMethod(app, get_cl) : nullptr;
            jclass cl_cls = cl ? env->GetObjectClass(cl) : nullptr;
            jmethodID load_cls =
                cl_cls ? env->GetMethodID(cl_cls, "loadClass",
                                          "(Ljava/lang/String;)Ljava/lang/Class;")
                       : nullptr;
            if (load_cls) {
                jstring cname = env->NewStringUTF("com.wsm.fixture.Probe");
                jobject probe_cls = cname ? env->CallObjectMethod(cl, load_cls, cname) : nullptr;
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    probe_cls = nullptr;
                }
                if (probe_cls) {
                    jmethodID m = env->GetStaticMethodID(
                        reinterpret_cast<jclass>(probe_cls), "engineAlive",
                        "(Ljava/lang/String;)V");
                    if (m) {
                        char info[96];
                        snprintf(info, sizeof info, "%s pid=%d", WSM_BUILD_STAMP,
                                 static_cast<int>(getpid()));
                        jstring jinfo = env->NewStringUTF(info);
                        if (jinfo) {
                            env->CallStaticVoidMethod(reinterpret_cast<jclass>(probe_cls), m,
                                                      jinfo);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            ok = true;
                        }
                    }
                }
            }
        }
        env->PopLocalFrame(nullptr);
    }
    if (ok) {
        ELOGI("probe: fixture callback delivered (2-way JNI OK)");
    } else {
        ELOGI("probe: fixture class absent or app not ready (expected in game)");
    }
    if (attached) g_vm->DetachCurrentThread();
    return ok;
}

void *engine_thread(void *) {
    const char *fdstr = getenv("WSM_CHANNEL_FD");
    const char *noncestr = getenv("WSM_NONCE");
    const char *dfdstr = getenv("WSM_DEX_FD");
    if (dfdstr) g_menu_dex_fd = atoi(dfdstr);
    if (!fdstr) {
        ELOGI("no WSM_CHANNEL_FD env — engine aborted");
        return nullptr;
    }
    char path[64];
    snprintf(path, sizeof path, "/proc/self/fd/%s", fdstr);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        ELOGI("channel open(%s) errno=%d", path, errno);
        return nullptr;
    }
    void *map = mmap(nullptr, WSM_CHANNEL_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        ELOGI("channel mmap errno=%d", errno);
        close(fd);
        return nullptr;
    }
    wsm_hello_t *hello = static_cast<wsm_hello_t *>(map);
    wsm_ack_t *ack =
        reinterpret_cast<wsm_ack_t *>(static_cast<uint8_t *>(map) + WSM_OFF_ACK);
    wsm_probe_t *probe =
        reinterpret_cast<wsm_probe_t *>(static_cast<uint8_t *>(map) + WSM_OFF_PROBE);

    ELOGI("ALIVE pid=%d uid=%u hello_magic=0x%x hello_proto=%u", static_cast<int>(getpid()),
          static_cast<unsigned>(getuid()), hello->magic, hello->protocol);

    if (hello->magic != WSM_HELLO_MAGIC || hello->protocol != WSM_PROTOCOL_VERSION ||
        hello->loader_pid != (uint32_t)getpid() || hello->target_uid != (uint32_t)getuid() ||
        !noncestr || hello->nonce != strtoull(noncestr, nullptr, 16) ||
        strncmp(hello->build, WSM_BUILD_STAMP, sizeof hello->build) ||
        strncmp(hello->abi, WSM_ABI_STR, sizeof hello->abi)) {
        ELOGI("BOOTSTRAP identity refused"); munmap(map, WSM_CHANNEL_MAP_SIZE); close(fd); return nullptr;
    }
    memset(ack, 0, sizeof *ack);
    ack->protocol = WSM_PROTOCOL_VERSION;
    if (hello->magic == WSM_HELLO_MAGIC) {
        ack->nonce_echo = hello->nonce;
    } else if (noncestr) {
        ack->nonce_echo = strtoull(noncestr, nullptr, 16);
        snprintf(ack->note, sizeof ack->note, "hello missing, used env nonce");
    }
    ack->engine_pid = static_cast<uint32_t>(getpid());
    ack->engine_uid = static_cast<uint32_t>(getuid());
    ack->state = WSM_STATE_READY;
    ack->caps = 0;
    snprintf(ack->build, sizeof ack->build, "%s", WSM_BUILD_STAMP);
    snprintf(ack->abi, sizeof ack->abi, "%s", WSM_ABI_STR);
    __sync_synchronize();
    __atomic_store_n(&ack->magic, WSM_ACK_MAGIC, __ATOMIC_RELEASE);
    ELOGI("handshake ack written (proto=%u nonce=%016llx)", WSM_PROTOCOL_VERSION,
          static_cast<unsigned long long>(ack->nonce_echo));

    if (jni_probe()) {
        ack->caps |= WSM_CAP_FIXTURE_CALLBACK_OK;
    } else {
        ack->caps |= WSM_CAP_FIXTURE_CLASS_ABSENT;
    }

    /* read-only capability scan: wait for libil2cpp (<=30s) */
    const uint64_t deadline = now_ms() + 30000;
    char mapline[256];
    uint64_t base = 0;
    while (now_ms() < deadline) {
        base = find_lib_base("libil2cpp.so", mapline, sizeof mapline);
        if (base != 0) break;
        usleep(500 * 1000);
    }
    memset(probe, 0, sizeof *probe);
    probe->ts_ms = now_ms();
    if (base == 0) {
        probe->stage = WSM_PROBE_STAGE_ABSENT;
        snprintf(probe->text, sizeof probe->text,
                 "libil2cpp absent within 30s (expected for fixture/early load)");
        __sync_synchronize();
        probe->magic = WSM_PROBE_MAGIC;
        ELOGI("probe: libil2cpp absent (30s window)");
        return nullptr;
    }
    ELOGI("il2cpp mapline: %s", mapline);
    ack->caps |= WSM_CAP_LIBIL2CPP_SEEN;
    probe->il2cpp_base = base;

    /* ELF dynsym parse (read-only; works for ARM64 libs under the bridge).
       The first libil2cpp mapping can be a transient houdini alias — try every
       offset-0 candidate (and retry for a window) until one parses cleanly. */
    ElfSyms es{};
    bool parsed = safe_elf_parse(base, &es);
    const uint64_t dl_parse = now_ms() + 30000;
    while (!parsed && now_ms() < dl_parse) {
        uint64_t cand[8];
        int nb = collect_lib_bases("libil2cpp.so", cand, 8);
        for (int i = 0; i < nb; i++) {
            if (safe_elf_parse(cand[i], &es)) {
                base = cand[i];
                parsed = true;
                ELOGI("elf parsed at candidate base=0x%llx (#%d/%d)",
                      static_cast<unsigned long long>(base), i + 1, nb);
                break;
            }
        }
        if (!parsed) usleep(1000 * 1000);
    }
    if (parsed) safe_elf_lookup(es,"il2cpp_method_get_flags",&g_method_flags);
    if (parsed) {
        auto bind_api = [&](const char *name, auto &fn) {
            uint64_t address = 0;
            if (safe_elf_lookup(es, name, &address)) memcpy(&fn, &address, sizeof fn);
        };
        bind_api("il2cpp_class_get_methods", g_binding_api.methods);
        bind_api("il2cpp_method_get_name", g_binding_api.name);
        bind_api("il2cpp_method_get_param_count", g_binding_api.argc);
        bind_api("il2cpp_method_get_param", g_binding_api.param);
        bind_api("il2cpp_method_get_return_type", g_binding_api.returns);
        bind_api("il2cpp_method_get_flags", g_binding_api.flags);
        bind_api("il2cpp_type_get_name", g_binding_api.type_name);
        bind_api("il2cpp_free", g_binding_api.release);
        bind_api("il2cpp_method_is_generic", g_binding_api.generic);
        bind_api("il2cpp_method_is_inflated", g_binding_api.inflated);
        ELOGI("BIND full-signature API ready=%d", g_binding_api.ready());
    }
    g_target_base = parsed ? base : 0; // Use the ELF-validated load bias, not a transient first alias.
    probe->il2cpp_base = base;
    int found = 0;
    char missing[256];
    size_t used = 0;
    missing[0] = 0;
    uint64_t sym_addr[24] = {};
    if (parsed) {
        ack->caps |= WSM_CAP_ELF_PARSE_OK;
        for (size_t i = 0; i < kSymbolCount; i++) {
            uint64_t a = 0;
            if (safe_elf_lookup(es, kSymbols[i], &a)) {
                found++;
                if (i < 24) sym_addr[i] = a;
            } else if (used < sizeof missing - 1) {
                used += static_cast<size_t>(snprintf(missing + used, sizeof missing - used,
                                                     "%s ", kSymbols[i]));
                if (used >= sizeof missing) used = sizeof missing - 1;
            }
        }
        if (found == static_cast<int>(kSymbolCount)) ack->caps |= WSM_CAP_SYMBOLS_ALL;
    } else {
        snprintf(missing, sizeof missing, "elf-parse-failed");
    }
    /* ---- G7/G8/G9 query-only phase: by-name resolve via il2cpp API (NO writes). ---- */
    uint64_t q_dom = 0, q_asm = 0, q_img = 0, q_cls = 0;
    uint64_t q_meth = 0, q_fld = 0, q_sval = 0;
    int q_foff = -1;
    void *w_mods = nullptr;   /* perpetual watcher state (read-only) */
    void *w_inst = nullptr;
    void *w_smdt = nullptr;
    void (*w_fsgv)(void *, void *) = nullptr;
    void (*w_fgv)(void *, void *, void *) = nullptr;
    uint64_t st_mods = 0, st_inst = 0; /* v30c: alamat nilai static utk baca-memori murni */
    uint64_t q_inst = 0;
    float q_smdt = 0;
    int q_smdt_has = -1, q_modsn = -1;
    if (sym_addr[0] && sym_addr[1] && sym_addr[2] && sym_addr[3]) {
        typedef void *(*fn_void)(void);
        typedef void *(*fn_one)(void *);
        typedef void *(*fn_dao)(void *, const char *);
        typedef void *(*fn_cfn)(void *, const char *, const char *);
        typedef void *(*fn_attach)(void *);
        fn_void dg = nullptr;
        fn_dao dao = nullptr;
        fn_one agi = nullptr;
        fn_cfn cfn = nullptr;
        fn_attach attach_fn = nullptr;
        memcpy(&dg, &sym_addr[0], sizeof dg);
        memcpy(&dao, &sym_addr[1], sizeof dao);
        memcpy(&agi, &sym_addr[2], sizeof agi);
        memcpy(&cfn, &sym_addr[3], sizeof cfn);
        if (sym_addr[9]) memcpy(&attach_fn, &sym_addr[9], sizeof attach_fn);
        (void)attach_fn; /* v30d: engine thread sengaja tidak attach */
        g_attach = reinterpret_cast<void *>(sym_addr[9]);
        usleep(12 * 1000 * 1000); /* il2cpp init grace period */
        const uint64_t dl_q = now_ms() + 45000;
        void *dom = nullptr;
        while (now_ms() < dl_q) {
            dom = dg();
            if (dom) break;
            usleep(1000 * 1000);
        }
        q_dom = reinterpret_cast<uint64_t>(dom);
        if (dom) {
            /* v30d: TIDAK attach engine thread — GC tidak boleh pernah menyentuh stack hybrid ini.
               Probe-calls (dao/cfn/cgf/fsgv) tetap berfungsi lintas jembatan houdini tanpa attach. */
            g_dom = q_dom;
            void *asm_cs = dao(dom, "Scripts");
            if (!asm_cs) asm_cs = dao(dom, "Scripts.dll");
            if (!asm_cs) asm_cs = dao(dom, "Assembly-CSharp");
            q_asm = reinterpret_cast<uint64_t>(asm_cs);
            if (asm_cs) {
                void *img = agi(asm_cs);
                q_img = reinterpret_cast<uint64_t>(img);
                if (img) {
                    void *cls = cfn(img, "", "GlobalTimeManager");
                    q_cls = reinterpret_cast<uint64_t>(cls);
                }
            }
        }
        if (q_cls) {
            typedef void *(*fn_cgm)(void *, const char *, int);
            typedef void *(*fn_cgf)(void *, const char *);
            typedef int (*fn_foff)(void *);
            typedef void (*fn_fsgv)(void *, void *);
            fn_cgm cgm = nullptr;
            fn_cgf cgf = nullptr;
            fn_foff foff_fn = nullptr;
            fn_fsgv fsgv = nullptr;
            if (sym_addr[4]) memcpy(&cgm, &sym_addr[4], sizeof cgm);
            if (sym_addr[6]) memcpy(&cgf, &sym_addr[6], sizeof cgf);
            if (sym_addr[7]) memcpy(&foff_fn, &sym_addr[7], sizeof foff_fn);
            if (sym_addr[10]) memcpy(&fsgv, &sym_addr[10], sizeof fsgv);
            void *klass = reinterpret_cast<void *>(q_cls);
            if (cgm) {
                void *mi = cgm(klass, "get_Instance", 0);
                q_meth = reinterpret_cast<uint64_t>(mi);
            }
            if (cgf) {
                void *f_mods = cgf(klass, "mods");
                void *f_inst = cgf(klass, "instance");
                w_mods = f_mods;
                w_inst = f_inst;
                w_fsgv = fsgv;
                (void)w_fsgv; /* v30c: beat watcher kini baca-memori murni */
                w_smdt = cgf(klass, "storedMaximumDeltaTime");
                if (sym_addr[12]) memcpy(&w_fgv, &sym_addr[12], sizeof w_fgv);
                ctl_klass = klass;
                ctl_f_inst = f_inst;
                ctl_f_mods = f_mods;
                ctl_fn_fsgv = reinterpret_cast<void *>(sym_addr[10]);
                ctl_fn_invoke = reinterpret_cast<void *>(sym_addr[11]);
                ctl_fn_strnew = reinterpret_cast<void *>(sym_addr[13]);
                g_cfn = reinterpret_cast<void *>(sym_addr[3]);
                g_cgm = reinterpret_cast<void *>(sym_addr[4]);
                g_cgf = reinterpret_cast<void *>(sym_addr[6]);
                g_finv = reinterpret_cast<void *>(sym_addr[11]);
                g_fgv2 = reinterpret_cast<void *>(sym_addr[12]);
                g_objcls = reinterpret_cast<void *>(sym_addr[14]);
                g_clsname = reinterpret_cast<void *>(sym_addr[15]);
                g_img = q_img;
                if (cgm) {
                    ctl_mi_instance = strict_binding(klass, {"get_Instance", "GlobalTimeManager", {nullptr, nullptr, nullptr}, 0, true});
                    ctl_mi_mod = strict_binding(klass, {"Mod", "System.Void", {"System.Single", "System.String", "System.Boolean"}, 3, false});
                    ctl_mi_unmod = strict_binding(klass, {"Unmod", "System.Void", {"System.String", nullptr, nullptr}, 1, false});
                    ctl_mi_setmax = cgm(klass, "SetMaximumDeltaTime", 1);
                    ctl_mi_resetmax = cgm(klass, "ResetMaximumDeltaTime", 0);
                    ctl_mi_clear = cgm(klass, "Clear", 0);
                }
                void *f = f_mods ? f_mods : f_inst;
                q_fld = reinterpret_cast<uint64_t>(f);
                if (f && foff_fn) q_foff = foff_fn(f);
                /* v30c: siapkan sumber baca-memori murni utk watcher (verifikasi vs API) */
                {
                    void *sf = nullptr;
                    memcpy(&sf, reinterpret_cast<const uint8_t *>(klass) + 0xA8, 8);
                    auto saddr = [&](void *fld) -> uint64_t {
                        if (!ptr_ok(sf) || !fld) return 0;
                        int32_t of = -1;
                        memcpy(&of, reinterpret_cast<const uint8_t *>(fld) + 0x18, 4);
                        if (of < 0 || of > 0x100000) return 0;
                        return reinterpret_cast<uint64_t>(sf) + static_cast<uint64_t>(of);
                    };
                    void *s_m = nullptr, *s_i = nullptr;
                    if (fsgv) {
                        if (f_mods) { GUARDED_BEGIN(); fsgv(f_mods, &s_m); GUARDED_END(); }
                        if (f_inst) { GUARDED_BEGIN(); fsgv(f_inst, &s_i); GUARDED_END(); }
                    }
                    uint64_t a_m = saddr(f_mods), a_i = saddr(f_inst);
                    uint64_t d_m = 0, d_i = 0;
                    if (a_m) { GUARDED_BEGIN(); memcpy(&d_m, reinterpret_cast<const void *>(a_m), 8); GUARDED_END(); }
                    if (a_i) { GUARDED_BEGIN(); memcpy(&d_i, reinterpret_cast<const void *>(a_i), 8); GUARDED_END(); }
                    st_mods = (a_m && d_m == reinterpret_cast<uint64_t>(s_m)) ? a_m : 0;
                    st_inst = (a_i && d_i == reinterpret_cast<uint64_t>(s_i)) ? a_i : 0;
                    ELOGI("WATCH direct-read verify: mods api=0x%llx direct=0x%llx -> %s | inst api=0x%llx direct=0x%llx -> %s",
                          static_cast<unsigned long long>(reinterpret_cast<uint64_t>(s_m)),
                          static_cast<unsigned long long>(d_m), st_mods ? "LOCK" : "skip",
                          static_cast<unsigned long long>(reinterpret_cast<uint64_t>(s_i)),
                          static_cast<unsigned long long>(d_i), st_inst ? "LOCK" : "skip");
                }
                if (fsgv && (f_mods || f_inst)) {
                    /* Quick guarded snapshot; the perpetual watcher covers LIVE values. */
                    void *v = nullptr;
                    if (f_inst) {
                        GUARDED_BEGIN();
                        fsgv(f_inst, &v);
                        GUARDED_END();
                    }
                    if (!v && f_mods) {
                        GUARDED_BEGIN();
                        fsgv(f_mods, &v);
                        GUARDED_END();
                    }
                    q_sval = reinterpret_cast<uint64_t>(v);
                }
            }
            /* G10 best-effort snapshot (normally still null this early; watcher covers LIVE). */
            if (fsgv && w_inst) {
                void *iv = nullptr;
                GUARDED_BEGIN();
                fsgv(w_inst, &iv);
                GUARDED_END();
                q_inst = reinterpret_cast<uint64_t>(iv);
                if (iv && w_fgv && w_smdt) {
                    uint8_t buf[16] = {};
                    GUARDED_BEGIN();
                    w_fgv(w_smdt, iv, buf);
                    GUARDED_END();
                    q_smdt_has = buf[0];
                    memcpy(&q_smdt, buf + 4, 4);
                }
            }
            if (fsgv && w_mods) {
                void *mv = nullptr;
                GUARDED_BEGIN();
                fsgv(w_mods, &mv);
                GUARDED_END();
                if (ptr_ok(mv)) {
                    int32_t sz = -1;
                    memcpy(&sz, reinterpret_cast<const uint8_t *>(mv) + 0x18, 4);
                    if (sz >= 0 && sz <= 1000000) q_modsn = sz;
                }
            }
        }
        if (q_dom) ack->caps |= WSM_CAP_QUERY_OK;
    } else {
        size_t ml = strlen(missing);
        if (ml + 12 < sizeof missing) snprintf(missing + ml, sizeof missing - ml, " +noquery");
    }
    probe->stage = WSM_PROBE_STAGE_REPORT;
    snprintf(probe->text, sizeof probe->text,
             "libil2cpp base=0x%llx elf sym %d/%zu miss:%s | dom=0x%llx asm=0x%llx img=0x%llx "
             "cls=0x%llx mth=0x%llx fld=0x%llx off=%d sval=0x%llx | inst=0x%llx smdt=%.4f(has=%d) modsN=%d",
             static_cast<unsigned long long>(base), found, kSymbolCount,
             missing[0] ? missing : " -", static_cast<unsigned long long>(q_dom),
             static_cast<unsigned long long>(q_asm), static_cast<unsigned long long>(q_img),
             static_cast<unsigned long long>(q_cls), static_cast<unsigned long long>(q_meth),
             static_cast<unsigned long long>(q_fld), q_foff,
             static_cast<unsigned long long>(q_sval), static_cast<unsigned long long>(q_inst),
             static_cast<double>(q_smdt), q_smdt_has, q_modsn);
    ack->caps |= WSM_CAP_PROBE_DONE;
    __sync_synchronize();
    probe->magic = WSM_PROBE_MAGIC;
    ELOGI("probe done: %s", probe->text);

    if (g_menu_dex_fd >= 0) {
        pthread_t mt;
        if (pthread_create(&mt, nullptr, menu_thread,
                           reinterpret_cast<void *>(static_cast<intptr_t>(g_menu_dex_fd))) == 0) {
            pthread_detach(mt);
        } else {
            ELOGI("MENU thread spawn failed");
        }
    }
    g_time_static = st_inst;
    pthread_t ct;
    if (pthread_create(&ct, nullptr, control_thread, nullptr) == 0) {
        pthread_detach(ct);
    } else {
        ELOGI("CTL thread spawn failed");
    }
    pthread_t ft;
    if (pthread_create(&ft, nullptr, feat_thread, nullptr) == 0) {
        pthread_detach(ft);
    } else {
        ELOGI("FEAT thread spawn failed");
    }
    // Experimental autohook and auxiliary producers are retired in the release path.
    /* v30d: detach tidak diperlukan — engine thread tidak pernah di-attach. */

    /* ---- perpetual watcher v2q (read-only, fault-guarded): every il2cpp touch
       runs under GUARDED_* so a transient internal fault skips the beat instead
       of killing the game (v2p lesson: an unguarded watcher call crashed the
       game's Thread-3 with a NULL deref inside il2cpp). ---- */
    if (st_mods || st_inst) {
        uint64_t last_m = ~0ull, last_i = ~0ull;
        int beats = 0;
        sig_atomic_t last_faults = g_guard_faults;
        for (;;) {
            BEAT_BEGIN();
            uint64_t cur_m = 0, cur_i = 0;
            if (st_mods) {
                GUARDED_BEGIN();
                memcpy(&cur_m, reinterpret_cast<const void *>(st_mods), 8);
                GUARDED_END();
            }
            if (st_inst) {
                GUARDED_BEGIN();
                memcpy(&cur_i, reinterpret_cast<const void *>(st_inst), 8);
                GUARDED_END();
            }
            // The feature owner refreshes ctl_instance; watcher only publishes diagnostics.
            if (g_guard_faults != last_faults) {
                ELOGI("WATCH guard: %d faulted call(s) skipped (total=%d, t=%ds)",
                      static_cast<int>(g_guard_faults - last_faults),
                      static_cast<int>(g_guard_faults), beats * 8);
                last_faults = g_guard_faults;
            }
            bool changed = (cur_m != last_m) || (cur_i != last_i);
            if (changed) {
                /* v30 fix: dua panggilan cross-arch di sini (field-getter smdt &
                   il2cpp_runtime_invoke get_Instance) pernah menjatuhkan proses via
                   crash internal houdini saat first-activation — dihapus total.
                   Sisa jalur = baca-memori murni (aman). */
                float smdt = 0;
                int smdt_has = -1, modsN = -1;
                (void)smdt;
                if (ptr_ok(reinterpret_cast<void *>(cur_m))) {
                    int32_t sz = -1;
                    memcpy(&sz, reinterpret_cast<const uint8_t *>(cur_m) + 0x18, 4);
                    if (sz >= 0 && sz <= 1000000) modsN = sz;
                }
                ELOGI("WATCH static: mods=0x%llx instance=0x%llx smdt=%.4f(has=%d) modsN=%d (t=%ds)",
                      static_cast<unsigned long long>(cur_m),
                      static_cast<unsigned long long>(cur_i), smdt, smdt_has, modsN, beats * 8);
                snprintf(probe->text, sizeof probe->text,
                         "LIVE base=0x%llx cls=0x%llx fld=0x%llx off=%d | mods=0x%llx instance=0x%llx "
                         "smdt=%.4f(has=%d) modsN=%d (t=%ds)",
                         static_cast<unsigned long long>(base),
                         static_cast<unsigned long long>(q_cls),
                         static_cast<unsigned long long>(q_fld), q_foff,
                         static_cast<unsigned long long>(cur_m),
                         static_cast<unsigned long long>(cur_i), smdt, smdt_has, modsN, beats * 8);
                last_m = cur_m;
                last_i = cur_i;
            } else if (beats > 0 && (beats % 30) == 0) {
                ELOGI("WATCH alive: mods=0x%llx instance=0x%llx",
                      static_cast<unsigned long long>(cur_m),
                      static_cast<unsigned long long>(cur_i));
            }
            beats++;
            BEAT_END();
            usleep(8000 * 1000); /* 8 s per beat */
        }
    }
    return nullptr;
}

} // namespace

extern "C" {

__attribute__((visibility("default"), used)) jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void) reserved;
    if (__atomic_test_and_set(&g_init_guard, __ATOMIC_SEQ_CST)) {
        ELOGI("duplicate JNI_OnLoad ignored");
        return JNI_VERSION_1_6;
    }
    g_vm = vm;
    ELOGI("JNI_OnLoad received (build=%s abi=%s)", WSM_BUILD_STAMP, WSM_ABI_STR);
    pthread_t t;
    if (pthread_create(&t, nullptr, engine_thread, nullptr) == 0) {
        pthread_detach(t);
    } else {
        ELOGI("engine thread spawn failed");
    }
    return JNI_VERSION_1_6;
}

} // extern "C"

// WSM modular v3 Loader (x86_64 + arm64, Zygisk). Evidence-first bootstrap.
//   pre   : allowlist (exact) -> read+validate+hash engine into private memory (no fds kept).
//   post  : create memfds NOW (post-specialize => immune to zygote fd sweep, A02), fill HELLO,
//           set env, spawn worker, then signal.
//   worker: matching-ABI dlopen + explicit JNI_OnLoad -> handshake -> evidence logs.
// P0/P1 closure: single load decision (no auto-fallback), no trampoline, payload verified,
// bounded reads, local frames, silent non-match. exemptFd intentionally NOT used (A02:
// ZN returned false on emulator; fds opened post-specialize don't need exemption).
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <jni.h>
#include <android/log.h>
#include "zygisk.hpp"
#include "wsm_protocol.h"
#include "wsm_bus.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WSM", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "WSM", __VA_ARGS__)

#if defined(__aarch64__)
#define WSM_ABI_STR "arm64-v8a"
#elif defined(__x86_64__)
#define WSM_ABI_STR "x86_64"
#else
#error unsupported abi
#endif

namespace {

const char *const kAllowlist[] = {"com.kakaogames.gdts", "com.wsm.fixture"};
const size_t kAllowCount = sizeof(kAllowlist) / sizeof(kAllowlist[0]);
const size_t kMaxPayload = 32u * 1024u * 1024u;
#if defined(__aarch64__)
const char *const kEngineRelPath = "engine/arm64-v8a.so";
constexpr uint16_t kExpectedMachine = 0xb7; /* EM_AARCH64 */
#elif defined(__x86_64__)
const char *const kEngineRelPath = "engine/x86_64.so";
constexpr uint16_t kExpectedMachine = 0x3e; /* EM_X86_64 */
#else
#error unsupported abi
#endif

struct Session {
    bool selected;
    void *arm_data;
    size_t arm_size;
    int arm_fd;
    char proc[64];
    int target_uid;
    int payload_fd;
    int dex_fd;
    int channel_fd;
    size_t payload_size;
    size_t dex_size;
    void *dex_data;
    uint64_t payload_hash;
    uint64_t nonce;
    void *channel_map;
    pthread_t worker;
    volatile int post_reached;
    uint8_t *engine_data; /* pre-read payload bytes; freed after post staging */
    size_t engine_size;
};

Session g_s{};
JavaVM *g_vm = nullptr;
zygisk::Api *g_api = nullptr;

uint64_t now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}

uint64_t fnv1a(const void *data, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

bool validate_engine_elf(const uint8_t *d, size_t n) {
    if (n < 64) return false;
    if (!(d[0] == 0x7f && d[1] == 'E' && d[2] == 'L' && d[3] == 'F')) return false;
    if (d[4] != 2 || d[5] != 1) return false; /* ELF64, little-endian */
    uint16_t machine = 0;
    memcpy(&machine, d + 18, sizeof machine);
    return machine == kExpectedMachine;
}

/* EINTR-safe, bounds-checked read of a whole file. */
void *read_file_bounded(int dirfd, const char *rel, size_t *out_size) {
    *out_size = 0;
    int fd = openat(dirfd, rel, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("openat(%s) errno=%d", rel, errno);
        return nullptr;
    }
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0 ||
        static_cast<uint64_t>(st.st_size) > kMaxPayload) {
        LOGE("fstat(%s) errno=%d size=%lld (max %zu)", rel, errno,
             static_cast<long long>(st.st_size), kMaxPayload);
        close(fd);
        return nullptr;
    }
    size_t size = static_cast<size_t>(st.st_size);
    void *buf = malloc(size);
    if (!buf) {
        LOGE("malloc(%zu) failed", size);
        close(fd);
        return nullptr;
    }
    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, static_cast<char *>(buf) + got, size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            LOGE("read(%s) fail at %zu errno=%d", rel, got, errno);
            free(buf);
            close(fd);
            return nullptr;
        }
        got += static_cast<size_t>(n);
    }
    close(fd);
    *out_size = size;
    return buf;
}

[[maybe_unused]] void log_jni_exception(JNIEnv *env, const char *where) {
    jthrowable ex = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!ex) return;
    jclass cls = env->GetObjectClass(ex);
    jmethodID ts = cls ? env->GetMethodID(cls, "toString", "()Ljava/lang/String;") : nullptr;
    jstring msg = ts ? reinterpret_cast<jstring>(env->CallObjectMethod(ex, ts)) : nullptr;
    if (msg) {
        const char *c = env->GetStringUTFChars(msg, nullptr);
        if (c) {
            LOGE("%s: %s", where, c);
            env->ReleaseStringUTFChars(msg, c);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void wait_for_handshake() {
    wsm_ack_t *ack = reinterpret_cast<wsm_ack_t *>(
        static_cast<uint8_t *>(g_s.channel_map) + WSM_OFF_ACK);
    const uint64_t deadline = now_ms() + 10000;
    bool ok = false;
    while (now_ms() < deadline) {
        if (__atomic_load_n(&ack->magic, __ATOMIC_ACQUIRE) == WSM_ACK_MAGIC && ack->protocol == WSM_PROTOCOL_VERSION &&
            ack->nonce_echo == g_s.nonce) {
            ok = true;
            break;
        }
        usleep(50 * 1000);
    }
    if (!ok) {
        LOGE("HANDSHAKE_TIMEOUT (10s) — engine did not ack");
        return;
    }
    if (ack->engine_uid != static_cast<uint32_t>(g_s.target_uid) ||
        ack->engine_pid != static_cast<uint32_t>(getpid()) ||
        strncmp(ack->build, WSM_BUILD_STAMP, sizeof ack->build) != 0 ||
        strncmp(ack->abi, WSM_ABI_STR, sizeof ack->abi) != 0) {
        LOGE("HANDSHAKE_IDENTITY_MISMATCH"); return;
    }
    LOGI("HANDSHAKE_OK engine_pid=%u engine_uid=%u state=%u caps=0x%x build=%s abi=%s",
         ack->engine_pid, ack->engine_uid, ack->state, ack->caps, ack->build, ack->abi);

    wsm_probe_t *probe = reinterpret_cast<wsm_probe_t *>(
        static_cast<uint8_t *>(g_s.channel_map) + WSM_OFF_PROBE);
    const uint64_t probe_deadline = now_ms() + 150000;
    while (now_ms() < probe_deadline) {
        if (probe->magic == WSM_PROBE_MAGIC) break;
        usleep(250 * 1000);
    }
    if (probe->magic == WSM_PROBE_MAGIC) {
        LOGI("PROBE stage=%u base=0x%llx :: %s", probe->stage,
             static_cast<unsigned long long>(probe->il2cpp_base), probe->text);
    } else {
        LOGI("PROBE pending (report not observed yet)");
    }
}

void *worker_main(void *) {
    const uint64_t wait_deadline = now_ms() + 30000;
    while (!__atomic_load_n(&g_s.post_reached, __ATOMIC_ACQUIRE)) {
        if (now_ms() > wait_deadline) {
            LOGE("worker: post not signaled in 30s — abort");
            return nullptr;
        }
        usleep(20 * 1000);
    }
    if (!g_vm) {
        LOGE("worker: no JavaVM");
        return nullptr;
    }
    JNIEnv *env = nullptr;
    bool attached = false;
    jint r = g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (r == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("worker: attach failed");
            return nullptr;
        }
        attached = true;
    } else if (r != JNI_OK || !env) {
        LOGE("worker: GetEnv=%d", static_cast<int>(r));
        return nullptr;
    }
    if (env->PushLocalFrame(16) != JNI_OK) {
        LOGE("worker: local frame failed");
        if (attached) g_vm->DetachCurrentThread();
        return nullptr;
    }

    char fdpath[64];
    snprintf(fdpath, sizeof fdpath, "/proc/self/fd/%d", g_s.payload_fd);

    /* Single gated load decision: native dlopen + explicit JNI_OnLoad (A01/A02/A13).
       Rationale: System.load() NPEs on attached threads (Reflection.getCallerClass()==null);
       dlopen from the staged fd is deterministic and ABI-correct (the native bridge handles
       foreign-arch loads inside bridged processes). */
    void *handle = dlopen(fdpath, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        LOGE("ENGINE_DLOPEN_FAIL %s err=%s", fdpath, dlerror());
    } else {
        typedef jint (*jni_onload_fn)(JavaVM *, void *);
        jni_onload_fn onload = reinterpret_cast<jni_onload_fn>(dlsym(handle, "JNI_OnLoad"));
        if (!onload) {
            LOGE("ENGINE_NO_JNI_ONLOAD %s", fdpath);
        } else {
            jint ver = onload(g_vm, nullptr);
            LOGI("ENGINE_DLOPEN ok %s handle=%p jni_ver=0x%x (payload %zu B fnv64=%016llx)",
                 fdpath, handle, static_cast<unsigned>(ver), g_s.payload_size,
                 static_cast<unsigned long long>(g_s.payload_hash));
            close(g_s.payload_fd); /* mapping persists; fd no longer needed (A07) */
            g_s.payload_fd = -1;
            wait_for_handshake();
        }
    }
    env->PopLocalFrame(nullptr);
    if (attached) g_vm->DetachCurrentThread();
    return nullptr;
}

class WsmLoaderV2 final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        g_api = api;
        env_ = env;
        g_vm = nullptr;
        if (env) env->GetJavaVM(&g_vm);
        /* Silent: this runs in every process; diagnostics only after allowlist match (A19). */
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        g_s = Session{};
        g_s.arm_fd = -1;
        g_s.payload_fd = -1;
        g_s.dex_fd = -1;
        g_s.channel_fd = -1;
        if (!env_ || !args || !args->nice_name) return;
        const char *name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!name) {
            if (env_->ExceptionCheck()) env_->ExceptionClear();
            return;
        }
        bool matched = false;
        for (size_t i = 0; i < kAllowCount; i++) {
            if (strcmp(name, kAllowlist[i]) == 0) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            env_->ReleaseStringUTFChars(args->nice_name, name);
            return; /* silent non-match (A05/A19) */
        }
        snprintf(g_s.proc, sizeof g_s.proc, "%s", name);
        env_->ReleaseStringUTFChars(args->nice_name, name);
        g_s.target_uid = args->uid;

        const int dirfd = g_api->getModuleDir(); /* borrowed fd; valid in pre (S4) */
        if (dirfd < 0) {
            LOGE("[%s] getModuleDir=%d", g_s.proc, dirfd);
            return;
        }
        size_t size = 0;
        void *data = read_file_bounded(dirfd, kEngineRelPath, &size);
        if (!data) return;
        if (!validate_engine_elf(static_cast<uint8_t *>(data), size)) {
            LOGE("[%s] engine ELF invalid (size=%zu)", g_s.proc, size);
            free(data);
            return;
        }
        g_s.payload_hash = fnv1a(data, size);
        g_s.payload_size = size;

        g_s.engine_data = static_cast<uint8_t *>(data); /* ownership kept until post staging */
        g_s.engine_size = size;
        g_s.arm_data = read_file_bounded(dirfd, WSM_PAYLOAD_REL, &g_s.arm_size);
        if (!g_s.arm_data || g_s.arm_size < 64 ||
            memcmp(g_s.arm_data, "\177ELF", 4) != 0 ||
            static_cast<uint8_t *>(g_s.arm_data)[4] != 2 ||
            static_cast<uint8_t *>(g_s.arm_data)[18] != 0xb7) {
            LOGE("ARM64 payload missing/invalid; refusing incomplete module");
            cleanup_stage();
            return;
        }
        g_s.selected = true;
        LOGI("[%s] pre-staged payload=%zu B fnv64=%016llx staging=deferred_to_post",
             g_s.proc, size, static_cast<unsigned long long>(g_s.payload_hash));

        /* G12: best-effort menu dex (in-memory classloading needs no disk artifact) */
        {
            size_t dsize = 0;
            void *dd = read_file_bounded(dirfd, "dex/wsm_menu.dex", &dsize);
            if (dd && dsize > 0 && dsize <= (4u * 1024u * 1024u)) {
                g_s.dex_data = dd;
                g_s.dex_size = dsize;
                LOGI("[%s] pre-staged dex=%zu B", g_s.proc, dsize);
            } else { free(dd); }
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!g_s.selected) return;
        /* A02: stage fds NOW — opened post-specialize they avoid the zygote fd sweep. */
        if (!stage_post()) {
            cleanup_stage();
            return;
        }
        __atomic_store_n(&g_s.post_reached, 1, __ATOMIC_RELEASE);
        LOGI("post staged+signaled pid=%d chan_fd=%d payload_fd=%d", static_cast<int>(getpid()),
             g_s.channel_fd, g_s.payload_fd);

    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override {}
    void postServerSpecialize(const zygisk::ServerSpecializeArgs *) override {}

private:
    static bool stage_post() {
        const size_t size = g_s.engine_size;
        if (!g_s.engine_data || size == 0) {
            LOGE("[%s] post: no pre-staged payload", g_s.proc);
            return false;
        }
        int pfd = static_cast<int>(syscall(__NR_memfd_create, "wsm_payload", 1));
        if (pfd < 0) {
            LOGE("[%s] payload memfd errno=%d", g_s.proc, errno);
            return false;
        }
        if (ftruncate(pfd, static_cast<off_t>(size)) != 0) {
            LOGE("[%s] payload ftruncate errno=%d", g_s.proc, errno);
            close(pfd);
            return false;
        }
        void *pmap = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd, 0);
        if (pmap == MAP_FAILED) {
            LOGE("[%s] payload mmap errno=%d", g_s.proc, errno);
            close(pfd);
            return false;
        }
        memcpy(pmap, g_s.engine_data, size);
        if (munmap(pmap, size) != 0) {
            LOGE("[%s] payload munmap errno=%d (continuing)", g_s.proc, errno);
        }
        free(g_s.engine_data);
        g_s.engine_data = nullptr;
        g_s.payload_fd = pfd;

        if (g_s.dex_data && g_s.dex_size > 0) {
            int d = static_cast<int>(syscall(__NR_memfd_create, "wsm_dex", 1));
            if (d >= 0 && ftruncate(d, static_cast<off_t>(g_s.dex_size)) == 0) {
                void *dmap = mmap(nullptr, g_s.dex_size, PROT_READ | PROT_WRITE, MAP_SHARED, d, 0);
                if (dmap != MAP_FAILED) {
                    memcpy(dmap, g_s.dex_data, g_s.dex_size);
                    munmap(dmap, g_s.dex_size);
                    g_s.dex_fd = d;
                    LOGI("[%s] dex staged %zu B fd=%d", g_s.proc, g_s.dex_size, d);
                } else {
                    close(d);
                }
            } else if (d >= 0) {
                close(d);
            }
            free(g_s.dex_data);
            g_s.dex_data = nullptr;
        }

        // Package the guest helper in a process-owned descriptor: no root copy to app libdir.
        g_s.arm_fd = static_cast<int>(syscall(__NR_memfd_create, "wsm_arm64", 1));
        if (g_s.arm_fd < 0 || ftruncate(g_s.arm_fd, static_cast<off_t>(g_s.arm_size))) return false;
        void *arm_map = mmap(nullptr, g_s.arm_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_s.arm_fd, 0);
        if (arm_map == MAP_FAILED) return false;
        memcpy(arm_map, g_s.arm_data, g_s.arm_size);
        munmap(arm_map, g_s.arm_size);
        free(g_s.arm_data); g_s.arm_data = nullptr;
        char arm_env[32]; snprintf(arm_env, sizeof arm_env, "%d", g_s.arm_fd);
        setenv("WSM_ARM64_FD", arm_env, 1);
        int cfd = static_cast<int>(syscall(__NR_memfd_create, "wsm_chan", 1));
        if (cfd < 0) {
            LOGE("[%s] channel memfd errno=%d", g_s.proc, errno);
            return false;
        }
        if (ftruncate(cfd, WSM_CHANNEL_MAP_SIZE) != 0) {
            LOGE("[%s] channel ftruncate errno=%d", g_s.proc, errno);
            close(cfd);
            return false;
        }
        void *cmap =
            mmap(nullptr, WSM_CHANNEL_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);
        if (cmap == MAP_FAILED) {
            LOGE("[%s] channel mmap errno=%d", g_s.proc, errno);
            close(cfd);
            return false;
        }
        g_s.channel_map = cmap;
        g_s.channel_fd = cfd;

        uint64_t nonce = 0;
        long gr = syscall(__NR_getrandom, &nonce, sizeof nonce, 0);
        if (gr != static_cast<long>(sizeof nonce) || nonce == 0) {
            nonce = (now_ms() << 32) ^ (static_cast<uint64_t>(getpid()) << 16);
        }
        g_s.nonce = nonce;

        wsm_hello_t *hello = static_cast<wsm_hello_t *>(g_s.channel_map);
        memset(g_s.channel_map, 0, WSM_CHANNEL_MAP_SIZE);
        hello->magic = WSM_HELLO_MAGIC;
        hello->protocol = WSM_PROTOCOL_VERSION;
        hello->loader_pid = static_cast<uint32_t>(getpid());
        hello->target_uid = static_cast<uint32_t>(g_s.target_uid);
        hello->nonce = g_s.nonce;
        snprintf(hello->build, sizeof hello->build, "%s", WSM_BUILD_STAMP);
        snprintf(hello->abi, sizeof hello->abi, "%s", WSM_ABI_STR);
        hello->stage = 1;

        char tmp[40];
        snprintf(tmp, sizeof tmp, "%d", g_s.channel_fd);
        setenv("WSM_CHANNEL_FD", tmp, 1);
        snprintf(tmp, sizeof tmp, "%u", WSM_PROTOCOL_VERSION);
        setenv("WSM_PROTOCOL", tmp, 1);
        snprintf(tmp, sizeof tmp, "%016llx", static_cast<unsigned long long>(g_s.nonce));
        setenv("WSM_NONCE", tmp, 1);
        if (g_s.dex_fd >= 0) {
            snprintf(tmp, sizeof tmp, "%d", g_s.dex_fd);
            setenv("WSM_DEX_FD", tmp, 1);
        }

        if (pthread_create(&g_s.worker, nullptr, worker_main, nullptr) != 0) {
            LOGE("[%s] worker spawn failed", g_s.proc);
            return false;
        }
        pthread_detach(g_s.worker);
        return true;
    }

    void cleanup_stage() {
        free(g_s.arm_data); g_s.arm_data = nullptr;
        if (g_s.arm_fd >= 0) { close(g_s.arm_fd); g_s.arm_fd = -1; }
        if (g_s.engine_data) {
            free(g_s.engine_data);
            g_s.engine_data = nullptr;
        }
        if (g_s.dex_data) {
            free(g_s.dex_data);
            g_s.dex_data = nullptr;
        }
        if (g_s.dex_fd >= 0) {
            close(g_s.dex_fd);
            g_s.dex_fd = -1;
        }
        if (g_s.payload_fd >= 0) {
            close(g_s.payload_fd);
            g_s.payload_fd = -1;
        }
        if (g_s.channel_fd >= 0) {
            close(g_s.channel_fd);
            g_s.channel_fd = -1;
        }
        if (g_s.channel_map) {
            if (munmap(g_s.channel_map, WSM_CHANNEL_MAP_SIZE) != 0) {
                LOGE("cleanup: channel munmap failed");
            }
            g_s.channel_map = nullptr;
        }
    }

    JNIEnv *env_ = nullptr;
};

} // namespace

REGISTER_ZYGISK_MODULE(WsmLoaderV2)

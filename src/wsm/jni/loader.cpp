// WSM Loader — zygisk bootstrap (x86_64 + arm64 builds)
// He built me. I am worm shadow. Flow (F0 POC):
//   pre  : read engine .so from module dir (zygote ctx) -> memfd -> exemptFd
//   post : x86_64 -> System.load(/proc/self/fd/N)  [ART bridges to ARM64 engine JNI_OnLoad]
//          arm64  -> direct dlopen(/proc/self/fd/N) + dlsym + call
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <jni.h>
#include <android/log.h>
#include "zygisk.hpp"

#define WSM_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WSM", __VA_ARGS__)
#define WSM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "WSM", __VA_ARGS__)

#if defined(__aarch64__)
#define WSM_ABI_STR "arm64-v8a"
#elif defined(__x86_64__)
#define WSM_ABI_STR "x86_64"
#elif defined(__arm__)
#define WSM_ABI_STR "armeabi-v7a"
#elif defined(__i386__)
#define WSM_ABI_STR "x86"
#else
#error unsupported abi
#endif

namespace {

constexpr const char *kTargetProcess = "com.kakaogames.gdts";
constexpr const char *kEngineRelPath = "engine/arm64-v8a.so";

bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

void *read_file_all(int dirfd, const char *relpath, size_t *out_size) {
    *out_size = 0;
    int fd = openat(dirfd, relpath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        WSM_LOGE("openat(%s) failed errno=%d", relpath, errno);
        return nullptr;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        WSM_LOGE("fstat(%s) failed errno=%d size=%lld", relpath, errno,
                 static_cast<long long>(st.st_size));
        close(fd);
        return nullptr;
    }
    size_t size = static_cast<size_t>(st.st_size);
    void *buf = malloc(size);
    if (!buf) {
        WSM_LOGE("malloc(%zu) failed", size);
        close(fd);
        return nullptr;
    }
    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, static_cast<char *>(buf) + got, size - got);
        if (n <= 0) {
            WSM_LOGE("read(%s) short got=%zu errno=%d", relpath, got, errno);
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

class WsmLoader final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
        if (env) env->GetJavaVM(&vm_);
        WSM_LOGI("onLoad pid=%d abi=%s api=%d vm=%p", static_cast<int>(getpid()),
                 WSM_ABI_STR, ZYGISK_API_VERSION, static_cast<void *>(vm_));
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        selected_ = false;
        if (!env_ || !args || !args->nice_name) return;
        const char *name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!name) {
            if (env_->ExceptionCheck()) env_->ExceptionClear();
            return;
        }
        selected_ = starts_with(name, kTargetProcess);
        const bool interesting = selected_ || strstr(name, "gdts") != nullptr;
        if (interesting) {
            WSM_LOGI("pre name=%s selected=%d pid=%d", name, selected_ ? 1 : 0,
                     static_cast<int>(getpid()));
        }
        env_->ReleaseStringUTFChars(args->nice_name, name);
        if (!selected_) return;

        int dirfd = api_->getModuleDir();
        WSM_LOGI("module dir fd=%d", dirfd);
        if (dirfd < 0) return;

        size_t size = 0;
        void *data = read_file_all(dirfd, kEngineRelPath, &size);
        if (!data) return;
        WSM_LOGI("engine staged: %zu bytes", size);

        int mfd = static_cast<int>(syscall(__NR_memfd_create, "wsm_engine", 1 /*MFD_CLOEXEC*/));
        if (mfd < 0) {
            WSM_LOGE("memfd_create errno=%d", errno);
            free(data);
            return;
        }
        if (ftruncate(mfd, static_cast<off_t>(size)) != 0) {
            WSM_LOGE("ftruncate errno=%d", errno);
            close(mfd);
            free(data);
            return;
        }
        void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
        if (map == MAP_FAILED) {
            WSM_LOGE("mmap errno=%d", errno);
            close(mfd);
            free(data);
            return;
        }
        memcpy(map, data, size);
        munmap(map, size);
        free(data);
        engine_fd_ = mfd;
        engine_size_ = size;
        api_->exemptFd(mfd);
        WSM_LOGI("engine memfd=%d size=%zu exempt=done", mfd, size);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!selected_ || engine_fd_ < 0) return;
        WSM_LOGI("post enter pid=%d uid=%u engine_fd=%d size=%zu",
                 static_cast<int>(getpid()), static_cast<unsigned>(getuid()),
                 engine_fd_, engine_size_);
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", engine_fd_);
#if defined(__x86_64__)
        // x86_64 process + ARM64 engine: let ART bridge it via System.load,
        // so the engine's JNI_OnLoad runs inside the translated ARM64 world.
        load_via_system(path);
        // Secondary path: direct dlopen + native bridge trampoline (best effort).
        load_via_native_bridge(path);
#else
        load_direct(path);
#endif
        WSM_LOGI("post done pid=%d", static_cast<int>(getpid()));
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override {}

private:
    void load_direct(const char *path) {
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char *e = dlerror();
            WSM_LOGE("dlopen(%s) failed: %s", path, e ? e : "?");
            return;
        }
        using engine_fn = long (*)(long long, long long);
        engine_fn fn = reinterpret_cast<engine_fn>(dlsym(h, "wsm_engine_main"));
        WSM_LOGI("dlopen ok handle=%p wsm_engine_main=%p", h,
                 reinterpret_cast<void *>(fn));
        if (fn) {
            long r = fn(0x57534D30LL, static_cast<long long>(getpid()));
            WSM_LOGI("engine_main returned 0x%lx", static_cast<unsigned long>(r));
        }
    }

    void load_via_system(const char *path) {
        JNIEnv *env = nullptr;
        if (!vm_) {
            WSM_LOGE("no JavaVM cached — skip System.load");
            return;
        }
        bool attached = false;
        jint res = vm_->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) {
                WSM_LOGE("AttachCurrentThread failed");
                return;
            }
            attached = true;
        } else if (res != JNI_OK || !env) {
            WSM_LOGE("GetEnv failed code=%d", static_cast<int>(res));
            return;
        }
        jclass sys = env->FindClass("java/lang/System");
        if (!sys) {
            WSM_LOGE("FindClass(System) failed");
            env->ExceptionClear();
        } else {
            jmethodID load = env->GetStaticMethodID(sys, "load", "(Ljava/lang/String;)V");
            if (!load) {
                WSM_LOGE("GetStaticMethodID(System.load) failed");
                env->ExceptionClear();
            } else {
                jstring jpath = env->NewStringUTF(path);
                env->CallStaticVoidMethod(sys, load, jpath);
                if (env->ExceptionCheck()) {
                    WSM_LOGE("System.load(%s) threw:", path);
                    dump_exception(env);
                } else {
                    WSM_LOGI("System.load(%s) OK — engine JNI_OnLoad should follow", path);
                }
            }
        }
        if (attached) vm_->DetachCurrentThread();
    }

    void load_via_native_bridge(const char *path) {
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        const char *derr = dlerror();
        WSM_LOGI("direct dlopen(%s)=%p err=%s", path, h, derr ? derr : "none");
        if (!h) return;
        void *nb = dlopen("libnativebridge.so", RTLD_NOW | RTLD_LOCAL);
        if (!nb) nb = dlopen("/system/lib64/libnativebridge.so", RTLD_NOW | RTLD_LOCAL);
        if (!nb) nb = dlopen("/apex/com.android.art/lib64/libnativebridge.so", RTLD_NOW | RTLD_LOCAL);
        WSM_LOGI("libnativebridge=%p", nb);
        if (!nb) return;
        using tramp_fn = void *(*)(void *, const char *, const char *, uint32_t);
        tramp_fn tramp = reinterpret_cast<tramp_fn>(
            dlsym(nb, "_ZN7android25NativeBridgeGetTrampolineEPvPKcS2_j"));
        if (!tramp) {
            tramp = reinterpret_cast<tramp_fn>(dlsym(nb, "NativeBridgeGetTrampoline"));
        }
        WSM_LOGI("NativeBridgeGetTrampoline=%p", reinterpret_cast<void *>(tramp));
        if (!tramp) return;
        using engine_fn = long (*)(long long, long long);
        const char *shorty = "vJJ";
        engine_fn fn = reinterpret_cast<engine_fn>(
            tramp(h, "wsm_engine_main", shorty, static_cast<uint32_t>(strlen(shorty))));
        WSM_LOGI("bridge trampoline(wsm_engine_main)=%p", reinterpret_cast<void *>(fn));
        if (!fn) return;
        long r = fn(0x57534D31LL, static_cast<long long>(getpid()));
        WSM_LOGI("engine_main(bridge) returned 0x%lx", static_cast<unsigned long>(r));
    }

    void dump_exception(JNIEnv *env) {
        jthrowable ex = env->ExceptionOccurred();
        env->ExceptionClear();
        if (!ex) return;
        jclass cls = env->GetObjectClass(ex);
        jmethodID ts = cls ? env->GetMethodID(cls, "toString", "()Ljava/lang/String;") : nullptr;
        jstring msg = ts ? reinterpret_cast<jstring>(env->CallObjectMethod(ex, ts)) : nullptr;
        if (msg) {
            const char *c = env->GetStringUTFChars(msg, nullptr);
            if (c) {
                WSM_LOGE("exception: %s", c);
                env->ReleaseStringUTFChars(msg, c);
            }
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    JavaVM *vm_ = nullptr;
    bool selected_ = false;
    int engine_fd_ = -1;
    size_t engine_size_ = 0;
};

} // namespace

REGISTER_ZYGISK_MODULE(WsmLoader)

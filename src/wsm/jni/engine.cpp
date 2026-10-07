// WSM Engine — the ARM64 world. Loaded into the x86_64 process via native bridge.
// Proof-of-life target (F0): when this runs, the bridge works and WSM owns both worlds.
#include <unistd.h>
#include <stdio.h>
#include <jni.h>
#include <android/log.h>

#define WSM_ELOG(...) __android_log_print(ANDROID_LOG_INFO, "WSMEngine", __VA_ARGS__)

__attribute__((unused)) static void write_proof(const char *how) {
    FILE *f = fopen("/data/local/tmp/wsm_engine_proof.txt", "w");
    if (f) {
        fprintf(f, "WSM ARM64 ENGINE ALIVE pid=%d via=%s\n",
                static_cast<int>(getpid()), how);
        fclose(f);
    } else {
        WSM_ELOG("proof file not writable (sandbox) — logcat is the proof");
    }
}

extern "C" {

#if defined(__aarch64__)

__attribute__((visibility("default"), used))
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    WSM_ELOG("ALIVE via JNI_OnLoad! arch=arm64-v8a pid=%d vm=%p (ART bridge works)",
             static_cast<int>(getpid()), static_cast<void *>(vm));
    write_proof("JNI_OnLoad");
    return JNI_VERSION_1_6;
}

__attribute__((visibility("default"), used))
long wsm_engine_main(long long a, long long b) {
    WSM_ELOG("ALIVE via trampoline! arch=arm64-v8a pid=%d a=0x%llx b=%lld",
             static_cast<int>(getpid()), static_cast<unsigned long long>(a), b);
    write_proof("trampoline");
    return 0x57534D4C; // "WSML"
}

#else

__attribute__((visibility("default"), used))
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)vm;
    (void)reserved;
    WSM_ELOG("stub JNI_OnLoad (non-arm build) — not shipped");
    return JNI_VERSION_1_6;
}

__attribute__((visibility("default"), used))
long wsm_engine_main(long long a, long long b) {
    (void)a;
    (void)b;
    WSM_ELOG("stub engine (non-arm build) — not shipped");
    return 0;
}

#endif

} // extern "C"

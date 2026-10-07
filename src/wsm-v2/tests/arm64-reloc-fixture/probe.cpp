#include <jni.h>
#include "../../payload/h64.cpp"
extern "C" JNIEXPORT jint JNICALL Java_com_wsm_relocprobe_Runner_test(JNIEnv *,jclass) {
#if defined(__aarch64__)
    return branch_selftest();
#else
    return -1000; // Never report x86 arithmetic as ARM64 instruction execution.
#endif
}

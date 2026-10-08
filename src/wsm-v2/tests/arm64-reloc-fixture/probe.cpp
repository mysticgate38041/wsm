#include <jni.h>
#include "../../payload/h64.cpp"
__attribute__((noinline)) static void main_fixture(void *p) {
    ++*static_cast<volatile int *>(p);
    asm volatile("nop\n nop\n nop\n nop" ::: "memory");
}
static void *probe_invoke(void *,void *,void **,void **exception){*exception=nullptr;return (void *)0x1234;}
static void *probe_unbox(void *p){return p;}
static uint32_t probe_pin(void *,bool){return 1;}
static void *probe_target(uint32_t){return (void *)0x1234;}
static void probe_release(uint32_t){}
static int main_dispatch_test(){
    uint64_t bus[160]{};auto *previous=g_bus;g_bus=bus;
    wsm::SweepApi api{probe_invoke,probe_unbox,probe_pin,probe_target,probe_release,
        (void *)1,(void *)1,(void *)1,(void *)1,(void *)1,(void *)1,(void *)1,(void *)1,(void *)1};
    api.probe_only=true;
    int calls=0;char name[16]{};pthread_getname_np(pthread_self(),name,sizeof name);
    int rc=install_main_dispatch((uintptr_t)&main_fixture,"libwsmrelocprobe.so");
    if(rc>0){
        bus[WSM_MAIN_API]=(uint64_t)(uintptr_t)&api;bus[WSM_MAIN_STAGE]=0x1234;
        bus[WSM_MAIN_STATE]=(uint64_t)wsm::SweepState::Pending;bus[WSM_MAIN_REQUEST]=bus[WSM_MAIN_ALLOWED]=1;
        pthread_setname_np(pthread_self(),"WSMTestWorker");main_fixture(&calls);
        if(calls!=1||bus[WSM_MAIN_ACK])rc=-201;
        else{pthread_setname_np(pthread_self(),"UnityMain");main_fixture(&calls);
            if(calls!=2||bus[WSM_MAIN_ACK]!=1||bus[WSM_MAIN_TID]!=(uint64_t)gettid()||bus[WSM_MAIN_STATE]!=(uint64_t)wsm::SweepState::Applied)rc=-202;}
        const int restore=restore_hook(WSM_HOOK_SLOTS-1,"libwsmrelocprobe.so");
        if(restore<0)rc=-203;
        main_fixture(&calls);if(calls!=3)rc=-204;
    }
    pthread_setname_np(pthread_self(),name);g_bus=previous;
    return rc>0?0:rc;
}
extern "C" JNIEXPORT jint JNICALL Java_com_wsm_relocprobe_Runner_test(JNIEnv *,jclass) {
#if defined(__aarch64__)
    int rc=branch_selftest();return rc?rc:main_dispatch_test();
#else
    return -1000; // Never report x86 arithmetic as ARM64 instruction execution.
#endif
}

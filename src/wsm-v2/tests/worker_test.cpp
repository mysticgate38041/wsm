#include "../jni/payload_worker.h"
#include "../jni/wsm_runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
struct Context { wsm::Runtime runtime; bool woke=false; };
static void *consume(void *v) {auto *c=static_cast<Context *>(v);c->woke=c->runtime.wait_for_work(2000);return nullptr;}
int main() {
    using wsm::WorkerSchedule;wsm::WorkerState s{false,false,false,false,0};
    assert(WorkerSchedule::timeout(100,s)==1000);s.foreground=true;assert(WorkerSchedule::timeout(100,s)==1000);
    s.active=true;s.feature_due=150;assert(WorkerSchedule::timeout(100,s)==50);
    s.feature_due=999;assert(WorkerSchedule::timeout(100,s)==250);s.main_pending=true;assert(WorkerSchedule::timeout(100,s)==25);
    s.main_pending=false;s.fault=true;assert(WorkerSchedule::timeout(100,s)==1000);
    Context c;pthread_t thread;assert(!pthread_create(&thread,nullptr,consume,&c));uint64_t id;
    assert(c.runtime.submit("panic",id));assert(!pthread_join(thread,nullptr));assert(c.woke);
    wsm::Command command;assert(c.runtime.pop(command));assert(command.id==id);
    c.runtime.complete(command,wsm::Outcome::Applied,"reset");c.runtime.shutdown();
    wsm::Runtime publication;const uint64_t before=publication.epoch();
    assert(publication.publish_for_epoch("{\"ready\":true,\"features\":{\"god\":true}}",before));
    uint64_t panic;assert(publication.submit("panic",panic));
    assert(!publication.publish_for_epoch("{\"ready\":true,\"features\":{\"god\":true}}",before));
    char snapshot[4096];publication.snapshot(snapshot,sizeof snapshot);
    assert(strstr(snapshot,"reset_pending")&&strstr(snapshot,"\"features\":{}")&&!strstr(snapshot,"god"));
    const uint64_t reset=publication.epoch();assert(publication.publish_for_epoch("{\"ready\":false,\"features\":{}}",reset));
    publication.invalidate();assert(!publication.publish_for_epoch("{\"ready\":true}",reset));
    puts("worker: bounded deadlines and producer notification passed");
}

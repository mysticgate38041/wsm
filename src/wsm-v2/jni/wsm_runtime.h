#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "futex_bus.h"
#include "runtime_snapshot.h"

namespace wsm {
constexpr size_t QueueSize = 32, HistorySize = 64, CommandSize = 192;
enum class Outcome { Accepted, Applied, Rejected, Stale, Fault };
inline const char *name(Outcome s) {
    switch (s) {
        case Outcome::Accepted: return "accepted";
        case Outcome::Applied: return "applied";
        case Outcome::Rejected: return "rejected";
        case Outcome::Stale: return "stale";
        case Outcome::Fault: return "fault";
    }
    return "fault";
}
struct Command { uint64_t id, epoch; char text[CommandSize]; };
struct Result { uint64_t id, epoch; Outcome state; char detail[512]; };
struct RuntimeMetrics {
    uint64_t submitted=0, rejected=0, completed=0, stale=0, duplicateCompletions=0;
    uint64_t dequeued=0, queueUs=0, maxQueueUs=0, completionUs=0, maxCompletionUs=0;
    uint64_t waits=0, wakeups=0, publications=0, rejectedPublications=0;
    size_t queueDepth=0, queueHighWater=0;
};
inline uint64_t runtime_now_us() {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return static_cast<uint64_t>(ts.tv_sec)*1000000 + static_cast<uint64_t>(ts.tv_nsec)/1000;
}
// One owner executes backend work. Producers only submit and query results.
// Queue/result/epoch share one gate; status bytes have an independent read lock.
// Game/JNI calls and JSON serialization are never performed under these locks.
class Runtime {
public:
    using Clock = uint64_t (*)();
private:
    class Guard {
        pthread_mutex_t *mutex_;
    public:
        explicit Guard(pthread_mutex_t &mutex):mutex_(&mutex) { pthread_mutex_lock(mutex_); }
        ~Guard() { pthread_mutex_unlock(mutex_); }
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
    };
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    Command queue_[QueueSize]{};
    Result results_[HistorySize]{};
    uint64_t enqueued_[HistorySize]{};
    size_t head_=0, count_=0;
    uint64_t next_=0, epoch_=1, observed_generation_=0;
    WorkEvent event_;
    RuntimeSnapshot snapshot_;
    RuntimeMetrics metrics_;
    Clock clock_;
    bool stopping_=false;
    void finish_locked(Result &result, Outcome state, const char *detail) {
        const uint64_t now=clock_(), start=enqueued_[result.id%HistorySize];
        const uint64_t elapsed=now>=start?now-start:0;
        result.state=state;
        snprintf(result.detail,sizeof result.detail,"%s",detail?detail:"");
        ++metrics_.completed;
        if (state==Outcome::Stale) ++metrics_.stale;
        metrics_.completionUs+=elapsed;
        if (elapsed>metrics_.maxCompletionUs) metrics_.maxCompletionUs=elapsed;
    }
    void invalidate_locked(const char *detail) {
        ++epoch_;
        for (auto &result:results_)
            if (result.id && result.state==Outcome::Accepted && result.epoch!=epoch_)
                finish_locked(result,Outcome::Stale,detail);
        head_=count_=0;
        snapshot_.invalidate(epoch_,stopping_?"stopped":"reset_pending");
        event_.notify_locked();
    }
public:
    explicit Runtime(Clock clock=runtime_now_us):clock_(clock?clock:runtime_now_us) {}
    // shutdown + join waiters before destruction.
    ~Runtime() { pthread_mutex_destroy(&mutex_); }
    Runtime(const Runtime &)=delete;
    Runtime &operator=(const Runtime &)=delete;
    bool submit(const char *text,uint64_t &id,uint64_t expected_epoch=0) {
        id=0;
        Guard guard(mutex_);
        if (!text || !*text || strnlen(text,CommandSize)>=CommandSize || stopping_ ||
            (expected_epoch && expected_epoch!=epoch_)) { ++metrics_.rejected; return false; }
        if (strcmp(text,"panic")==0) invalidate_locked("cancelled by panic");
        Result &result=results_[(next_+1)%HistorySize];
        // Never evict an outstanding completion to make room for newer history.
        if (count_==QueueSize || (result.id && result.state==Outcome::Accepted)) {
            ++metrics_.rejected; return false;
        }
        id=++next_;
        Command &command=queue_[(head_+count_++)%QueueSize];
        command.id=id;command.epoch=epoch_;
        snprintf(command.text,sizeof command.text,"%s",text);
        result.id=id;result.epoch=epoch_;result.state=Outcome::Accepted;
        snprintf(result.detail,sizeof result.detail,"queued");
        enqueued_[id%HistorySize]=clock_();
        ++metrics_.submitted;
        if (count_>metrics_.queueHighWater) metrics_.queueHighWater=count_;
        event_.notify_locked();return true;
    }
    bool pop(Command &command) {
        Guard guard(mutex_);
        if (!count_ || stopping_) return false;
        command=queue_[head_];head_=(head_+1)%QueueSize;--count_;
        const uint64_t now=clock_(),start=enqueued_[command.id%HistorySize];
        const uint64_t elapsed=now>=start?now-start:0;
        ++metrics_.dequeued;metrics_.queueUs+=elapsed;
        if (elapsed>metrics_.maxQueueUs) metrics_.maxQueueUs=elapsed;
        return true;
    }
    void complete(const Command &command,Outcome state,const char *detail) {
        Guard guard(mutex_);
        Result &result=results_[command.id%HistorySize];
        if (!command.id || state==Outcome::Accepted || result.id!=command.id ||
            result.epoch!=command.epoch || result.state!=Outcome::Accepted) {
            ++metrics_.duplicateCompletions;return;
        }
        finish_locked(result,command.epoch==epoch_?state:Outcome::Stale,
            command.epoch==epoch_?detail:"epoch invalidated before completion");
    }
    bool result(uint64_t id,Result &out) {
        Guard guard(mutex_);
        if (!id || results_[id%HistorySize].id!=id) return false;
        out=results_[id%HistorySize];return true;
    }
    uint64_t epoch() { Guard guard(mutex_);return epoch_; }
    uint64_t invalidate() {
        Guard guard(mutex_);invalidate_locked("scene/activity changed; submit again");return epoch_;
    }
    bool wait_for_work(int timeout_ms=-1) {
        Guard guard(mutex_);++metrics_.waits;
        bool found=!stopping_ && (count_ || observed_generation_!=event_.generation_locked());
        if (!found && !stopping_) found=event_.wait_locked(&mutex_,observed_generation_,timeout_ms);
        if (found) observed_generation_=event_.generation_locked();
        found=found&&!stopping_;
        if (found) ++metrics_.wakeups;
        return found;
    }
    void notify() { Guard guard(mutex_);event_.notify_locked(); }
    size_t pending() { Guard guard(mutex_);return count_; }
    bool stopping() { Guard guard(mutex_);return stopping_; }
    void shutdown() {
        Guard guard(mutex_);
        if (!stopping_) { stopping_=true;invalidate_locked("runtime shutdown"); }
    }
    void publish(const char *json) { Guard guard(mutex_);snapshot_.store(json); }
    bool publish_for_epoch(const char *json,uint64_t expected_epoch) {
        Guard guard(mutex_);
        if (expected_epoch!=epoch_ || stopping_) {
            ++metrics_.rejectedPublications;
            snapshot_.invalidate(epoch_,stopping_?"stopped":"reset_pending");return false;
        }
        const bool stored=snapshot_.store(json);
        if (stored) ++metrics_.publications;else ++metrics_.rejectedPublications;
        return stored;
    }
    void snapshot(char *out,size_t size) { snapshot_.copy(out,size); }
    uint64_t snapshot_revision() { return snapshot_.copy(nullptr,0); }
    RuntimeMetrics metrics() {
        Guard guard(mutex_);auto result=metrics_;result.queueDepth=count_;return result;
    }
    void telemetry(char *out,size_t size) {
        char status[4096];const uint64_t revision=snapshot_.copy(status,sizeof status);
        const RuntimeMetrics m=metrics();
        const size_t length=strlen(status);
        if (!length || status[length-1]!='}') { if(out&&size)snprintf(out,size,"{}");return; }
        status[length-1]=0;
        if (!out || !size) return;
        const int written=snprintf(out,size,
            "%s,\"revision\":%llu,\"runtime\":{\"submitted\":%llu,\"rejected\":%llu,\"completed\":%llu,"
            "\"queueDepth\":%zu,\"queueHighWater\":%zu,\"meanQueueUs\":%llu,\"maxQueueUs\":%llu,"
            "\"meanCompletionUs\":%llu,\"maxCompletionUs\":%llu,\"waits\":%llu,\"wakeups\":%llu,\"publications\":%llu}}",
            status,(unsigned long long)revision,(unsigned long long)m.submitted,
            (unsigned long long)m.rejected,(unsigned long long)m.completed,m.queueDepth,m.queueHighWater,
            (unsigned long long)(m.dequeued?m.queueUs/m.dequeued:0),(unsigned long long)m.maxQueueUs,
            (unsigned long long)(m.completed?m.completionUs/m.completed:0),(unsigned long long)m.maxCompletionUs,
            (unsigned long long)m.waits,(unsigned long long)m.wakeups,(unsigned long long)m.publications);
        if (written<0 || static_cast<size_t>(written)>=size)
            snprintf(out,size,"{\"state\":\"fault\",\"ready\":false,\"detail\":\"telemetry buffer too small\"}");
    }
};
inline bool finite_range(float v,float lo,float hi) { return v==v && v>=lo && v<=hi; }
}

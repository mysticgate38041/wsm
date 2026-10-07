#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
// The queue lock never covers game calls. One engine worker owns every mutation.
class Runtime {
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    Command queue_[QueueSize]{};
    Result results_[HistorySize]{};
    size_t head_ = 0, count_ = 0;
    uint64_t next_ = 0, epoch_ = 1;
    char snapshot_[4096] = "{\"state\":\"booting\",\"epoch\":1,\"features\":{}}";
public:
    Runtime() = default;
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    bool submit(const char *s, uint64_t &id, uint64_t expected_epoch = 0) {
        id = 0;
        if (!s || !*s || strlen(s) >= CommandSize) return false;
        pthread_mutex_lock(&mutex_);
        const bool panic = strcmp(s, "panic") == 0;
        if (expected_epoch && expected_epoch != epoch_) { pthread_mutex_unlock(&mutex_); return false; }
        if (panic) {
            ++epoch_; // Also reject delayed producers that were prepared before PANIC.
            // PANIC invalidates pending mutations and takes the next execution slot.
            for (size_t i = 0; i < count_; ++i) {
                Command &c = queue_[(head_ + i) % QueueSize];
                Result &r = results_[c.id % HistorySize];
                r.state = Outcome::Stale;
                snprintf(r.detail, sizeof r.detail, "cancelled by panic");
            }
            head_ = count_ = 0;
        }
        if (count_ == QueueSize) { pthread_mutex_unlock(&mutex_); return false; }
        id = ++next_;
        Command &c = queue_[(head_ + count_++) % QueueSize];
        c.id = id; c.epoch = epoch_; snprintf(c.text, sizeof c.text, "%s", s);
        Result &r = results_[id % HistorySize];
        r.id = id; r.epoch = epoch_; r.state = Outcome::Accepted;
        snprintf(r.detail, sizeof r.detail, "queued");
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    bool pop(Command &c) {
        pthread_mutex_lock(&mutex_);
        bool found = count_ != 0;
        if (found) { c = queue_[head_]; head_ = (head_ + 1) % QueueSize; --count_; }
        pthread_mutex_unlock(&mutex_);
        return found;
    }
    void complete(const Command &c, Outcome s, const char *detail) {
        pthread_mutex_lock(&mutex_);
        Result &r = results_[c.id % HistorySize];
        if (r.id == c.id) { r.state = s; snprintf(r.detail, sizeof r.detail, "%s", detail); }
        pthread_mutex_unlock(&mutex_);
    }
    bool result(uint64_t id, Result &r) {
        pthread_mutex_lock(&mutex_);
        bool found = id && results_[id % HistorySize].id == id;
        if (found) r = results_[id % HistorySize];
        pthread_mutex_unlock(&mutex_);
        return found;
    }
    uint64_t epoch() {
        pthread_mutex_lock(&mutex_); uint64_t e = epoch_; pthread_mutex_unlock(&mutex_); return e;
    }
    uint64_t invalidate() {
        pthread_mutex_lock(&mutex_); ++epoch_; pthread_mutex_unlock(&mutex_); return epoch();
    }
    void publish(const char *s) {
        pthread_mutex_lock(&mutex_); snprintf(snapshot_, sizeof snapshot_, "%s", s); pthread_mutex_unlock(&mutex_);
    }
    void snapshot(char *out, size_t n) {
        pthread_mutex_lock(&mutex_); snprintf(out, n, "%s", snapshot_); pthread_mutex_unlock(&mutex_);
    }
};
inline bool finite_range(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }
}

#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace wsm {
// Status readers never acquire the command queue mutex. Writers are serialized
// by Runtime's epoch gate; no caller may acquire that gate while holding this lock.
class RuntimeSnapshot {
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    char bytes_[4096] = "{\"state\":\"booting\",\"ready\":false,\"epoch\":1,\"features\":{}}";
    uint64_t revision_ = 0;
public:
    ~RuntimeSnapshot() { pthread_mutex_destroy(&mutex_); }
    RuntimeSnapshot() = default;
    RuntimeSnapshot(const RuntimeSnapshot &) = delete;
    RuntimeSnapshot &operator=(const RuntimeSnapshot &) = delete;
    bool store(const char *json) {
        if (!json || !*json || strnlen(json, sizeof bytes_) >= sizeof bytes_) return false;
        pthread_mutex_lock(&mutex_);
        if (strcmp(bytes_, json) != 0) {
            snprintf(bytes_, sizeof bytes_, "%s", json);
            ++revision_;
        }
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    void invalidate(uint64_t epoch, const char *state = "reset_pending") {
        char empty[160];
        snprintf(empty, sizeof empty, "{\"state\":\"%s\",\"ready\":false,\"epoch\":%llu,\"features\":{}}",
                 state, static_cast<unsigned long long>(epoch));
        store(empty);
    }
    uint64_t copy(char *out, size_t size) {
        pthread_mutex_lock(&mutex_);
        if (out && size) snprintf(out, size, "%s", bytes_);
        const uint64_t revision = revision_;
        pthread_mutex_unlock(&mutex_);
        return revision;
    }
};
}

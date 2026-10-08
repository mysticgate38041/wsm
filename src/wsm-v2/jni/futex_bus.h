#pragma once
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

namespace wsm {
// Generation notification only: commands stay in Runtime's bounded FIFO.
// A pthread condition is portable across both production ABIs and avoids
// futex-word layout assumptions. All methods require the queue mutex held.
class WorkEvent {
    pthread_cond_t condition_{};
    uint64_t generation_ = 0;
    clockid_t clock_ = CLOCK_REALTIME;
public:
    WorkEvent() {
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
#if defined(__linux__) || defined(__ANDROID__)
        if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0)
            clock_ = CLOCK_MONOTONIC;
#endif
        pthread_cond_init(&condition_, &attr);
        pthread_condattr_destroy(&attr);
    }
    ~WorkEvent() { pthread_cond_destroy(&condition_); }
    WorkEvent(const WorkEvent &) = delete;
    WorkEvent &operator=(const WorkEvent &) = delete;
    uint64_t generation_locked() const { return generation_; }
    void notify_locked() {
        ++generation_;
        pthread_cond_broadcast(&condition_);
    }
    bool wait_locked(pthread_mutex_t *mutex, uint64_t observed, int timeout_ms) {
        if (observed != generation_) return true;
        if (timeout_ms == 0) return false;
        timespec deadline{};
        if (timeout_ms > 0) {
            if (clock_gettime(clock_, &deadline) != 0) return false;
            deadline.tv_sec += timeout_ms / 1000;
            deadline.tv_nsec += static_cast<long>(timeout_ms % 1000) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L;
            }
        }
        while (observed == generation_) {
            const int rc = timeout_ms < 0 ? pthread_cond_wait(&condition_, mutex)
                : pthread_cond_timedwait(&condition_, mutex, &deadline);
            if (rc == ETIMEDOUT) return observed != generation_;
            if (rc != 0 && rc != EINTR) return false;
        }
        return true;
    }
};
}

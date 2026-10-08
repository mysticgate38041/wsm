#include "payload_worker.h"
namespace wsm {
int WorkerSchedule::timeout(uint64_t now, const WorkerState &s) {
    if (s.main_pending) return 25;
    if (!s.foreground || s.fault) return 1000;
    if (!s.active) return 1000;
    int wait=250;
    if (s.feature_due>now && s.feature_due-now < static_cast<uint64_t>(wait))
        wait=static_cast<int>(s.feature_due-now);
    return wait;
}
}

#pragma once
#include <stdint.h>
namespace wsm {
struct WorkerState { bool foreground, active, main_pending, fault; uint64_t feature_due; };
// Commands wake the Runtime condition variable immediately. These timeouts are
// bounded lifecycle/maintenance deadlines, never a claim of zero idle CPU.
class WorkerSchedule {
public:
    static int timeout(uint64_t now, const WorkerState &state);
};
}

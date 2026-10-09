#pragma once
#include <pthread.h>
#include <stdint.h>
namespace wsm {
enum class BootstrapPhase { Selected, InputsReady, Staged, WorkerReady, EngineLoaded, HandshakeReady, ProbeObserved, Failed };
inline const char *bootstrap_phase_name(BootstrapPhase p) {
    switch(p) {
        case BootstrapPhase::Selected:return "selected";
        case BootstrapPhase::InputsReady:return "inputs_ready";
        case BootstrapPhase::Staged:return "staged";
        case BootstrapPhase::WorkerReady:return "worker_ready";
        case BootstrapPhase::EngineLoaded:return "engine_loaded";
        case BootstrapPhase::HandshakeReady:return "handshake_ready";
        case BootstrapPhase::ProbeObserved:return "probe_observed";
        case BootstrapPhase::Failed:return "failed";
    }
    return "unknown";
}
struct BootstrapSnapshot {
    BootstrapPhase phase=BootstrapPhase::Selected;
    uint64_t started_ms=0, changed_ms=0, previous_phase_ms=0;
    int error=0;
};
// Small deterministic lifecycle recorder. No target pointers, allocations or JNI.
class BootstrapProgress {
    pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
    BootstrapSnapshot state_{};
public:
    ~BootstrapProgress(){pthread_mutex_destroy(&mutex_);}
    BootstrapProgress()=default;
    BootstrapProgress(const BootstrapProgress&)=delete;
    BootstrapProgress&operator=(const BootstrapProgress&)=delete;
    void begin(uint64_t now) {
        pthread_mutex_lock(&mutex_);state_={BootstrapPhase::Selected,now,now,0,0};pthread_mutex_unlock(&mutex_);
    }
    bool advance(BootstrapPhase next,uint64_t now) {
        pthread_mutex_lock(&mutex_);
        const bool valid=next!=BootstrapPhase::Failed&&state_.phase!=BootstrapPhase::Failed&&
            static_cast<int>(next)==static_cast<int>(state_.phase)+1&&now>=state_.changed_ms;
        if(valid){state_.previous_phase_ms=now-state_.changed_ms;state_.changed_ms=now;state_.phase=next;}
        pthread_mutex_unlock(&mutex_);return valid;
    }
    void fail(int error,uint64_t now) {
        pthread_mutex_lock(&mutex_);
        if(state_.phase!=BootstrapPhase::Failed){
            if(now>=state_.changed_ms){state_.previous_phase_ms=now-state_.changed_ms;state_.changed_ms=now;}
            state_.phase=BootstrapPhase::Failed;state_.error=error;
        }
        pthread_mutex_unlock(&mutex_);
    }
    BootstrapSnapshot snapshot() {
        pthread_mutex_lock(&mutex_);const auto state=state_;pthread_mutex_unlock(&mutex_);return state;
    }
};
}

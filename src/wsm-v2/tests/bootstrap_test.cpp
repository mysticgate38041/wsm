#include "../jni/bootstrap_progress.h"
#include <assert.h>
#include <stdio.h>
#include <thread>
int main(){
    wsm::BootstrapProgress trace;trace.begin(100);
    assert(!trace.advance(wsm::BootstrapPhase::EngineLoaded,110));
    assert(!trace.advance(wsm::BootstrapPhase::InputsReady,99));
    assert(trace.advance(wsm::BootstrapPhase::InputsReady,125));
    auto s=trace.snapshot();assert(s.started_ms==100&&s.changed_ms==125&&s.previous_phase_ms==25);
    assert(trace.advance(wsm::BootstrapPhase::Staged,130));
    std::thread worker([&]{assert(trace.advance(wsm::BootstrapPhase::WorkerReady,145));trace.fail(9,150);});
    worker.join();trace.fail(10,160);s=trace.snapshot();
    assert(s.phase==wsm::BootstrapPhase::Failed&&s.error==9&&s.changed_ms==150);
    assert(!trace.advance(wsm::BootstrapPhase::EngineLoaded,200));
    trace.begin(300);assert(trace.advance(wsm::BootstrapPhase::InputsReady,310));
    puts("PASS bootstrap: ordered phases, monotonic clock, handoff, first failure retained, reset");
}

#include "../jni/wsm_runtime.h"
#include <assert.h>
#include <atomic>
#include <chrono>
#include <thread>

static void basic_contract() {
    wsm::Runtime r;
    uint64_t id = 42; wsm::Command c{}; wsm::Result result{};
    assert(!r.submit(nullptr, id) && id == 0);
    assert(!r.submit("", id));
    char command[wsm::CommandSize + 1];
    memset(command, 'a', sizeof command); command[wsm::CommandSize - 1] = 0;
    assert(r.submit(command, id) && r.pop(c));
    assert(strlen(c.text) == 191 && c.id == id);
    r.complete(c, wsm::Outcome::Applied, "boundary");
    command[wsm::CommandSize - 1] = 'a'; command[wsm::CommandSize] = 0;
    assert(!r.submit(command, id));
    for (size_t i = 0; i < wsm::QueueSize; ++i) assert(r.submit("feat god 1", id));
    const uint64_t queued_first = id - wsm::QueueSize + 1;
    assert(r.pending() == wsm::QueueSize && !r.submit("feat hp 1", id));
    const uint64_t before_panic = r.epoch();
    assert(r.submit("panic", id));
    assert(r.pending() == 1 && r.epoch() == before_panic + 1);
    uint64_t delayed_id = 42;
    assert(!r.submit("feat god 1", delayed_id, before_panic) && delayed_id == 0);
    assert(r.result(queued_first, result) && result.state == wsm::Outcome::Stale);
    assert(r.pop(c) && strcmp(c.text, "panic") == 0);
    r.complete(c, wsm::Outcome::Applied, "restored");
    assert(r.result(id, result) && result.state == wsm::Outcome::Applied);
    r.complete(c, wsm::Outcome::Fault, "duplicate completion");
    assert(r.result(id, result) && result.state == wsm::Outcome::Applied);
    assert(!r.pop(c));
    const uint64_t old_epoch = r.epoch(); assert(r.invalidate() == old_epoch + 1);
    assert(!r.submit("feat hp 1", id, old_epoch));
    assert(r.submit("feat hp 1", id, r.epoch()) && r.pop(c));
    assert(c.epoch == old_epoch + 1);
    r.complete(c, wsm::Outcome::Rejected, "no scene");
    for (size_t i = 0; i < wsm::HistorySize + 1; ++i) {
        assert(r.submit("feat hp 0", id)); assert(r.pop(c));
        r.complete(c, wsm::Outcome::Rejected, "no scene");
    }
    assert(!r.result(1, result));
    r.publish("{\"state\":\"ready\"}"); char out[64]; r.snapshot(out, sizeof out);
    assert(strcmp(out, "{\"state\":\"ready\"}") == 0);
    assert(wsm::finite_range(2, 1, 5) && !wsm::finite_range(6, 1, 5));
}
static void stale_completion() {
    wsm::Runtime r; uint64_t id = 0, panic_id = 0;
    wsm::Command in_flight{}, panic{}; wsm::Result result{};
    assert(r.submit("speed 2", id) && r.pop(in_flight));
    r.invalidate();
    assert(r.result(id, result) && result.state == wsm::Outcome::Stale);
    r.complete(in_flight, wsm::Outcome::Applied, "late success");
    assert(r.result(id, result) && result.state == wsm::Outcome::Stale);
    assert(r.submit("speed 2", id) && r.pop(in_flight));
    assert(r.submit("panic", panic_id));
    r.complete(in_flight, wsm::Outcome::Applied, "late success after panic");
    assert(r.result(id, result) && result.state == wsm::Outcome::Stale);
    assert(r.pop(panic) && panic.id == panic_id);
    r.complete(panic, wsm::Outcome::Applied, "restored");
    assert(r.result(panic_id, result) && result.state == wsm::Outcome::Applied);
    assert(r.submit("loot 1", id) && r.pop(in_flight));
    wsm::Command forged = in_flight; ++forged.epoch;
    r.complete(forged, wsm::Outcome::Applied, "wrong command epoch");
    assert(r.result(id, result) && result.state == wsm::Outcome::Accepted);
}
static void event_contract() {
    wsm::Runtime r;
    assert(!r.wait_for_work(0));
    r.notify();
    assert(r.wait_for_work(0) && !r.wait_for_work(0));
    uint64_t id; wsm::Command c{};
    assert(r.submit("feat hp 1", id));
    assert(r.wait_for_work(0) && r.pending() == 1);
    assert(r.wait_for_work(0));
    assert(r.pop(c) && !r.wait_for_work(0));
    r.invalidate(); assert(r.wait_for_work(0) && !r.wait_for_work(0));
    auto before = std::chrono::steady_clock::now();
    assert(!r.wait_for_work(25));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - before).count();
    assert(elapsed >= 15 && elapsed < 1500);
    std::atomic<bool> started{false}, woken{false};
    std::thread waiter([&] { started = true; woken = r.wait_for_work(2000); });
    while (!started) std::this_thread::yield();
    r.notify();
    waiter.join(); assert(woken && !r.wait_for_work(0));
    wsm::Runtime stopping; std::atomic<int> entered{0}, stopped{0};
    std::thread waiters[2];
    for (auto &thread : waiters) thread = std::thread([&] {
        ++entered; assert(!stopping.wait_for_work(-1)); ++stopped;
    });
    while (entered != 2) std::this_thread::yield();
    stopping.shutdown(); for (auto &thread : waiters) thread.join();
    assert(stopped == 2 && stopping.stopping() && !stopping.wait_for_work(0));
    assert(!stopping.submit("panic", id) && id == 0);
    wsm::Runtime pending; wsm::Result result{};
    assert(pending.submit("loot 1", id)); pending.shutdown();
    assert(pending.pending() == 0 && !pending.pop(c));
    assert(pending.result(id, result) && result.state == wsm::Outcome::Stale);
}
static void multiproducer_fifo() {
    wsm::Runtime r; std::atomic<int> producers{0}, accepted{0}, consumed{0};
    std::thread consumer([&] {
        uint64_t previous = 0; wsm::Command c{};
        while (producers != 4 || consumed != 4000) {
            assert(r.wait_for_work(2000));
            while (r.pop(c)) {
                assert(c.id > previous); previous = c.id;
                assert(c.epoch == 1 && strcmp(c.text, "feat god 1") == 0);
                r.complete(c, wsm::Outcome::Applied, "done"); ++consumed;
            }
        }
    });
    std::thread writers[4];
    for (auto &writer : writers) writer = std::thread([&] {
        for (int i = 0; i < 1000; ++i) {
            uint64_t id;
            while (!r.submit("feat god 1", id)) std::this_thread::yield();
            ++accepted;
        }
        ++producers; r.notify();
    });
    for (auto &writer : writers) writer.join(); consumer.join();
    assert(accepted == 4000 && consumed == 4000 && r.pending() == 0);
}
static void wait_boundary_stress() {
    wsm::Runtime r; std::atomic<int> consumed{0};
    std::thread consumer([&] {
        for (int i = 0; i < 2000; ++i) {
            assert(r.wait_for_work(2000)); wsm::Command c{};
            assert(r.pop(c) && c.id == static_cast<uint64_t>(i + 1));
            r.complete(c, wsm::Outcome::Applied, "boundary"); ++consumed;
        }
    });
    for (int i = 0; i < 2000; ++i) {
        uint64_t id; assert(r.submit("feat hp 1", id));
        while (consumed != i + 1) std::this_thread::yield();
    }
    consumer.join(); assert(r.pending() == 0);
}
int main() {
    basic_contract(); stale_completion(); event_contract();
    multiproducer_fifo(); wait_boundary_stress();
    puts("PASS runtime: FIFO/capacity/191-char/history64, PANIC priority and stale completion, lifecycle/notify-before-wait/timeout/shutdown, 4 producers / 4000 commands, 2000 wait boundaries");
}

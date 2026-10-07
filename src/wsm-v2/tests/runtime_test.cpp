#include "../jni/wsm_runtime.h"
#include <assert.h>
#include <atomic>
#include <unistd.h>

static wsm::Runtime concurrent;
static std::atomic<int> producers{0}, accepted{0}, consumed{0};
static void *produce(void *) {
    for (int i = 0; i < 1000; ++i) {
        uint64_t id;
        while (!concurrent.submit("feat god 1", id)) usleep(10);
        ++accepted;
    }
    ++producers;
    return nullptr;
}
static void *consume(void *) {
    wsm::Command c{};
    uint64_t previous = 0;
    while (producers < 4 || consumed < 4000) {
        if (!concurrent.pop(c)) { usleep(10); continue; }
        assert(c.id > previous); previous = c.id;
        concurrent.complete(c, wsm::Outcome::Applied, "done");
        ++consumed;
    }
    return nullptr;
}
int main() {
    wsm::Runtime r;
    uint64_t id = 0; wsm::Command c{}; wsm::Result result{};
    assert(!r.submit("", id));
    char long_command[256]; memset(long_command, 'a', sizeof long_command); long_command[255] = 0;
    assert(!r.submit(long_command, id));
    for (size_t i = 0; i < wsm::QueueSize; ++i) assert(r.submit("feat god 1", id));
    assert(!r.submit("feat hp 1", id));
    uint64_t before_panic = r.epoch();
    assert(r.submit("panic", id));
    assert(r.epoch() == before_panic + 1);
    uint64_t delayed_id = 42;
    assert(!r.submit("feat god 1", delayed_id, before_panic) && delayed_id == 0);
    assert(r.result(1, result) && result.state == wsm::Outcome::Stale);
    assert(r.pop(c) && strcmp(c.text, "panic") == 0);
    r.complete(c, wsm::Outcome::Applied, "restored");
    assert(r.result(id, result) && result.state == wsm::Outcome::Applied);
    assert(!r.pop(c));
    uint64_t old_epoch = r.epoch(); assert(r.invalidate() == old_epoch + 1);
    assert(!r.submit("feat hp 1", id, old_epoch));
    assert(r.submit("feat hp 1", id, r.epoch()) && r.pop(c));
    assert(r.submit("feat mana 1", id) && r.pop(c) && c.epoch == old_epoch + 1);
    for (size_t i = 0; i < wsm::HistorySize + 1; ++i) { assert(r.submit("feat hp 0", id)); assert(r.pop(c)); r.complete(c, wsm::Outcome::Rejected, "no scene"); }
    assert(!r.result(1, result));
    r.publish("{\"state\":\"ready\"}"); char out[64]; r.snapshot(out, sizeof out);
    assert(strcmp(out, "{\"state\":\"ready\"}") == 0);
    assert(wsm::finite_range(2,1,5) && !wsm::finite_range(6,1,5));
    pthread_t workers[5];
    assert(pthread_create(&workers[4], nullptr, consume, nullptr) == 0);
    for (int i = 0; i < 4; ++i) assert(pthread_create(&workers[i], nullptr, produce, nullptr) == 0);
    for (auto &worker : workers) assert(pthread_join(worker, nullptr) == 0);
    assert(accepted == 4000 && consumed == 4000);
    puts("PASS runtime: FIFO, capacity, panic priority/cancellation, epochs, history, snapshots, 4 concurrent producers / 4000 requests");
}

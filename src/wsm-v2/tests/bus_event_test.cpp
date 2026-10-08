#include "../jni/shared_bus_event.h"
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
struct Context { alignas(8) uint64_t bus[WSM_BUS_WORDS]{}; pthread_barrier_t barrier; bool received=false; };
static void *consume(void *v) {
    auto &c=*static_cast<Context *>(v);const auto generation=wsm::bus_generation(c.bus);
    pthread_barrier_wait(&c.barrier);
    if(!__atomic_load_n(&c.bus[0],__ATOMIC_ACQUIRE)) wsm::wait_bus(c.bus,generation,1000);
    c.received=__atomic_load_n(&c.bus[0],__ATOMIC_ACQUIRE)==42;
    __atomic_store_n(&c.bus[9],7ULL,__ATOMIC_RELEASE);wsm::notify_bus(c.bus);return nullptr;
}
int main() {
    Context c;auto generation=wsm::bus_generation(c.bus);wsm::notify_bus(c.bus);
    errno=0;wsm::wait_bus(c.bus,generation,1000);assert(errno==EAGAIN); // notify-before-wait
    generation=wsm::bus_generation(c.bus);errno=0;wsm::wait_bus(c.bus,generation,1);assert(errno==ETIMEDOUT);
    assert(!pthread_barrier_init(&c.barrier,nullptr,2));pthread_t thread;assert(!pthread_create(&thread,nullptr,consume,&c));
    pthread_barrier_wait(&c.barrier);wsm::send_bus_command(c.bus,42);assert(!pthread_join(thread,nullptr));
    assert(c.received&&__atomic_load_n(&c.bus[9],__ATOMIC_ACQUIRE)==7);assert(!pthread_barrier_destroy(&c.barrier));
    assert(c.bus[WSM_BUS_IDENTITY]==0&&c.bus[WSM_BUS_EVENT+1]==0); // reserved identity/mailbox words untouched
    puts("bus event: notification-before-wait, timeout and producer/ack passed");
}

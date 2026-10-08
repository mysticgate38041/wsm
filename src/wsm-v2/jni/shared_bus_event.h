#pragma once
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include "wsm_bus.h"
// Standard libc/kernel notification for the owned in-process mailbox. This does
// not inspect, intercept, suspend, or hide any other thread.
namespace wsm {
inline uint32_t *bus_event_word(volatile uint64_t *bus) {
    return reinterpret_cast<uint32_t *>(const_cast<uint64_t *>(bus+WSM_BUS_EVENT));
}
inline uint32_t bus_generation(volatile uint64_t *bus) {
    return __atomic_load_n(bus_event_word(bus),__ATOMIC_ACQUIRE);
}
inline void notify_bus(volatile uint64_t *bus) {
    __atomic_add_fetch(bus_event_word(bus),1u,__ATOMIC_RELEASE);
    syscall(SYS_futex,bus_event_word(bus),FUTEX_WAKE,INT32_MAX,nullptr,nullptr,0);
}
inline void wait_bus(volatile uint64_t *bus,uint32_t observed,int timeout_ms) {
    timespec timeout{timeout_ms/1000,(timeout_ms%1000)*1000000L};
    syscall(SYS_futex,bus_event_word(bus),FUTEX_WAIT,observed,&timeout,nullptr,0);
}
inline void send_bus_command(volatile uint64_t *bus,uint64_t command) {
    __atomic_store_n(&bus[0],command,__ATOMIC_RELEASE);notify_bus(bus);
}
}

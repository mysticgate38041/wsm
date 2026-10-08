#pragma once
#include <stddef.h>
#include <stdint.h>
namespace wsm {
using Reachable = bool (*)(uintptr_t,uintptr_t);
using NearAllocator = void *(*)(uintptr_t);
using SealPage = bool (*)(void *,size_t);
// Caller serializes reserve/seal. Published pages are retained for process life:
// restoration alone does not prove that every executing reader has quiesced.
class TrampolinePool {
    void *arena_; size_t stride_, capacity_, page_size_, retained_=0;
    void *pages_[512]{}; bool sealed_[512]{};
    NearAllocator allocate_; SealPage seal_;
public:
    TrampolinePool(void *arena, size_t stride, size_t capacity, size_t page_size,
                   NearAllocator allocate, SealPage seal);
    void *reserve(uintptr_t target, Reachable reachable);
    bool seal(void *page, size_t bytes);
    size_t retained() const { return retained_; }
    size_t committed_bytes() const { return retained_*page_size_; }
};
}

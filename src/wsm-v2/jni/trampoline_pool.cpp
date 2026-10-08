#include "trampoline_pool.h"
#include <limits.h>
namespace wsm {
TrampolinePool::TrampolinePool(void *arena,size_t stride,size_t capacity,size_t page_size,
                             NearAllocator allocate,SealPage seal)
    : arena_(arena),stride_(stride),capacity_(capacity),page_size_(page_size),allocate_(allocate),seal_(seal) {}
void *TrampolinePool::reserve(uintptr_t target,Reachable reachable) {
    if (!target || !reachable || !seal_ || !page_size_ || (page_size_&(page_size_-1)) ||
        page_size_>stride_ || stride_%page_size_ || !capacity_ || capacity_>512 || retained_>=capacity_) return nullptr;
    const uintptr_t base=reinterpret_cast<uintptr_t>(arena_);
    if (!base || base%page_size_ || retained_>(UINTPTR_MAX-base)/stride_) return nullptr;
    void *page=reinterpret_cast<void *>(base+retained_*stride_);
    if (!reachable(target,reinterpret_cast<uintptr_t>(page))) page=allocate_ ? allocate_(target) : nullptr;
    if (!page || reinterpret_cast<uintptr_t>(page)==UINTPTR_MAX || reinterpret_cast<uintptr_t>(page)%page_size_ ||
        !reachable(target,reinterpret_cast<uintptr_t>(page))) return nullptr;
    pages_[retained_++]=page;
    return page;
}
bool TrampolinePool::seal(void *page,size_t bytes) {
    if (!page || !bytes || bytes>page_size_ || !seal_) return false;
    for (size_t i=0;i<retained_;++i) if (pages_[i]==page) {
        if (sealed_[i]) return false;
        __builtin___clear_cache(static_cast<char *>(page),static_cast<char *>(page)+bytes);
        if (!seal_(page,page_size_)) return false;
        sealed_[i]=true;return true;
    }
    return false;
}
}

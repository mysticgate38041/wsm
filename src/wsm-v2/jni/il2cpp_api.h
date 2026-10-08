#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wsm {
// Metadata is opaque. A MethodInfo address is never a native entry point.
struct MethodInfoRef { void *value = nullptr; };
struct NativeMethodPointer { uintptr_t address = 0; };

// Function signatures deliberately preserve the existing binding facade ABI.
// type_name returns runtime-owned allocated text; release must free that text.
struct BindingApi {
    void *(*methods)(void *, void **) = nullptr;
    const char *(*name)(void *) = nullptr;
    uint32_t (*argc)(void *) = nullptr;
    void *(*param)(void *, uint32_t) = nullptr;
    void *(*returns)(void *) = nullptr;
    uint32_t (*flags)(void *, uint32_t *) = nullptr;
    char *(*type_name)(void *) = nullptr;
    void (*release)(void *) = nullptr;
    bool (*generic)(void *) = nullptr;
    bool (*inflated)(void *) = nullptr;
    bool ready() const {
        return methods && name && argc && param && returns && flags &&
            type_name && release && generic && inflated;
    }
};

// The embedding runtime supplies an ABI-qualified accessor and executable-range
// validator. The resolver never guesses MethodInfo field offsets or dereferences
// its contents. A native entry is optional for metadata-only invocation.
struct NativePointerApi {
    uintptr_t (*method_pointer)(MethodInfoRef, void *) = nullptr;
    bool (*executable_pointer)(NativeMethodPointer, void *) = nullptr;
    void *context = nullptr;
    bool ready() const { return method_pointer && executable_pointer; }
};
}

#pragma once
#include <stdint.h>
#include <string.h>

namespace wsm {
struct BindingApi {
    void *(*methods)(void *, void **);
    const char *(*name)(void *);
    uint32_t (*argc)(void *);
    void *(*param)(void *, uint32_t);
    void *(*returns)(void *);
    uint32_t (*flags)(void *, uint32_t *);
    char *(*type_name)(void *);
    void (*release)(void *);
    bool (*generic)(void *);
    bool (*inflated)(void *);
    bool ready() const {
        return methods && name && argc && param && returns && flags && type_name && release && generic && inflated;
    }
};
struct Binding {
    const char *name, *returns;
    const char *params[3];
    uint32_t argc;
    bool is_static;
};
enum class BindingState { Found, Missing, Ambiguous, Unavailable, Truncated };
struct BindingResult { void *method; BindingState state; uint32_t matches; };
inline bool type_matches(const BindingApi &api, void *type, const char *expected) {
    if (!type || !expected) return false;
    char *name = api.type_name(type);
    bool same = name && strcmp(name, expected) == 0;
    if (name) api.release(name);
    return same;
}
inline BindingResult resolve_binding(const BindingApi &api, void *klass, const Binding &wanted) {
    BindingResult result{nullptr, BindingState::Unavailable, 0};
    if (!api.ready() || !klass || wanted.argc > 3) return result;
    void *iterator = nullptr;
    for (uint32_t count = 0; count < 4096; ++count) {
        void *method = api.methods(klass, &iterator);
        if (!method) {
            result.state = result.matches == 1 ? BindingState::Found : result.matches ? BindingState::Ambiguous : BindingState::Missing;
            if (result.matches != 1) result.method = nullptr;
            return result;
        }
        const char *name = api.name(method);
        uint32_t implementation = 0;
        if (!name || strcmp(name, wanted.name) || api.argc(method) != wanted.argc ||
            bool(api.flags(method, &implementation) & 0x10u) != wanted.is_static || api.generic(method) || api.inflated(method)) continue;
        bool matches = type_matches(api, api.returns(method), wanted.returns);
        for (uint32_t i = 0; matches && i < wanted.argc; ++i) matches = type_matches(api, api.param(method, i), wanted.params[i]);
        if (matches) { ++result.matches; result.method = method; }
    }
    // An incomplete enumeration cannot establish uniqueness.
    return {nullptr, BindingState::Truncated, result.matches};
}
inline bool identity_matches(const char *package, const char *version, uint64_t code) {
    return package && version && strcmp(package, "com.kakaogames.gdts") == 0 &&
        strcmp(version, "3.54.0") == 0 && code == 423;
}
}

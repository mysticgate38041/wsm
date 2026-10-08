#include "il2cpp_resolver.h"
#include <string.h>

namespace wsm {
namespace {
bool present(const char *text) { return text && text[0] != '\0'; }
}
bool valid_binding(const Binding &wanted) {
    if (!present(wanted.name) || !present(wanted.returns) || wanted.argc > kMaxBindingParameters) return false;
    for (uint32_t i = 0; i < wanted.argc; ++i) if (!present(wanted.params[i])) return false;
    return true;
}
bool type_matches(const BindingApi &api, void *type, const char *expected) {
    if (!type || !present(expected) || !api.type_name || !api.release) return false;
    char *name = api.type_name(type);
    const bool same = name && strcmp(name, expected) == 0;
    if (name) api.release(name);
    return same;
}
BindingResult resolve_binding(const BindingApi &api, void *klass, const Binding &wanted) {
    if (!valid_binding(wanted)) return {nullptr, BindingState::InvalidContract, 0};
    BindingResult result{nullptr, BindingState::Unavailable, 0};
    if (!api.ready() || !klass) return result;
    void *iterator = nullptr;
    for (uint32_t count = 0; count < kMaxMethodEnumeration; ++count) {
        void *method = api.methods(klass, &iterator);
        if (!method) {
            result.state = result.matches == 1 ? BindingState::Found :
                result.matches ? BindingState::Ambiguous : BindingState::Missing;
            if (result.matches != 1) result.method = nullptr;
            return result;
        }
        const char *name = api.name(method);
        uint32_t implementation = 0;
        if (!name || strcmp(name, wanted.name) || api.argc(method) != wanted.argc ||
            bool(api.flags(method, &implementation) & 0x10u) != wanted.is_static ||
            api.generic(method) || api.inflated(method)) continue;
        bool matches = type_matches(api, api.returns(method), wanted.returns);
        for (uint32_t i = 0; matches && i < wanted.argc; ++i)
            matches = type_matches(api, api.param(method, i), wanted.params[i]);
        if (matches) { ++result.matches; result.method = method; }
    }
    // No uniqueness claim is possible when enumeration did not terminate.
    return {nullptr, BindingState::Truncated, result.matches};
}
const char *binding_state_name(BindingState state) {
    switch (state) {
        case BindingState::Found: return "found";
        case BindingState::Missing: return "missing";
        case BindingState::Ambiguous: return "ambiguous";
        case BindingState::Unavailable: return "unavailable";
        case BindingState::Truncated: return "truncated";
        case BindingState::InvalidContract: return "invalid-contract";
    }
    return "unknown";
}
bool valid_identity(const IdentityContract &identity) {
    return present(identity.package) && present(identity.version) && identity.code != 0;
}
bool identity_matches(const IdentityContract &observed, const IdentityContract &expected) {
    return valid_identity(observed) && valid_identity(expected) && observed.code == expected.code &&
        strcmp(observed.package, expected.package) == 0 && strcmp(observed.version, expected.version) == 0;
}
IdentityContract supported_identity() { return {"com.kakaogames.gdts", "3.54.0", 423}; }
bool identity_matches(const char *package, const char *version, uint64_t code) {
    return identity_matches({package, version, code}, supported_identity());
}
}

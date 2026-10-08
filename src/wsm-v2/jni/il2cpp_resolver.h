#pragma once
#include "il2cpp_api.h"

namespace wsm {
constexpr uint32_t kMaxBindingParameters = 3;
constexpr uint32_t kMaxMethodEnumeration = 4096;
struct Binding {
    const char *name, *returns;
    const char *params[kMaxBindingParameters];
    uint32_t argc;
    bool is_static;
};
enum class BindingState { Found, Missing, Ambiguous, Unavailable, Truncated, InvalidContract };
// method remains an opaque MethodInfo for compatibility, never executable code.
struct BindingResult { void *method; BindingState state; uint32_t matches; };
bool valid_binding(const Binding &wanted);
bool type_matches(const BindingApi &api, void *type, const char *expected);
BindingResult resolve_binding(const BindingApi &api, void *klass, const Binding &wanted);
const char *binding_state_name(BindingState state);

struct IdentityContract { const char *package; const char *version; uint64_t code; };
bool valid_identity(const IdentityContract &identity);
bool identity_matches(const IdentityContract &observed, const IdentityContract &expected);
IdentityContract supported_identity();
// Preserve the RC3 facade's exact package/version/versionCode check.
bool identity_matches(const char *package, const char *version, uint64_t code);
}

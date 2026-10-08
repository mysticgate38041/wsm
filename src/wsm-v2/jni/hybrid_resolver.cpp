#include "hybrid_resolver.h"

namespace wsm {
namespace {
HybridState translate(BindingState state) {
    switch (state) {
        case BindingState::Found: return HybridState::MetadataFound;
        case BindingState::Missing: return HybridState::MetadataMissing;
        case BindingState::Ambiguous: return HybridState::MetadataAmbiguous;
        case BindingState::Unavailable: return HybridState::MetadataUnavailable;
        case BindingState::Truncated: return HybridState::MetadataTruncated;
        case BindingState::InvalidContract: return HybridState::InvalidContract;
    }
    return HybridState::MetadataUnavailable;
}
}
HybridResult resolve_hybrid(const BindingApi &api, void *klass, const Binding &wanted,
    const IdentityContract &expected, const IdentityContract &observed,
    const NativePointerApi *native, const AobRequest *discovery) {
    HybridResult result{};
    if (!valid_identity(expected) || !valid_binding(wanted)) {
        result.state = HybridState::InvalidContract;
        result.binding.state = BindingState::InvalidContract;
        return result;
    }
    if (!identity_matches(observed, expected)) { result.state = HybridState::IdentityMismatch; return result; }
    result.identity_verified = true;
    result.binding = resolve_binding(api, klass, wanted);
    result.state = translate(result.binding.state);
    if (discovery) {
        result.discovery = discovery->pattern ? scan_aob(discovery->span, *discovery->pattern, discovery->range) : ScanResult{};
        if (!discovery->pattern) result.discovery.state = ScanState::InvalidPattern;
    }
    if (result.binding.state != BindingState::Found) return result;
    const MethodInfoRef metadata{result.binding.method};
    if (native) {
        if (!native->ready()) { result.state = HybridState::NativeUnavailable; return result; }
        const NativeMethodPointer pointer{native->method_pointer(metadata, native->context)};
        if (!pointer.address || !native->executable_pointer(pointer, native->context)) {
            result.state = HybridState::NativeRejected; return result;
        }
        result.native_pointer = pointer;
    }
    result.method_info = metadata;
    result.qualified = true;
    return result;
}
const char *hybrid_state_name(HybridState state) {
    switch (state) {
        case HybridState::MetadataFound: return "metadata-found";
        case HybridState::MetadataMissing: return "metadata-missing";
        case HybridState::MetadataAmbiguous: return "metadata-ambiguous";
        case HybridState::MetadataUnavailable: return "metadata-unavailable";
        case HybridState::MetadataTruncated: return "metadata-truncated";
        case HybridState::InvalidContract: return "invalid-contract";
        case HybridState::IdentityMismatch: return "identity-mismatch";
        case HybridState::NativeUnavailable: return "native-unavailable";
        case HybridState::NativeRejected: return "native-rejected";
    }
    return "unknown";
}
}

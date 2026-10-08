#pragma once
#include "il2cpp_resolver.h"
#include "aob_scanner.h"

namespace wsm {
struct AobRequest { const MaskedPattern *pattern; ByteSpan span; ScanRange range; };
enum class HybridState {
    MetadataFound, MetadataMissing, MetadataAmbiguous, MetadataUnavailable,
    MetadataTruncated, InvalidContract, IdentityMismatch, NativeUnavailable, NativeRejected
};
struct HybridResult {
    HybridState state = HybridState::MetadataUnavailable;
    BindingResult binding{nullptr, BindingState::Unavailable, 0};
    MethodInfoRef method_info{};
    NativeMethodPointer native_pointer{};
    ScanResult discovery{};
    bool identity_verified = false;
    bool qualified = false;
};
// Metadata must match its full typed signature and exact caller-supplied identity.
// An AOB result is diagnostic only. Even a unique result NEVER supplies executable
// code, a MethodInfo, a changed-version qualification or an offset fallback.
HybridResult resolve_hybrid(const BindingApi &api, void *klass, const Binding &wanted,
    const IdentityContract &expected, const IdentityContract &observed,
    const NativePointerApi *native = nullptr, const AobRequest *discovery = nullptr);
const char *hybrid_state_name(HybridState state);
}

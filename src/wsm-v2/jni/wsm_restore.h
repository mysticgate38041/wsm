#ifndef WSM_RESTORE_H
#define WSM_RESTORE_H
#include <stddef.h>
#include <stdint.h>

namespace wsm {
// identity is an opaque comparison token, never a pointer fallback. The pinned
// strong GC handle owns the managed identity until verified clean release.
struct OptionOwnership {
    void *identity;
    uint32_t bits;
    uint64_t lifetime;
    uint32_t handle, preserve;
    bool verifying, uncertain;
};
struct OptionRestoreApi {
    void *context;
    bool (*is_live)(void *, void *);
    bool (*read)(void *, void *, uint32_t *);
    bool (*remove)(void *, void *, uint32_t);
    uint32_t (*pin)(void *, void *, bool);
    void *(*target)(void *, uint32_t);
    bool (*release)(void *, uint32_t);
};
struct OptionRestoreReport {
    unsigned attempted = 0, failed = 0, residual_objects = 0, uncertain_objects = 0;
    uint32_t residual_bits = 0;
    bool complete() const { return failed == 0 && residual_objects == 0; }
};
inline unsigned owned_option_objects(const OptionOwnership *owners, size_t count) {
    unsigned objects = 0;
    for (size_t i = 0; i < count; ++i) objects += owners[i].handle || owners[i].bits || owners[i].uncertain;
    return objects;
}
inline unsigned uncertain_option_objects(const OptionOwnership *owners, size_t count) {
    unsigned objects = 0;
    for (size_t i = 0; i < count; ++i) objects += owners[i].uncertain;
    return objects;
}
inline uint32_t owned_option_bits(const OptionOwnership *owners, size_t count) {
    uint32_t bits = 0;
    for (size_t i = 0; i < count; ++i) bits |= owners[i].bits;
    return bits;
}
inline void *option_lease_target(OptionOwnership &owner, const OptionRestoreApi &api) {
    if (owner.uncertain) return nullptr;
    if (!owner.handle || !api.target) { owner.uncertain = true; return nullptr; }
    void *target = api.target(api.context, owner.handle);
    if (!target || target != owner.identity) { owner.uncertain = true; return nullptr; }
    return target;
}
inline bool acquire_option_lease(OptionOwnership &owner, void *object, uint64_t lifetime,
        const OptionRestoreApi &api) {
    if (!object || owner.handle || owner.bits || owner.uncertain || !api.pin || !api.target || !api.release) return false;
    const uint32_t handle = api.pin(api.context, object, true);
    if (!handle) return false; // No option mutation may precede successful pinning.
    owner = {object, 0, lifetime, handle, 0, false, false};
    return option_lease_target(owner, api) != nullptr;
}
inline bool release_clean_option_lease(OptionOwnership &owner, const OptionRestoreApi &api) {
    if (owner.bits || owner.verifying || owner.uncertain) return false;
    if (owner.handle && !option_lease_target(owner,api)) return false;
    if (owner.handle && (!api.release || !api.release(api.context, owner.handle))) {
        // The free call may have partially executed; never retry an uncertain free.
        owner.uncertain = true;
        return false;
    }
    owner = {};
    return true;
}
inline bool prepare_option_add(OptionOwnership &owner, uint32_t before, uint32_t add) {
    if (!owner.handle || owner.uncertain) return false;
    owner.preserve |= before & ~owner.bits;
    owner.bits |= add; // An invocation may mutate and then throw.
    owner.verifying = true;
    return true;
}
inline bool confirm_option_add(OptionOwnership &owner, uint32_t before, uint32_t wanted, uint32_t after) {
    if ((after & owner.preserve) != owner.preserve) { owner.uncertain = true; return false; }
    if ((after & wanted) != wanted || (after & before) != before) return false;
    owner.verifying = false; owner.preserve = 0;
    return true;
}
inline OptionRestoreReport restore_options(OptionOwnership *owners, size_t count,
        uint64_t lifetime, bool trusted, const OptionRestoreApi &api, uint32_t desired = 0) {
    OptionRestoreReport report{};
    for (size_t i = 0; i < count; ++i) {
        auto &owner = owners[i];
        const uint32_t pending = owner.bits & ~desired;
        bool failed = false;
        if (owner.uncertain) failed = true; // Baseline loss/invalid lease stays sticky across retry.
        else if (!pending && !owner.verifying) {
            if (!owner.bits && owner.handle) {
                if (!trusted || !release_clean_option_lease(owner, api)) failed = true;
            } else if (!owner.bits) owner = {};
        } else if (!trusted || owner.lifetime != lifetime || !api.is_live || !api.read || !api.remove) failed = true;
        else {
            void *target = option_lease_target(owner, api);
            uint32_t before = 0;
            if (!target || !api.is_live(api.context, target) || !api.read(api.context, target, &before)) failed = true;
            else if (owner.verifying && (before & owner.preserve) != owner.preserve) {
                owner.uncertain = true; failed = true;
            } else {
                // Missing owned bits are proven clean only after checking any
                // outstanding game-baseline postcondition from an earlier call.
                owner.bits &= ~(pending & ~before);
                const uint32_t remove = pending & before;
                if (remove) {
                    owner.preserve |= before & ~owner.bits;
                    owner.verifying = true;
                    ++report.attempted;
                    target = option_lease_target(owner, api);
                    if (!target || !api.is_live(api.context, target) || !api.remove(api.context, target, remove)) failed = true;
                    else {
                        uint32_t after = 0;
                        target = option_lease_target(owner, api);
                        if (!target || !api.is_live(api.context, target) || !api.read(api.context, target, &after)) failed = true;
                        else if ((after & owner.preserve) != owner.preserve) {
                            owner.uncertain = true; failed = true;
                        } else if ((after & remove) || (after & (before & ~remove)) != (before & ~remove)) failed = true;
                        else { owner.bits &= ~remove; owner.verifying = false; owner.preserve = 0; }
                    }
                } else { owner.verifying = false; owner.preserve = 0; }
                if (!failed && !owner.bits && !release_clean_option_lease(owner, api)) failed = true;
            }
        }
        if (failed) ++report.failed;
        if ((owner.bits & ~desired) || owner.uncertain || owner.verifying || (!owner.bits && owner.handle)) {
            ++report.residual_objects;
            report.residual_bits |= owner.bits & ~desired;
        }
        report.uncertain_objects += owner.uncertain;
    }
    return report;
}
inline bool restoration_complete(const OptionRestoreReport &options, bool hooks_ok,
        bool time_ok, bool time_owned) {
    return options.complete() && hooks_ok && time_ok && !time_owned;
}
} // namespace wsm
#endif

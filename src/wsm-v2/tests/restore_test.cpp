#include "../jni/wsm_restore.h"
#include <assert.h>
#include <stdio.h>
#include <new>

struct Object {
    uint32_t mask = 131;
    bool live = true, remove_exception = false, remove_noop = false, lose_baseline = false;
    unsigned reads = 0, removes = 0, fail_read = 0, invalidate_on_read = 0;
    uint32_t last_remove = 0;
};
struct Access {
    unsigned live_checks = 0, pins = 0, targets_read = 0, releases = 0, next = 1;
    bool fail_pin = false, fail_release = false;
    void *targets[32]{};
};
static uint32_t pin(void *context, void *object, bool pinned) {
    auto &a=*static_cast<Access *>(context);++a.pins;assert(pinned);
    if(a.fail_pin)return 0;
    assert(a.next<32);const uint32_t handle=a.next++;a.targets[handle]=object;return handle;
}
static void *target(void *context,uint32_t handle) {
    auto &a=*static_cast<Access *>(context);++a.targets_read;
    return handle<32?a.targets[handle]:nullptr;
}
static bool release(void *context,uint32_t handle) {
    auto &a=*static_cast<Access *>(context);++a.releases;
    if(handle>=32||!a.targets[handle])return false;
    a.targets[handle]=nullptr; // a simulated fault may follow a partially completed free
    return !a.fail_release;
}
static bool live(void *context, void *key) {
    ++static_cast<Access *>(context)->live_checks;
    return key && static_cast<Object *>(key)->live;
}
static bool read(void *context, void *key, uint32_t *value) {
    auto &object = *static_cast<Object *>(key);
    if (++object.reads == object.fail_read) return false; // managed getter exception
    *value = object.mask;
    if(object.reads==object.invalidate_on_read) {
        auto &a=*static_cast<Access *>(context);
        for(auto &entry:a.targets)if(entry==key)entry=nullptr;
    }
    return true;
}
static bool remove(void *, void *key, uint32_t bits) {
    auto &object = *static_cast<Object *>(key);
    ++object.removes; object.last_remove = bits;
    if (object.remove_exception) return false;
    if (!object.remove_noop) object.mask &= ~bits;
    if (object.lose_baseline) object.mask &= ~128u;
    return true;
}
static wsm::OptionRestoreApi api(Access &access) { return {&access, live, read, remove, pin, target, release}; }
static wsm::OptionOwnership lease(Access &access,Object &object,uint32_t bits=3,uint64_t lifetime=7) {
    wsm::OptionOwnership owner{};
    assert(wsm::acquire_option_lease(owner,&object,lifetime,api(access)));
    owner.bits=bits;return owner;
}
static void empty_without_identity_or_scene() {
    wsm::OptionOwnership owners[2]{};
    auto report = wsm::restore_options(owners, 2, 7, false, {});
    assert(report.complete() && report.attempted == 0);
    assert(wsm::restoration_complete(report, true, true, false));
}
static void unavailable_scene_or_identity() {
    Object object; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, false, api(access));
    assert(!report.complete() && report.failed == 1 && report.residual_bits == 3);
    assert(owner.bits == 3 && owner.identity == &object && owner.handle != 0 && access.live_checks == 0);
    assert(object.reads == 0 && object.removes == 0);
}
static void stale_scene_or_reused_address() {
    Object object; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 8, true, api(access));
    assert(!report.complete() && owner.bits == 3);
    assert(access.live_checks == 0 && object.reads == 0 && object.removes == 0);
}
static void missing_live_object() {
    Object object; object.live = false; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(!report.complete() && owner.bits == 3 && report.residual_objects == 1);
    assert(object.reads == 0 && object.removes == 0);
}
static void getter_exception() {
    Object object; object.fail_read = 1; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(!report.complete() && owner.bits == 3 && object.removes == 0);
}
static void removal_exception_and_retry() {
    Object object; object.remove_exception = true; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(!report.complete() && report.attempted == 1 && owner.bits == 3 && object.mask == 131);
    object.remove_exception = false;
    report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(report.complete() && owner.bits == 0 && owner.identity == nullptr && owner.handle == 0 && object.mask == 128);
}
static void remove_success_without_effect() {
    Object object; object.remove_noop = true; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(!report.complete() && owner.bits == 3 && object.removes == 1 && object.reads == 2);
}
static void readback_exception_and_absent_bit_retry() {
    Object object; object.fail_read = 2; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(!report.complete() && owner.bits == 3 && object.mask == 128);
    report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(report.complete() && owner.bits == 0 && owner.identity == nullptr && owner.handle == 0 && object.removes == 1);
}
static void partial_multi_player_cleanup() {
    Object first, second; second.remove_exception = true; Access access;
    wsm::OptionOwnership owners[2]{lease(access,first),lease(access,second)};
    auto report = wsm::restore_options(owners, 2, 7, true, api(access));
    assert(!report.complete() && report.attempted == 2 && report.failed == 1 && report.residual_objects == 1);
    assert(owners[0].bits == 0 && owners[1].bits == 3 && report.residual_bits == 3);
    assert(first.mask == 128 && second.mask == 131);
    assert(!wsm::restoration_complete(report, true, true, false));
}
static void preexisting_bits_and_requested_overlap() {
    Object object; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access), 1);
    assert(report.complete() && object.last_remove == 2 && object.mask == 129 && owner.bits == 1);
    report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(report.complete() && object.last_remove == 1 && object.mask == 128 && owner.bits == 0);
}
static void unexpected_baseline_loss_is_not_complete() {
    Object object; object.lose_baseline = true; Access access;
    auto owner=lease(access,object);
    auto report = wsm::restore_options(&owner, 1, 7, true, api(access));
    assert(!report.complete() && owner.bits == 3 && report.failed == 1 && owner.uncertain);
    const unsigned reads=object.reads,removes=object.removes;
    for(int retry=0;retry<3;++retry) {
        report=wsm::restore_options(&owner,1,7,true,api(access));
        assert(!report.complete() && report.uncertain_objects==1 && owner.handle!=0);
        assert(object.mask==0 && object.reads==reads && object.removes==removes && access.releases==0);
    }
}
static void independent_cleanup_components() {
    wsm::OptionRestoreReport options{};
    assert(!wsm::restoration_complete(options, false, true, false));
    assert(!wsm::restoration_complete(options, true, false, false));
    assert(!wsm::restoration_complete(options, true, true, true));
    assert(wsm::restoration_complete(options, true, true, false));
}
static void same_lifetime_address_reuse_with_invalid_handle() {
    Access access;alignas(Object) unsigned char storage[sizeof(Object)];
    Object *original=new(storage) Object;
    auto owner=lease(access,*original);
    access.targets[owner.handle]=nullptr; // old managed identity is invalid
    original->~Object();Object *replacement=new(storage) Object;
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.uncertain && replacement->mask==131);
    assert(replacement->reads==0 && replacement->removes==0 && access.releases==0);
    // Even a later numeric-handle rebound cannot erase the observed invalidity.
    access.targets[owner.handle]=replacement;
    report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && replacement->reads==0 && replacement->removes==0);
}
static void unexpected_target_rebinding() {
    Access access;Object original,replacement;auto owner=lease(access,original);
    access.targets[owner.handle]=&replacement;
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.uncertain);
    assert(original.reads==0 && replacement.reads==0 && replacement.removes==0);
}
static void invalidation_between_read_and_remove() {
    Access access;Object object;auto owner=lease(access,object);object.invalidate_on_read=1;
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.uncertain && object.reads==1 && object.removes==0 && access.releases==0);
}
static void pin_failure_blocks_mutation() {
    Access access;access.fail_pin=true;Object object;object.mask=128;wsm::OptionOwnership owner{};
    assert(!wsm::acquire_option_lease(owner,&object,7,api(access)));
    assert(!wsm::prepare_option_add(owner,128,3));
    assert(owner.handle==0 && owner.bits==0 && object.mask==128 && object.reads==0 && object.removes==0);
}
static void release_failure_is_sticky_and_not_retried() {
    Access access;access.fail_release=true;Object object;auto owner=lease(access,object);
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.bits==0 && owner.handle!=0 && owner.uncertain && access.releases==1);
    const unsigned reads=object.reads;
    report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && access.releases==1 && object.reads==reads && object.mask==128);
}
static void invalidation_after_readback_blocks_free() {
    Access access;Object object;auto owner=lease(access,object);object.invalidate_on_read=2;
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.uncertain && owner.handle!=0 && owner.bits==0);
    assert(object.mask==128 && access.releases==0); // no free through an invalid handle
}
static void baseline_loss_hidden_by_readback_exception() {
    Access access;Object object;object.lose_baseline=true;object.fail_read=2;auto owner=lease(access,object);
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.verifying && owner.preserve==128 && !owner.uncertain && object.mask==0);
    report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && owner.uncertain && owner.bits==3 && access.releases==0);
}
static void add_attempt_owns_lease_and_baseline() {
    Access access;Object object;object.mask=128;auto owner=lease(access,object,0);
    assert(wsm::prepare_option_add(owner,128,3));
    assert(owner.bits==3 && owner.verifying && owner.preserve==128);
    object.mask=131; // managed Add mutates before a simulated exception
    auto report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(report.complete() && object.mask==128 && access.releases==1 && owner.handle==0);
    owner=lease(access,object,0);
    assert(wsm::prepare_option_add(owner,128,3));object.mask=3;
    assert(!wsm::confirm_option_add(owner,128,3,object.mask) && owner.uncertain);
    report=wsm::restore_options(&owner,1,7,true,api(access));
    assert(!report.complete() && access.releases==1 && object.mask==3);
}
int main() {
    empty_without_identity_or_scene(); unavailable_scene_or_identity();
    stale_scene_or_reused_address(); missing_live_object(); getter_exception();
    removal_exception_and_retry(); remove_success_without_effect();
    readback_exception_and_absent_bit_retry(); partial_multi_player_cleanup();
    preexisting_bits_and_requested_overlap(); unexpected_baseline_loss_is_not_complete();
    independent_cleanup_components(); same_lifetime_address_reuse_with_invalid_handle();
    unexpected_target_rebinding(); invalidation_between_read_and_remove(); pin_failure_blocks_mutation();
    release_failure_is_sticky_and_not_retried(); baseline_loss_hidden_by_readback_exception();
    add_attempt_owns_lease_and_baseline(); invalidation_after_readback_blocks_free();
    puts("restore_test: 20 shared production lease/cleanup cases PASS");
    return 0;
}

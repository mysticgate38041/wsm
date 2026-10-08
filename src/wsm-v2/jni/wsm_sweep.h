#pragma once
#include <stdint.h>
#include <string.h>

namespace wsm {
struct PulsePolicy {
    int damage = 1000000;
    bool one_hp = false;
    bool critical = false;
};
inline bool pulse_power_damage(float power, int &damage) {
    // This also rejects NaN/infinity before the integer conversion.
    if (!(power >= 1 && power <= 99) || power != static_cast<int>(power)) return false;
    damage = static_cast<int>(power) * 100000; // bounded Int32: at most 9,900,000
    return true;
}
inline bool pulse_policy(bool periodic, bool ohk, bool one_hp, bool power_on,
                         float power, bool critical_on, PulsePolicy &out) {
    PulsePolicy policy{};
    if (periodic) {
        policy.one_hp = one_hp && !ohk;
        if (!policy.one_hp) {
            if (power_on && !pulse_power_damage(power, policy.damage)) return false;
            policy.critical = critical_on;
        }
    }
    out = policy;
    return true;
}
// Called only from a verified Unity main-thread callback, in the ARM64 guest.
// DamageCommandUtil owns stats/behaviour/death dispatch; no monster MethodInfo
// is ever invoked with a foreign receiver and no death callback is forced twice.
struct SweepApi {
    void *(*invoke)(void *, void *, void **, void **);
    void *(*unbox)(void *);
    uint32_t (*pin)(void *, bool);
    void *(*target)(uint32_t);
    void (*release)(uint32_t);
    void *stage, *manager, *monsters, *active, *stats, *dead;
    void *factory, *not_mortal, *pipeline;
    void *players = nullptr, *position = nullptr, *hp = nullptr;
    float radius = 0; // 0: explicit Clear Stage; positive: periodic nearby pulse.
    bool one_hp = false;
    bool probe_only = false;
    int pulse_damage = 1000000;
    bool critical = false;
    void *set_critical = nullptr, *set_no_critical = nullptr;
    bool ready() const {
        return invoke && unbox && pin && target && release && stage && manager &&
            monsters && active && stats && dead && factory && not_mortal && pipeline &&
            (!one_hp || hp) && pulse_damage >= 100000 && pulse_damage <= 9900000 &&
            pulse_damage % 100000 == 0 &&
            (!critical || one_hp || (set_critical && set_no_critical));
    }
};
enum class SweepState : uint64_t { Idle, Pending, Running, Applied, Stale, Failed, Cancelled };
struct SweepResult { SweepState state; uint32_t applied, skipped; };
struct SweepPins {
    const SweepApi &api;
    uint32_t handles[261]{};
    unsigned count = 0;
    explicit SweepPins(const SweepApi &a) : api(a) {}
    ~SweepPins() { while (count) api.release(handles[--count]); }
    uint32_t add(void *o) {
        if (!o || count == 261) return 0;
        const uint32_t h = api.pin(o, true);
        if (h) handles[count++] = h;
        return h;
    }
};
inline void *sweep_call(const SweepApi &a, void *method, void *obj, void **args, bool &ok) {
    if (!ok) return nullptr;
    void *exception = nullptr;
    void *value = a.invoke(method, obj, args, &exception);
    if (exception) ok = false;
    return value;
}
inline bool sweep_list(void *list, void *&array, int &count) {
    if (!list) return false;
    memcpy(&array, (const uint8_t *)list + 0x10, 8);
    memcpy(&count, (const uint8_t *)list + 0x18, 4);
    uint64_t length = 0;
    if (array) memcpy(&length, (const uint8_t *)array + 0x18, 8);
    return count >= 0 && count <= 256 && (!count || (array && length >= (uint64_t)count));
}
inline void *sweep_item(void *array, int index) {
    void *item = nullptr;
    memcpy(&item, (const uint8_t *)array + 0x20 + 8 * index, 8);
    return item;
}
inline SweepResult run_sweep(const SweepApi &request, void *expected_stage,
                             volatile uint64_t *allowed, uint64_t ticket) {
    // Engine retains request storage until ACK and rejects updates while a ticket
    // is outstanding. Keep this executing ticket's policy immutable as well.
    const SweepApi a = request;
    SweepResult result{SweepState::Failed, 0, 0};
    if (!a.ready() || !expected_stage || !allowed || !ticket) return result;
    if (__atomic_load_n(allowed, __ATOMIC_ACQUIRE) != ticket) {
        result.state = SweepState::Cancelled; return result;
    }
    bool ok = true;
    SweepPins pins(a);
    void *stage = sweep_call(a, a.stage, nullptr, nullptr, ok);
    if (!ok) return result;
    if (stage != expected_stage) { result.state = SweepState::Stale; return result; }
    if (a.probe_only) { result.state = SweepState::Applied; return result; }
    if (!pins.add(stage)) return result;
    void *manager = sweep_call(a, a.manager, stage, nullptr, ok);
    if (!ok || !manager || !pins.add(manager)) return result;
    float origin[3]{};
    if (a.radius > 0) {
        if (!a.players || !a.position || (a.one_hp && !a.hp)) return result;
        void *players = sweep_call(a, a.players, manager, nullptr, ok);
        void *player_array = nullptr; int player_count = 0;
        if (!ok || !sweep_list(players, player_array, player_count) || !player_count) return result;
        void *hero = sweep_item(player_array, 0);
        if (!hero || !pins.add(hero)) return result;
        void *position = sweep_call(a, a.position, hero, nullptr, ok);
        void *data = ok && position ? a.unbox(position) : nullptr;
        if (!data) return result;
        memcpy(origin, data, sizeof origin);
    }
    void *list = sweep_call(a, a.monsters, manager, nullptr, ok);
    if (!ok || !list || !pins.add(list)) return result;
    void *array = nullptr; int count = 0;
    if (!sweep_list(list, array, count) || (array && !pins.add(array))) return result;
    uint32_t snapshot[256]{};
    for (int i = 0; i < count; ++i) {
        void *obj = sweep_item(array, i);
        if (obj && !(snapshot[i] = pins.add(obj))) return result;
    }
    for (int i = 0; i < count; ++i) {
        if (__atomic_load_n(allowed, __ATOMIC_ACQUIRE) != ticket) {
            result.state = SweepState::Cancelled; return result;
        }
        stage = sweep_call(a, a.stage, nullptr, nullptr, ok);
        if (!ok) return result;
        if (stage != expected_stage) { result.state = SweepState::Stale; return result; }
        void *obj = snapshot[i] ? a.target(snapshot[i]) : nullptr;
        if (!obj) { ++result.skipped; continue; }
        // Snapshot identity is stable even when death callbacks reorder the list.
        list = sweep_call(a, a.monsters, manager, nullptr, ok);
        bool present = false;
        if (!ok) return result;
        int current_count = 0;
        if (!sweep_list(list, array, current_count)) return result;
        for (int j = 0; j < current_count; ++j) if (sweep_item(array, j) == obj) { present = true; break; }
        if (!present) { ++result.skipped; continue; }
        void *boxed_active = sweep_call(a, a.active, obj, nullptr, ok);
        int active = -1;
        if (boxed_active && a.unbox(boxed_active)) memcpy(&active, a.unbox(boxed_active), 4);
        if (!ok) return result;
        if (active != 2 && active != 3) { ++result.skipped; continue; }
        if (a.radius > 0) {
            void *position = sweep_call(a, a.position, obj, nullptr, ok);
            void *data = ok && position ? a.unbox(position) : nullptr;
            if (!data) return result;
            float xyz[3]; memcpy(xyz, data, sizeof xyz);
            const float dx = xyz[0] - origin[0], dz = xyz[2] - origin[2];
            if (!(dx * dx + dz * dz <= a.radius * a.radius)) { ++result.skipped; continue; }
        }
        void *stats = sweep_call(a, a.stats, obj, nullptr, ok);
        if (!ok || !stats) return result;
        void *boxed_dead = sweep_call(a, a.dead, stats, nullptr, ok);
        uint8_t dead = 1;
        if (boxed_dead && a.unbox(boxed_dead)) memcpy(&dead, a.unbox(boxed_dead), 1);
        if (!ok) return result;
        if (dead) { ++result.skipped; continue; }
        int damage = a.pulse_damage;
        if (a.one_hp) {
            void *boxed_hp = sweep_call(a, a.hp, stats, nullptr, ok);
            void *data = ok && boxed_hp ? a.unbox(boxed_hp) : nullptr;
            if (!data) return result;
            int hp = 0; memcpy(&hp, data, sizeof hp);
            if (hp <= 1) { ++result.skipped; continue; }
            damage = hp - 1;
        }
        // runtime_invoke takes reference arguments as the object pointer itself;
        // only value-type arguments are addresses of their storage.
        void *factory_args[] = {obj, &damage};
        void *info = sweep_call(a, a.factory, nullptr, factory_args, ok);
        uint32_t info_pin = ok && info ? a.pin(info, true) : 0;
        if (!info_pin) return result;
        void *data = a.unbox(a.target(info_pin));
        bool mortal = a.one_hp; // ONEHP also forbids accidental death in the pipeline.
        void *mortal_args[] = {&mortal};
        if (data) sweep_call(a, a.not_mortal, data, mortal_args, ok); else ok = false;
        if (ok && a.critical && !a.one_hp) {
            bool critical = true, no_critical = false;
            void *critical_args[] = {&critical}, *no_critical_args[] = {&no_critical};
            sweep_call(a, a.set_critical, data, critical_args, ok);
            sweep_call(a, a.set_no_critical, data, no_critical_args, ok);
        }
        // Creating info/setter must not silently invalidate the scene either.
        stage = sweep_call(a, a.stage, nullptr, nullptr, ok);
        const bool cancelled = __atomic_load_n(allowed, __ATOMIC_ACQUIRE) != ticket;
        if (ok && stage == expected_stage && !cancelled) {
            int command_types = 0; // CommandTypes.None: local normal pipeline.
            void *args[] = {&command_types, data};
            sweep_call(a, a.pipeline, nullptr, args, ok);
            if (ok) ++result.applied;
        }
        a.release(info_pin);
        if (!ok) return result;
        if (cancelled) { result.state = SweepState::Cancelled; return result; }
        if (stage != expected_stage) { result.state = SweepState::Stale; return result; }
        // Do not dereference the target after death; reacquire the next identity.
    }
    result.state = SweepState::Applied;
    return result;
}
}

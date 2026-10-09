#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

namespace wsm {
constexpr size_t FeatureCount = 18;
enum class FeatureIndex : size_t {
    God, Hp, Stamina, Mana, Poise, Immune, Godmode, Ohk, OneHp,
    Aura, Damage, Critical, CriticalDamage, NoCooldown, StunAll,
    Speed, Loot, Timescale
};
struct FeatureSpec {
    const char *id;
    float initial, minimum, maximum;
};
extern const FeatureSpec FeatureSpecs[FeatureCount];
int feature_index(const char *id);
struct FeatureValue { bool enabled; float value; };
struct FeatureSnapshot {
    uint64_t epoch, revision;
    bool ready;
    FeatureValue features[FeatureCount];
};

// This is an observed-state cache, never an apply acknowledgement. The engine
// owns game mutations and only publishes observations after their own checks.
// Atomic floats preserve slider precision; the mutex makes multi-field writes
// and snapshots coherent and serializes reset/lifecycle with observations.
class FeatureFlags {
    struct AtomicValue {
        std::atomic<bool> enabled{false};
        std::atomic<float> value{0};
    };
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    AtomicValue values_[FeatureCount];
    std::atomic<uint64_t> epoch_{1}, revision_{0};
    std::atomic<bool> ready_{false};
    bool reset_values_locked();
public:
    FeatureFlags();
    ~FeatureFlags();
    FeatureFlags(const FeatureFlags &) = delete;
    FeatureFlags &operator=(const FeatureFlags &) = delete;
    // Fixed controls use their actual engine values (e.g. god=3, poise=52).
    // Slider values must be finite and in their existing production range.
    // Observations preserve configuration; read/update APIs gate execution readiness.
    // Validate the entire observation before changing any member. One revision
    // denotes one complete lifecycle + feature transaction.
    bool publish_observation(uint64_t epoch, bool ready, const FeatureValue (&values)[FeatureCount]);
    bool update(FeatureIndex index, bool enabled, float value, uint64_t expected_epoch);
    bool update(const char *id, bool enabled, float value, uint64_t expected_epoch);
    // Older epochs are rejected. Advancing lifecycle resets cached controls.
    bool lifecycle(uint64_t epoch, bool ready);
    // PANIC/scene reset disables every control, restores defaults and pauses.
    bool reset(uint64_t epoch);
    FeatureSnapshot snapshot();
    // Consumers must pass the current epoch; paused/stale reads fail closed.
    bool read(FeatureIndex index, uint64_t expected_epoch, FeatureValue &out);
    bool enabled(FeatureIndex index, uint64_t expected_epoch);
    uint64_t epoch() const { return epoch_.load(std::memory_order_acquire); }
    uint64_t revision() const { return revision_.load(std::memory_order_acquire); }
    bool ready() const { return ready_.load(std::memory_order_acquire); }
};
}

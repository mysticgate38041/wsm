#include "feature_flags.h"
#include <cmath>
#include <string.h>

namespace wsm {
// Same order as the 18 production menu controls, excluding retired entries.
const FeatureSpec FeatureSpecs[FeatureCount] = {
    {"god", 3, 3, 3}, {"hp", 1, 1, 1}, {"stam", 100, 100, 100},
    {"mana", 100, 100, 100}, {"poise", 52, 52, 52}, {"immune", 224, 224, 224},
    {"godmode", 1, 1, 1}, {"ohk", 1, 1, 1}, {"onehp", 1, 1, 1},
    {"aura", 20, 5, 40}, {"dmg", 10, 1, 99}, {"crit", 1, 1, 1},
    {"critdmg", 2, 1, 5}, {"nocd", 1, 1, 1}, {"stunall", 1, 1, 1},
    {"speed", 2, 1, 5}, {"loot", 1, 1, 1}, {"timescale", 1, .1f, 5}
};
int feature_index(const char *id) {
    if (id) for (size_t i = 0; i < FeatureCount; ++i)
        if (strcmp(id, FeatureSpecs[i].id) == 0) return static_cast<int>(i);
    return -1;
}
FeatureFlags::FeatureFlags() { reset_values_locked(); }
FeatureFlags::~FeatureFlags() { pthread_mutex_destroy(&mutex_); }
bool FeatureFlags::reset_values_locked() {
    bool changed = false;
    for (size_t i = 0; i < FeatureCount; ++i) {
        changed = changed || values_[i].enabled.load(std::memory_order_relaxed) ||
            values_[i].value.load(std::memory_order_relaxed) != FeatureSpecs[i].initial;
        values_[i].enabled.store(false, std::memory_order_relaxed);
        values_[i].value.store(FeatureSpecs[i].initial, std::memory_order_relaxed);
    }
    return changed;
}
bool FeatureFlags::update(FeatureIndex index, bool enabled, float value, uint64_t expected_epoch) {
    const size_t i = static_cast<size_t>(index);
    if (i >= FeatureCount || !std::isfinite(value) || value < FeatureSpecs[i].minimum ||
        value > FeatureSpecs[i].maximum || !expected_epoch) return false;
    pthread_mutex_lock(&mutex_);
    const bool valid = expected_epoch == epoch_.load(std::memory_order_relaxed) &&
        (!enabled || ready_.load(std::memory_order_relaxed));
    if (valid && (enabled != values_[i].enabled.load(std::memory_order_relaxed) ||
        value != values_[i].value.load(std::memory_order_relaxed))) {
        values_[i].value.store(value, std::memory_order_relaxed);
        values_[i].enabled.store(enabled, std::memory_order_relaxed);
        revision_.fetch_add(1, std::memory_order_release);
    }
    pthread_mutex_unlock(&mutex_);
    return valid;
}
bool FeatureFlags::update(const char *id, bool enabled, float value, uint64_t expected_epoch) {
    const int i = feature_index(id);
    return i >= 0 && update(static_cast<FeatureIndex>(i), enabled, value, expected_epoch);
}
bool FeatureFlags::lifecycle(uint64_t epoch, bool ready) {
    if (!epoch) return false;
    pthread_mutex_lock(&mutex_);
    const uint64_t old = epoch_.load(std::memory_order_relaxed);
    const bool valid = epoch >= old;
    if (valid && (epoch != old || ready != ready_.load(std::memory_order_relaxed))) {
        if (epoch != old) reset_values_locked();
        epoch_.store(epoch, std::memory_order_relaxed);
        ready_.store(ready, std::memory_order_release);
        revision_.fetch_add(1, std::memory_order_release);
    }
    pthread_mutex_unlock(&mutex_);
    return valid;
}
bool FeatureFlags::reset(uint64_t epoch) {
    if (!epoch) return false;
    pthread_mutex_lock(&mutex_);
    const uint64_t old = epoch_.load(std::memory_order_relaxed);
    const bool valid = epoch >= old;
    if (valid) {
        bool changed = reset_values_locked();
        changed = changed || epoch != old || ready_.load(std::memory_order_relaxed);
        epoch_.store(epoch, std::memory_order_relaxed);
        ready_.store(false, std::memory_order_release);
        if (changed) revision_.fetch_add(1, std::memory_order_release);
    }
    pthread_mutex_unlock(&mutex_);
    return valid;
}
FeatureSnapshot FeatureFlags::snapshot() {
    pthread_mutex_lock(&mutex_);
    FeatureSnapshot s{};
    s.epoch = epoch_.load(std::memory_order_relaxed);
    s.revision = revision_.load(std::memory_order_relaxed);
    s.ready = ready_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < FeatureCount; ++i)
        s.features[i] = {values_[i].enabled.load(std::memory_order_relaxed),
            values_[i].value.load(std::memory_order_relaxed)};
    pthread_mutex_unlock(&mutex_);
    return s;
}
bool FeatureFlags::read(FeatureIndex index, uint64_t expected_epoch, FeatureValue &out) {
    const size_t i = static_cast<size_t>(index);
    out = {false, 0};
    if (i >= FeatureCount || !expected_epoch) return false;
    pthread_mutex_lock(&mutex_);
    const bool valid = ready_.load(std::memory_order_relaxed) &&
        expected_epoch == epoch_.load(std::memory_order_relaxed);
    if (valid) out = {values_[i].enabled.load(std::memory_order_relaxed),
        values_[i].value.load(std::memory_order_relaxed)};
    pthread_mutex_unlock(&mutex_);
    return valid;
}
bool FeatureFlags::enabled(FeatureIndex index, uint64_t expected_epoch) {
    FeatureValue value{};
    return read(index, expected_epoch, value) && value.enabled;
}
}

#include "../jni/feature_flags.h"
#include <assert.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdio.h>
#include <thread>

static void mapping_and_revision() {
    wsm::FeatureFlags flags;
    // Independent production-menu contract, including all experimental controls.
    const char *expected_ids[] = {"god", "hp", "stam", "mana", "poise", "immune",
        "godmode", "ohk", "onehp", "aura", "dmg", "crit", "critdmg", "nocd",
        "stunall", "speed", "loot", "timescale"};
    const float expected_defaults[] = {3, 1, 100, 100, 52, 224, 1, 1, 1,
        20, 10, 1, 2, 1, 1, 2, 1, 1};
    static_assert(sizeof expected_ids / sizeof expected_ids[0] == wsm::FeatureCount,
        "18-control production contract changed");
    auto s = flags.snapshot(); assert(s.epoch == 1 && !s.ready && s.revision == 0);
    for (size_t i = 0; i < wsm::FeatureCount; ++i) {
        assert(wsm::feature_index(expected_ids[i]) == static_cast<int>(i));
        assert(!s.features[i].enabled && s.features[i].value == expected_defaults[i]);
    }
    assert(wsm::feature_index(nullptr) == -1 && wsm::feature_index("aggro") == -1);
    assert(!flags.update("god", true, 3, 1)); // Not ready; cannot fake enable.
    assert(flags.lifecycle(1, true) && flags.revision() == 1);
    assert(flags.lifecycle(1, true) && flags.revision() == 1);
    for (size_t i = 0; i < wsm::FeatureCount; ++i) {
        assert(flags.update(wsm::FeatureSpecs[i].id, true, wsm::FeatureSpecs[i].initial, 1));
        assert(flags.enabled(static_cast<wsm::FeatureIndex>(i), 1));
    }
    assert(flags.revision() == 1 + wsm::FeatureCount);
    uint64_t revision = flags.revision();
    assert(flags.update("god", true, 3, 1) && flags.revision() == revision);
    assert(!flags.update("god", true, 1, 1));
    assert(!flags.update("missing", false, 1, 1));
    assert(!flags.update(static_cast<wsm::FeatureIndex>(99), false, 1, 1));
    assert(!flags.update("hp", false, 1, 0));
    assert(!flags.update("hp", false, 1, 2));
    assert(flags.revision() == revision);
    assert(flags.lifecycle(1, false));
    wsm::FeatureValue v{true, 42};
    assert(!flags.read(wsm::FeatureIndex::God, 1, v) && !v.enabled && v.value == 0);
    assert(!flags.enabled(wsm::FeatureIndex::God, 1));
    assert(!flags.update("god", true, 3, 1));
    assert(flags.update("god", false, 3, 1));
    assert(flags.lifecycle(2, true));
    s = flags.snapshot(); assert(s.epoch == 2 && s.ready);
    for (auto &value : s.features) assert(!value.enabled);
    assert(!flags.lifecycle(1, true) && !flags.reset(1));
    assert(!flags.read(wsm::FeatureIndex::Hp, 1, v));
}
static void numeric_and_reset() {
    wsm::FeatureFlags flags; assert(flags.lifecycle(4, true));
    struct Slider { const char *id; float value; } sliders[] = {
        {"speed", 2.25f}, {"critdmg", 3.75f}, {"timescale", .37f},
        {"aura", 12.5f}, {"dmg", 13.75f}
    };
    for (auto &slider : sliders) {
        const int i = wsm::feature_index(slider.id);
        assert(flags.update(slider.id, true, slider.value, 4));
        auto s = flags.snapshot(); assert(s.features[i].value == slider.value && s.features[i].enabled);
        const uint64_t revision = flags.revision();
        assert(!flags.update(slider.id, true, std::numeric_limits<float>::quiet_NaN(), 4));
        assert(!flags.update(slider.id, true, std::numeric_limits<float>::infinity(), 4));
        assert(!flags.update(slider.id, true, -std::numeric_limits<float>::infinity(), 4));
        assert(!flags.update(slider.id, true, wsm::FeatureSpecs[i].minimum - .01f, 4));
        assert(!flags.update(slider.id, false, wsm::FeatureSpecs[i].maximum + .01f, 4));
        assert(flags.revision() == revision);
        assert(flags.update(slider.id, true, wsm::FeatureSpecs[i].minimum, 4));
        assert(flags.update(slider.id, true, wsm::FeatureSpecs[i].maximum, 4));
    }
    const uint64_t revision = flags.revision(); assert(flags.reset(5));
    auto s = flags.snapshot(); assert(s.epoch == 5 && !s.ready && s.revision == revision + 1);
    for (size_t i = 0; i < wsm::FeatureCount; ++i)
        assert(!s.features[i].enabled && s.features[i].value == wsm::FeatureSpecs[i].initial);
    assert(flags.reset(5) && flags.revision() == s.revision);
    assert(!flags.update("speed", true, 2, 4) && !flags.update("speed", true, 2, 5));
}
static void coherent_snapshots() {
    wsm::FeatureFlags flags; assert(flags.lifecycle(10, true));
    std::atomic<int> finished{0}; std::thread writers[4];
    const char *ids[] = {"speed", "critdmg", "aura", "dmg"};
    for (int i = 0; i < 4; ++i) writers[i] = std::thread([&, i] {
        const auto &spec = wsm::FeatureSpecs[wsm::feature_index(ids[i])];
        for (int j = 0; j < 1000; ++j)
            assert(flags.update(ids[i], true, j % 2 ? spec.minimum : spec.maximum, 10));
        ++finished;
    });
    uint64_t previous = 0;
    do {
        auto s = flags.snapshot(); assert(s.epoch == 10 && s.ready && s.revision >= previous);
        previous = s.revision;
        for (size_t i = 0; i < wsm::FeatureCount; ++i)
            assert(std::isfinite(s.features[i].value) && s.features[i].value >= wsm::FeatureSpecs[i].minimum &&
                s.features[i].value <= wsm::FeatureSpecs[i].maximum);
    } while (finished != 4);
    for (auto &writer : writers) writer.join();

    // Producers prepared under epoch 10 cannot re-enable any control after reset.
    std::atomic<int> prepared{0}, rejected{0}; std::atomic<bool> release{false};
    for (int i = 0; i < 4; ++i) writers[i] = std::thread([&, i] {
        const uint64_t expected = flags.epoch(); ++prepared;
        while (!release) std::this_thread::yield();
        if (!flags.update(ids[i], true, wsm::FeatureSpecs[wsm::feature_index(ids[i])].initial, expected)) ++rejected;
    });
    while (prepared != 4) std::this_thread::yield();
    assert(flags.reset(11)); release = true;
    for (auto &writer : writers) writer.join(); assert(rejected == 4);
    auto s = flags.snapshot(); assert(s.epoch == 11 && !s.ready);
    for (auto &value : s.features) assert(!value.enabled);
}
int main() {
    mapping_and_revision(); numeric_and_reset(); coherent_snapshots();
    puts("PASS flags: 18-control mapping, exact float sliders/ranges/nonfinite, readiness/epoch gates, reset/revision semantics, 4 writers / 4000 updates and coherent snapshots");
}

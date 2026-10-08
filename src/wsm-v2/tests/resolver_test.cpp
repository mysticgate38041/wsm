#include "../jni/hybrid_resolver.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

struct Method {
    const char *name, *returns;
    const char *params[3];
    uint32_t count;
    bool stat, generic, inflated;
};
struct Fixture {
    Method rows[3] = {
        {"GetBattleFor", "Oak.BattleInstance", {"Oak.Party", "System.Boolean", nullptr}, 2, false, false, false},
        {"GetBattleFor", "Oak.BattleInstance", {"Oak.IFieldObject", "System.Boolean", nullptr}, 2, false, false, false},
        {"Other", "System.Void", {nullptr, nullptr, nullptr}, 0, true, false, false}
    };
    uint32_t total = 3;
    uint32_t enumerated = 0;
};
static unsigned allocated = 0, released = 0;
static void *methods(void *klass, void **iterator) {
    Fixture *fixture = static_cast<Fixture *>(klass);
    const uintptr_t index = reinterpret_cast<uintptr_t>(*iterator);
    *iterator = reinterpret_cast<void *>(index + 1);
    ++fixture->enumerated;
    return index < fixture->total ? &fixture->rows[index < 3 ? index : 2] : nullptr;
}
static const char *name(void *method) { return static_cast<Method *>(method)->name; }
static uint32_t argc(void *method) { return static_cast<Method *>(method)->count; }
static void *param(void *method, uint32_t i) { return const_cast<char *>(static_cast<Method *>(method)->params[i]); }
static void *returns(void *method) { return const_cast<char *>(static_cast<Method *>(method)->returns); }
static uint32_t flags(void *method, uint32_t *implementation) {
    *implementation = 0; return static_cast<Method *>(method)->stat ? 0x10u : 0u;
}
static char *type_name(void *type) {
    if (!type) return nullptr;
    const size_t length = strlen(static_cast<char *>(type)) + 1;
    char *text = static_cast<char *>(malloc(length));
    if (text) { ++allocated; memcpy(text, type, length); }
    return text;
}
static void release(void *text) { ++released; free(text); }
static bool generic(void *method) { return static_cast<Method *>(method)->generic; }
static bool inflated(void *method) { return static_cast<Method *>(method)->inflated; }
static wsm::BindingApi api() { return {methods, name, argc, param, returns, flags, type_name, release, generic, inflated}; }
static wsm::Binding wanted() {
    return {"GetBattleFor", "Oak.BattleInstance", {"Oak.IFieldObject", "System.Boolean", nullptr}, 2, false};
}

static void binding_cases() {
    Fixture fixture;
    const wsm::BindingApi runtime = api();
    wsm::Binding contract = wanted();
    auto result = wsm::resolve_binding(runtime, &fixture, contract);
    assert(result.state == wsm::BindingState::Found && result.method == &fixture.rows[1] && result.matches == 1);
    fixture.rows[0].params[0] = "Oak.IFieldObject";
    result = wsm::resolve_binding(runtime, &fixture, contract);
    assert(result.state == wsm::BindingState::Ambiguous && !result.method && result.matches == 2);
    fixture.rows[0].stat = true;
    assert(wsm::resolve_binding(runtime, &fixture, contract).method == &fixture.rows[1]);
    fixture.rows[1].params[1] = "System.Int32";
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Missing);
    fixture.rows[1].params[1] = "System.Boolean";
    fixture.rows[1].returns = "System.Boolean";
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Missing);
    fixture.rows[1].returns = "Oak.BattleInstance";
    fixture.rows[1].generic = true;
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Missing);
    fixture.rows[1].generic = false; fixture.rows[1].inflated = true;
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Missing);
    fixture.rows[1].inflated = false; fixture.rows[1].count = 1;
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Missing);
    fixture.rows[1].count = 2; fixture.rows[1].returns = nullptr;
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Missing);
    fixture.rows[1].returns = "Oak.BattleInstance";
    fixture.total = wsm::kMaxMethodEnumeration - 1; fixture.enumerated = 0;
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::Found);
    assert(fixture.enumerated == wsm::kMaxMethodEnumeration);
    fixture.total = wsm::kMaxMethodEnumeration; fixture.enumerated = 0;
    result = wsm::resolve_binding(runtime, &fixture, contract);
    assert(result.state == wsm::BindingState::Truncated && !result.method && result.matches == 1);
    assert(fixture.enumerated == wsm::kMaxMethodEnumeration);
    contract.name = nullptr;
    assert(wsm::resolve_binding(runtime, &fixture, contract).state == wsm::BindingState::InvalidContract);
    contract = wanted(); contract.returns = "";
    assert(!wsm::valid_binding(contract));
    contract = wanted(); contract.params[1] = nullptr;
    assert(!wsm::valid_binding(contract));
    contract = wanted(); contract.argc = 4;
    assert(!wsm::valid_binding(contract));
    contract = wanted(); auto incomplete = runtime; incomplete.release = nullptr;
    assert(wsm::resolve_binding(incomplete, &fixture, contract).state == wsm::BindingState::Unavailable);
    assert(!wsm::type_matches(incomplete, const_cast<char *>("System.Int32"), "System.Int32"));
    assert(wsm::resolve_binding(runtime, nullptr, contract).state == wsm::BindingState::Unavailable);
    assert(allocated == released);
}

static void pattern_cases() {
    auto parsed = wsm::parse_aob("ab\tC? ?f ?? ?\r\n42");
    assert(parsed.state == wsm::PatternState::Parsed && parsed.pattern.length == 6);
    assert(parsed.pattern.bytes[0].value == 0xab && parsed.pattern.bytes[0].mask == 0xff);
    assert(parsed.pattern.bytes[1].value == 0xc0 && parsed.pattern.bytes[1].mask == 0xf0);
    assert(parsed.pattern.bytes[2].value == 0x0f && parsed.pattern.bytes[2].mask == 0x0f);
    assert(parsed.pattern.bytes[3].mask == 0 && parsed.pattern.bytes[4].mask == 0);
    assert(wsm::parse_aob(nullptr).state == wsm::PatternState::Empty);
    assert(wsm::parse_aob(" \n\t").state == wsm::PatternState::Empty);
    assert(wsm::parse_aob("? ??").state == wsm::PatternState::Unconstrained);
    const char *invalid[] = {"AA GG", "AA A", "AA 0x20", "AA 123", "AA ?Z", "AA A?B", "AA,BB"};
    for (const char *text : invalid) {
        parsed = wsm::parse_aob(text);
        assert(parsed.state == wsm::PatternState::InvalidToken && !wsm::valid_pattern(parsed.pattern));
    }
    char oversized[wsm::kMaxAobText + 1];
    memset(oversized, ' ', sizeof oversized); oversized[sizeof oversized - 1] = 0;
    assert(wsm::parse_aob(oversized).state == wsm::PatternState::TooLong);
    char many[(wsm::kMaxAobBytes + 1) * 3 + 1]{};
    for (size_t i = 0; i <= wsm::kMaxAobBytes; ++i) memcpy(many + i * 3, "AA ", 3);
    parsed = wsm::parse_aob(many);
    assert(parsed.state == wsm::PatternState::TooLong && !wsm::valid_pattern(parsed.pattern));
    many[wsm::kMaxAobBytes * 3] = 0;
    assert(wsm::parse_aob(many).state == wsm::PatternState::Parsed);
    auto manual = wsm::parse_aob("AA").pattern;
    manual.bytes[0].mask = 0x03;
    assert(!wsm::valid_pattern(manual));
    manual.bytes[0] = {0xaa, 0xf0};
    assert(!wsm::valid_pattern(manual));
}

static void scanner_cases() {
    const uint8_t bytes[]{0xaa, 0xbb, 0x00, 0xaa, 0xbb, 0x00};
    const auto pattern = wsm::parse_aob("AA BB").pattern;
    wsm::ByteSpan span{bytes, sizeof bytes, 0x1000};
    auto result = wsm::scan_aob(span, pattern);
    assert(result.state == wsm::ScanState::Ambiguous && result.matches == 2 && !result.address);
    result = wsm::scan_aob(span, pattern, {3, 5, 1, 10});
    assert(result.state == wsm::ScanState::Found && result.address == 0x1003 && result.offset == 3);
    assert(wsm::scan_aob(span, pattern, {3, 4, 1, 10}).state == wsm::ScanState::Missing);
    result = wsm::scan_aob(span, pattern, {0, SIZE_MAX, 2, 10});
    assert(result.state == wsm::ScanState::Found && result.address == 0x1000);
    span.address = 0x1001;
    result = wsm::scan_aob(span, pattern, {0, SIZE_MAX, 4, 10});
    assert(result.state == wsm::ScanState::Found && result.address == 0x1004 && result.offset == 3);
    span.address = 0x1000;
    result = wsm::scan_aob(span, pattern, {0, SIZE_MAX, 1, 1});
    assert(result.state == wsm::ScanState::Truncated && result.matches == 1 && !result.address && result.candidates == 1);
    result = wsm::scan_aob(span, pattern, {0, 2, 1, 1});
    assert(result.state == wsm::ScanState::Found && result.candidates == 1);
    assert(wsm::scan_aob(span, pattern, {5, 4, 1, 10}).state == wsm::ScanState::InvalidRange);
    assert(wsm::scan_aob(span, pattern, {0, 7, 1, 10}).state == wsm::ScanState::InvalidRange);
    assert(wsm::scan_aob(span, pattern, {0, SIZE_MAX, 3, 10}).state == wsm::ScanState::InvalidRange);
    assert(wsm::scan_aob(span, pattern, {0, SIZE_MAX, 0, 10}).state == wsm::ScanState::InvalidRange);
    assert(wsm::scan_aob(span, pattern, {0, SIZE_MAX, 1, 0}).state == wsm::ScanState::InvalidRange);
    assert(wsm::scan_aob(span, pattern, {0, SIZE_MAX, 1, wsm::kMaxAobCandidates + 1}).state == wsm::ScanState::InvalidRange);
    assert(wsm::scan_aob({nullptr, 6, 0x1000}, pattern).state == wsm::ScanState::InvalidSpan);
    assert(wsm::scan_aob({bytes, 6, UINTPTR_MAX - 3}, pattern).state == wsm::ScanState::InvalidSpan);
    assert(wsm::scan_aob({nullptr, 0, 0x1000}, pattern).state == wsm::ScanState::Missing);
    assert(wsm::scan_aob(span, {}).state == wsm::ScanState::InvalidPattern);
    const uint8_t masked[]{0x00, 0xab, 0xc9, 0x2f, 0xf1, 0x42};
    result = wsm::scan_aob({masked, sizeof masked, 0x2000}, wsm::parse_aob("AB C? ?F ?? 42").pattern);
    assert(result.state == wsm::ScanState::Found && result.offset == 1);
    const uint8_t overlap[]{0xaa, 0xaa, 0xaa};
    assert(wsm::scan_aob({overlap, sizeof overlap, 0x3000}, wsm::parse_aob("AA AA").pattern).state == wsm::ScanState::Ambiguous);
    const uint8_t last[]{0, 0, 0xaa, 0xbb};
    assert(wsm::scan_aob({last, sizeof last, 0x4000}, pattern).offset == 2);
}

struct NativeFixture { void *expected; uintptr_t address; unsigned calls = 0; };
static uintptr_t native_pointer(wsm::MethodInfoRef method, void *context) {
    auto *fixture = static_cast<NativeFixture *>(context); ++fixture->calls;
    assert(method.value == fixture->expected); return fixture->address;
}
static bool executable_pointer(wsm::NativeMethodPointer pointer, void *) {
    return pointer.address >= 0x8000 && pointer.address < 0x9000 && pointer.address % 4 == 0;
}
static void hybrid_cases() {
    static_assert(!std::is_convertible<wsm::MethodInfoRef, wsm::NativeMethodPointer>::value, "metadata and code must stay distinct");
    Fixture fixture;
    const auto runtime = api(); const auto contract = wanted();
    const auto expected = wsm::supported_identity();
    const uint8_t bytes[]{0x00, 0xaa, 0xbb, 0x00};
    const auto pattern = wsm::parse_aob("AA BB").pattern;
    const wsm::AobRequest discovery{&pattern, {bytes, sizeof bytes, 0x5000}, {}};
    auto result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected);
    assert(result.qualified && result.identity_verified && result.method_info.value == &fixture.rows[1] && !result.native_pointer.address);
    NativeFixture native_fixture{&fixture.rows[1], 0x8100};
    const wsm::NativePointerApi native{native_pointer, executable_pointer, &native_fixture};
    result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, &native, &discovery);
    assert(result.qualified && result.native_pointer.address == 0x8100 && result.discovery.address == 0x5001);
    assert(result.native_pointer.address != result.discovery.address);
    native_fixture.address = 0x8101;
    result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, &native, &discovery);
    assert(result.state == wsm::HybridState::NativeRejected && !result.qualified && !result.method_info.value && !result.native_pointer.address);
    native_fixture.address = 0;
    assert(wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, &native).state == wsm::HybridState::NativeRejected);
    const wsm::NativePointerApi incomplete{};
    assert(wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, &incomplete).state == wsm::HybridState::NativeUnavailable);
    const wsm::IdentityContract changes[] = {
        {expected.package, "3.54.1", expected.code}, {expected.package, expected.version, 424},
        {"other.package", expected.version, expected.code}, {nullptr, expected.version, expected.code}
    };
    const unsigned prior_calls = native_fixture.calls;
    fixture.enumerated = 0;
    for (const auto &identity : changes) {
        result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, identity, &native, &discovery);
        assert(result.state == wsm::HybridState::IdentityMismatch && !result.qualified && !result.identity_verified);
        assert(!result.method_info.value && !result.native_pointer.address && result.discovery.state == wsm::ScanState::NotRequested);
    }
    assert(!fixture.enumerated && prior_calls == native_fixture.calls);
    // An early unverified observation must not poison a later verified retry.
    result=wsm::resolve_hybrid(runtime,&fixture,contract,expected,expected);
    assert(result.qualified && result.method_info.value==&fixture.rows[1]);
    assert(fixture.enumerated>0);

    fixture.rows[1].name = "Changed";
    result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, nullptr, &discovery);
    assert(result.state == wsm::HybridState::MetadataMissing && result.discovery.state == wsm::ScanState::Found);
    assert(!result.qualified && !result.method_info.value && !result.native_pointer.address);
    fixture.rows[1].name = "GetBattleFor"; fixture.rows[0].params[0] = "Oak.IFieldObject";
    result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, nullptr, &discovery);
    assert(result.state == wsm::HybridState::MetadataAmbiguous && !result.qualified && !result.method_info.value);
    const uint8_t ambiguous[]{0xaa, 0xbb, 0xaa, 0xbb};
    const wsm::AobRequest multiple{&pattern, {ambiguous, sizeof ambiguous, 0x5000}, {}};
    result = wsm::resolve_hybrid(runtime, &fixture, contract, expected, expected, nullptr, &multiple);
    assert(result.discovery.state == wsm::ScanState::Ambiguous && !result.qualified && !result.native_pointer.address);
    assert(wsm::resolve_hybrid(runtime, &fixture, contract, {"", expected.version, 423}, expected).state == wsm::HybridState::InvalidContract);
    assert(wsm::identity_matches(expected.package, expected.version, expected.code));
    assert(!wsm::identity_matches(expected.package, expected.version, 424));
    assert(!wsm::identity_matches(expected.package, "3.54.1", 423));
    assert(allocated == released);
}
int main() {
    binding_cases(); pattern_cases(); scanner_cases(); hybrid_cases();
    puts("PASS resolver: typed metadata, bounded masked scan, exact identity, ambiguity/budget rejection, native validation, diagnostic-only AOB");
}

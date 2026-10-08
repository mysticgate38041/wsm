#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wsm {
constexpr size_t kMaxAobBytes = 256;
constexpr size_t kMaxAobText = 2048;
constexpr size_t kMaxAobCandidates = 1u << 20;
struct MaskedByte { uint8_t value = 0; uint8_t mask = 0; };
struct MaskedPattern { MaskedByte bytes[kMaxAobBytes]{}; size_t length = 0; };
enum class PatternState { Parsed, Empty, InvalidToken, TooLong, Unconstrained };
struct PatternResult { MaskedPattern pattern{}; PatternState state = PatternState::Empty; size_t error_offset = 0; };
// Whitespace-separated bytes: AB, A?, ?B, ?? or ?. No regex or escapes.
PatternResult parse_aob(const char *text);
bool valid_pattern(const MaskedPattern &pattern);

// data is an explicitly supplied readable buffer. address only labels offsets;
// the scanner never enumerates mappings, reads files or dereferences address.
struct ByteSpan { const uint8_t *data = nullptr; size_t size = 0; uintptr_t address = 0; };
struct ScanRange {
    size_t begin = 0;
    size_t end = SIZE_MAX; // exclusive; SIZE_MAX means exactly span.size
    size_t alignment = 1; // absolute address alignment, power of two
    size_t candidate_limit = kMaxAobCandidates;
};
enum class ScanState { NotRequested, Found, Missing, Ambiguous, InvalidPattern, InvalidSpan, InvalidRange, Truncated };
struct ScanResult {
    ScanState state = ScanState::NotRequested;
    uintptr_t address = 0;
    size_t offset = 0;
    size_t matches = 0; // ambiguous terminates at two, not total occurrence count
    size_t candidates = 0;
};
ScanResult scan_aob(ByteSpan span, const MaskedPattern &pattern, ScanRange range = {});
const char *scan_state_name(ScanState state);
}

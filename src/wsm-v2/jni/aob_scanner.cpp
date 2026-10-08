#include "aob_scanner.h"

namespace wsm {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool nibble(char c, uint8_t &value, uint8_t &mask) {
    if (c == '?') { value = 0; mask = 0; return true; }
    const int number = hex(c);
    if (number < 0) return false;
    value = static_cast<uint8_t>(number); mask = 0xf; return true;
}
ScanResult failure(ScanState state) { ScanResult result{}; result.state = state; return result; }
}
bool valid_pattern(const MaskedPattern &pattern) {
    if (!pattern.length || pattern.length > kMaxAobBytes) return false;
    bool constrained = false;
    for (size_t i = 0; i < pattern.length; ++i) {
        const uint8_t mask = pattern.bytes[i].mask;
        if (mask != 0 && mask != 0x0f && mask != 0xf0 && mask != 0xff) return false;
        if ((pattern.bytes[i].value & static_cast<uint8_t>(~mask)) != 0) return false;
        constrained = constrained || mask != 0;
    }
    return constrained;
}
PatternResult parse_aob(const char *text) {
    PatternResult result{};
    if (!text) return result;
    size_t length = 0;
    while (length < kMaxAobText && text[length]) ++length;
    if (length == kMaxAobText) { result.state = PatternState::TooLong; result.error_offset = length; return result; }
    size_t at = 0;
    while (at < length) {
        while (at < length && space(text[at])) ++at;
        if (at == length) break;
        const size_t begin = at;
        while (at < length && !space(text[at])) ++at;
        const size_t token = at - begin;
        if ((token != 1 && token != 2) || (token == 1 && text[begin] != '?')) {
            result.state = PatternState::InvalidToken; result.error_offset = begin; result.pattern.length = 0; return result;
        }
        if (result.pattern.length == kMaxAobBytes) {
            result.state = PatternState::TooLong; result.error_offset = begin; result.pattern.length = 0; return result;
        }
        MaskedByte byte{};
        if (token == 2) {
            uint8_t hi = 0, lo = 0, hi_mask = 0, lo_mask = 0;
            if (!nibble(text[begin], hi, hi_mask) || !nibble(text[begin + 1], lo, lo_mask)) {
                result.state = PatternState::InvalidToken; result.error_offset = begin; result.pattern.length = 0; return result;
            }
            byte.value = static_cast<uint8_t>((hi << 4) | lo);
            byte.mask = static_cast<uint8_t>((hi_mask << 4) | lo_mask);
        }
        result.pattern.bytes[result.pattern.length++] = byte;
    }
    result.state = !result.pattern.length ? PatternState::Empty :
        valid_pattern(result.pattern) ? PatternState::Parsed : PatternState::Unconstrained;
    return result;
}
ScanResult scan_aob(ByteSpan span, const MaskedPattern &pattern, ScanRange range) {
    if (!valid_pattern(pattern)) return failure(ScanState::InvalidPattern);
    if ((span.size && !span.data) || span.size > UINTPTR_MAX - span.address) return failure(ScanState::InvalidSpan);
    const size_t end = range.end == SIZE_MAX ? span.size : range.end;
    if (range.begin > end || end > span.size || !range.alignment ||
        (range.alignment & (range.alignment - 1)) || !range.candidate_limit ||
        range.candidate_limit > kMaxAobCandidates) return failure(ScanState::InvalidRange);
    ScanResult result = failure(ScanState::Missing);
    if (pattern.length > end - range.begin) return result;
    const size_t last = end - pattern.length;
    const uintptr_t first_address = span.address + range.begin;
    const size_t remainder = static_cast<size_t>(first_address & (range.alignment - 1));
    const size_t adjustment = remainder ? range.alignment - remainder : 0;
    if (adjustment > last - range.begin) return result;
    size_t at = range.begin + adjustment;
    while (at <= last) {
        if (result.candidates == range.candidate_limit) {
            result.state = ScanState::Truncated; result.address = 0; result.offset = 0; return result;
        }
        ++result.candidates;
        bool matches = true;
        for (size_t i = 0; matches && i < pattern.length; ++i)
            matches = (span.data[at + i] & pattern.bytes[i].mask) == pattern.bytes[i].value;
        if (matches) {
            ++result.matches;
            if (result.matches > 1) {
                result.state = ScanState::Ambiguous; result.address = 0; result.offset = 0; return result;
            }
            result.address = span.address + at; result.offset = at;
        }
        if (range.alignment > last - at) break;
        at += range.alignment;
    }
    result.state = result.matches == 1 ? ScanState::Found : ScanState::Missing;
    return result;
}
const char *scan_state_name(ScanState state) {
    switch (state) {
        case ScanState::NotRequested: return "not-requested";
        case ScanState::Found: return "found";
        case ScanState::Missing: return "missing";
        case ScanState::Ambiguous: return "ambiguous";
        case ScanState::InvalidPattern: return "invalid-pattern";
        case ScanState::InvalidSpan: return "invalid-span";
        case ScanState::InvalidRange: return "invalid-range";
        case ScanState::Truncated: return "truncated";
    }
    return "unknown";
}
}

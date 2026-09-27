#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bts {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> exact;
};

struct Patch {
    std::string name;
    Pattern pattern;
    size_t offset = 0;
    std::vector<uint8_t> value;
};

struct Write {
    size_t offset;
    std::vector<uint8_t> value;
};

inline bool whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline std::string_view trim(std::string_view text) {
    while (!text.empty() && whitespace(text.front())) text.remove_prefix(1);
    while (!text.empty() && whitespace(text.back())) text.remove_suffix(1);
    return text;
}

inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// One grammar shared by the DLL, command-line validator, and tests.
inline bool parse_bytes(std::string_view text, bool wildcards, Pattern& out, std::string& error) {
    out = {};
    text = trim(text);
    while (!text.empty()) {
        if (out.bytes.size() == 4096) { error = "pattern exceeds 4096 bytes"; return false; }
        if (text.size() < 2) { error = "incomplete hex byte"; return false; }
        if (wildcards && text.substr(0, 2) == "??") {
            out.bytes.push_back(0);
            out.exact.push_back(0);
        } else {
            int high = hex_digit(text[0]), low = hex_digit(text[1]);
            if (high < 0 || low < 0) { error = "invalid hex byte"; return false; }
            out.bytes.push_back(static_cast<uint8_t>((high << 4) | low));
            out.exact.push_back(1);
        }
        text.remove_prefix(2);
        if (!text.empty() && !whitespace(text.front())) {
            error = "hex bytes must be separated by whitespace"; return false;
        }
        text = trim(text);
    }
    if (out.bytes.empty()) { error = "empty byte sequence"; return false; }
    if (wildcards && std::none_of(out.exact.begin(), out.exact.end(), [](uint8_t v) { return v != 0; })) {
        error = "signature must contain a literal byte"; return false;
    }
    return true;
}

inline bool parse_number(std::string_view text, size_t& value) {
    text = trim(text);
    if (text.empty() || text.front() == '-' || text.front() == '+') return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

inline bool unique_match(std::span<const uint8_t> data, const Pattern& pattern,
                         size_t& position, std::string& error) {
    const size_t length = pattern.bytes.size();
    if (!length || length != pattern.exact.size() || length > data.size()) {
        error = "signature not found"; return false;
    }
    bool found = false;
    for (size_t i = 0; i <= data.size() - length; ++i) {
        size_t j = 0;
        while (j < length && (!pattern.exact[j] || data[i + j] == pattern.bytes[j])) ++j;
        if (j != length) continue;
        if (found) { error = "signature is ambiguous"; return false; }
        position = i;
        found = true;
    }
    if (!found) error = "signature not found";
    return found;
}

// Resolve all signatures against the original input. No caller memory changes
// until every match, range, and overlap has been checked.
inline bool plan(std::span<const uint8_t> data, std::span<const Patch> patches,
                 std::vector<Write>& writes, std::string& error) {
    writes.clear();
    if (patches.empty()) { error = "no patches configured"; return false; }
    for (const auto& patch : patches) {
        size_t match = 0;
        if (!unique_match(data, patch.pattern, match, error)) {
            error = patch.name + ": " + error; writes.clear(); return false;
        }
        if (patch.value.empty() || patch.offset > data.size() - match ||
            patch.value.size() > data.size() - match - patch.offset) {
            error = patch.name + ": replacement is outside the buffer"; writes.clear(); return false;
        }
        const size_t start = match + patch.offset, end = start + patch.value.size();
        for (const auto& prior : writes) {
            if (start < prior.offset + prior.value.size() && prior.offset < end) {
                error = patch.name + ": overlapping replacements"; writes.clear(); return false;
            }
        }
        writes.push_back({start, patch.value});
    }
    return true;
}

inline bool apply(std::span<uint8_t> data, std::span<const Patch> patches, std::string& error) {
    std::vector<Write> writes;
    if (!plan(data, patches, writes, error)) return false;
    for (const auto& write : writes)
        std::copy(write.value.begin(), write.value.end(), data.begin() + write.offset);
    return true;
}

} // namespace bts

#pragma once
#include "patch.h"
#include <map>
#include <set>

namespace bts {

inline std::string lower(std::string_view value) {
    std::string result(value);
    for (char& c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}

inline bool mod_filename(std::string_view name) {
    auto folded = lower(name);
    return name.size() > 4 && name.size() <= 128 &&
        (folded.ends_with(".ini") || folded.ends_with(".dll")) &&
        name.find_first_of("/\\:") == std::string_view::npos &&
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}

struct Ini {
    using Section = std::map<std::string, std::string>;
    std::map<std::string, Section> sections;

    const std::string* get(std::string_view section, std::string_view key) const {
        auto s = sections.find(lower(section));
        if (s == sections.end()) return nullptr;
        auto k = s->second.find(lower(key));
        return k == s->second.end() ? nullptr : &k->second;
    }
};

inline bool parse_ini(std::string_view source, Ini& ini, std::string& error) {
    ini = {};
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    if (source.size() > 1024 * 1024 || source.find('\0') != std::string_view::npos) {
        error = "INI must be UTF-8 text under 1 MiB"; return false;
    }
    std::string section;
    size_t line = 0;
    while (!source.empty()) {
        ++line;
        size_t end = source.find('\n');
        auto text = trim(source.substr(0, end));
        source.remove_prefix(end == std::string_view::npos ? source.size() : end + 1);
        if (text.empty() || text.front() == ';' || text.front() == '#') continue;
        if (text.front() == '[' && text.back() == ']' && text.size() > 2) {
            section = lower(trim(text.substr(1, text.size() - 2)));
            ini.sections.try_emplace(section);
            continue;
        }
        size_t equal = text.find('=');
        if (section.empty() || equal == std::string_view::npos || trim(text.substr(0, equal)).empty()) {
            error = "invalid INI line " + std::to_string(line); return false;
        }
        auto key = lower(trim(text.substr(0, equal)));
        if (!ini.sections[section].emplace(key, trim(text.substr(equal + 1))).second) {
            error = "duplicate key " + section + "/" + key; return false;
        }
    }
    return true;
}

inline bool numbered(const Ini& ini, const std::string& section, size_t limit,
                     std::vector<std::string>& entries, std::string& error) {
    entries.clear();
    auto found = ini.sections.find(lower(section));
    if (found == ini.sections.end()) { error = "missing section " + section; return false; }
    std::map<size_t, std::string> values;
    for (const auto& [key, value] : found->second) {
        if (key == "enable") continue;
        size_t number;
        if (!parse_number(key, number) || !number || number > limit || value.empty() ||
            !values.emplace(number, value).second) {
            error = "invalid numbered entry in " + section; return false;
        }
    }
    for (const auto& [number, value] : values) {
        if (number != entries.size() + 1) { error = "gap in " + section; return false; }
        entries.push_back(value);
    }
    return true;
}

inline bool read_patch(const Ini& ini, const std::string& section, const std::string& suffix,
                       Patch& patch, std::string& error) {
    patch = {};
    patch.name = section + suffix;
    auto signature = ini.get(section, "Signature" + suffix);
    auto value = ini.get(section, "Value" + suffix);
    auto offset = ini.get(section, "Offset" + suffix);
    if (!signature || !value || !offset) { error = patch.name + ": missing signature, value, or offset"; return false; }
    Pattern replacement;
    if (!parse_bytes(*signature, true, patch.pattern, error) ||
        !parse_bytes(*value, false, replacement, error)) {
        error = patch.name + ": " + error; return false;
    }
    if (!parse_number(*offset, patch.offset)) { error = patch.name + ": invalid unsigned offset"; return false; }
    patch.value = std::move(replacement.bytes);
    return true;
}

struct FilePatch { std::string file; std::vector<Patch> patches; };
struct Config {
    std::string spotify_version;
    int log_level = 0;
    bool block_crashpad = true;
    bool mods_enabled = true;
    size_t url_offset = 48, read_offset = 112, name_offset = 72;
};

inline bool load_config(const Ini& pack, Config& config, std::string& error) {
    config = {};
    error.clear();
    for (const auto& [section, entries] : pack.sections) for (const auto& [key, unused] : entries) {
        const bool allowed = (section == "compatibility" && key == "spotify") ||
            (section == "log" && key == "level") || (section == "mods" && key == "enable") ||
            (section == "libcef" && (key == "block_crashpad" || key == "cef_request_get_url_offset" ||
                key == "cef_zip_reader_get_read_file_offset" || key == "cef_zip_reader_get_file_name_offset"));
        if (!allowed) {
            error = "unsupported host setting " + section + "/" + key + "; mod settings belong in patches/<mod>.ini";
            return false;
        }
    }
    const auto* version = pack.get("Compatibility", "Spotify");
    if (!version || version->empty()) { error = "missing Compatibility/Spotify version"; return false; }
    config.spotify_version = *version;
    auto option = [&](const char* section, const char* key, size_t fallback, size_t max, size_t& result) {
        auto value = pack.get(section, key);
        result = fallback;
        if (value && (!parse_number(*value, result) || result > max)) {
            error = std::string(section) + "/" + key + ": invalid setting"; return false;
        }
        return true;
    };
    size_t value;
    if (!option("Log", "Level", 0, 2, value)) return false;
    config.log_level = static_cast<int>(value);
    if (!option("Mods", "Enable", 1, 1, value)) return false;
    config.mods_enabled = value != 0;
    if (!option("LIBCEF", "Block_crashpad", 1, 1, value)) return false;
    config.block_crashpad = value != 0;
    // ABI offsets belong to the host compatibility configuration.
    for (auto entry : {std::pair{"CEF_REQUEST_GET_URL_OFFSET", &config.url_offset},
        std::pair{"CEF_ZIP_READER_GET_READ_FILE_OFFSET", &config.read_offset},
        std::pair{"CEF_ZIP_READER_GET_FILE_NAME_OFFSET", &config.name_offset}}) {
        auto text = pack.get("LIBCEF", entry.first);
        if (text && (!parse_number(*text, *entry.second) || *entry.second < 40 ||
            *entry.second > 1024 || *entry.second % 8)) {
            error = std::string(entry.first) + ": invalid x64 member offset"; return false;
        }
    }
    return true;
}

} // namespace bts

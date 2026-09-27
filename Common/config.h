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
    bool developer_enabled = true, urls_enabled = true, buffers_enabled = true, css_enabled = false;
    bool block_crashpad = true;
    bool mods_enabled = true;
    std::map<std::string, bool> mod_overrides;
    size_t url_offset = 48, read_offset = 112, name_offset = 72;
    Patch developer, css;
    std::vector<std::string> urls;
    std::vector<FilePatch> files;
};

inline bool load_config(const Ini& pack, const Ini& settings, Config& config, std::string& error) {
    config = {};
    error.clear();
    for (const auto& [section, entries] : settings.sections) {
        for (const auto& [key, unused] : entries) {
            bool allowed = (section == "mods" && (key == "enable" || mod_filename(key))) ||
                (section == "log" && key == "level") ||
                (section == "libcef" && key == "block_crashpad") ||
                ((section == "developer" || section == "url_block" ||
                  section == "buffer_modify" || section == "homepage_vbar") && key == "enable");
            if (!allowed) { error = "unsupported preference " + section + "/" + key; return false; }
        }
    }
    const auto* version = pack.get("Compatibility", "Spotify");
    if (!version || version->empty()) { error = "missing Compatibility/Spotify version"; return false; }
    config.spotify_version = *version;
    auto option = [&](const char* section, const char* key, size_t fallback, size_t max, size_t& result) {
        auto value = settings.get(section, key);
        if (!value) value = pack.get(section, key);
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
    if (auto mods = settings.sections.find("mods"); mods != settings.sections.end()) {
        for (const auto& [file, flag] : mods->second) {
            if (file == "enable") continue;
            if (!parse_number(flag, value) || value > 1) { error = "Mods/" + file + ": expected 0 or 1"; return false; }
            config.mod_overrides[file] = value != 0;
        }
    }
    struct Flag { const char* section; const char* key; bool* field; };
    for (const auto& flag : {Flag{"Developer", "Enable", &config.developer_enabled},
        Flag{"URL_block", "Enable", &config.urls_enabled}, Flag{"Buffer_modify", "Enable", &config.buffers_enabled},
        Flag{"Homepage_vbar", "Enable", &config.css_enabled}, Flag{"LIBCEF", "Block_crashpad", &config.block_crashpad}}) {
        if (!option(flag.section, flag.key, *flag.field, 1, value)) return false;
        *flag.field = value != 0;
    }
    // ABI offsets belong to the compatibility pack, never user preferences.
    for (auto entry : {std::pair{"CEF_REQUEST_GET_URL_OFFSET", &config.url_offset},
        std::pair{"CEF_ZIP_READER_GET_READ_FILE_OFFSET", &config.read_offset},
        std::pair{"CEF_ZIP_READER_GET_FILE_NAME_OFFSET", &config.name_offset}}) {
        auto text = pack.get("LIBCEF", entry.first);
        if (text && (!parse_number(*text, *entry.second) || *entry.second < 40 ||
            *entry.second > 1024 || *entry.second % 8)) {
            error = std::string(entry.first) + ": invalid x64 member offset"; return false;
        }
    }
    if (!read_patch(pack, "Developer", "", config.developer, error) ||
        !read_patch(pack, "Homepage_vbar", "", config.css, error) ||
        !numbered(pack, "URL_block", 256, config.urls, error)) return false;
    for (const auto& url : config.urls) {
        if (url.front() != '/' || url.find_first_of("?#\r\n") != std::string::npos ||
            std::any_of(url.begin(), url.end(), [](unsigned char c) { return c > 127; })) {
            error = "URL rules must be ASCII path substrings without queries"; return false;
        }
    }
    std::vector<std::string> files;
    if (!numbered(pack, "Buffer_modify", 256, files, error)) return false;
    std::set<std::string> seen;
    for (const auto& file : files) {
        if (file.find_first_of("/\\:") != std::string::npos || !seen.insert(lower(file)).second) {
            error = "invalid or duplicate SPA filename"; return false;
        }
        FilePatch target{file, {}};
        std::vector<std::string> names;
        if (!numbered(pack, file, 256, names, error) || names.empty()) {
            if (error.empty()) error = file + ": no patches";
            return false;
        }
        for (const auto& name : names) {
            auto section = pack.sections.find(lower(name));
            if (section == pack.sections.end()) { error = "missing patch section " + name; return false; }
            for (const auto& [key, unused] : section->second) {
                if (key != "signature_1" && key != "offset_1" && key != "value_1" &&
                    key != "signature_2" && key != "offset_2" && key != "value_2") {
                    error = name + ": unknown patch field " + key; return false;
                }
            }
            bool found = false;
            for (size_t i = 1; i <= 2; ++i) {
                std::string suffix = "_" + std::to_string(i);
                if (i == 2 && !pack.get(name, "Signature_2") &&
                    !pack.get(name, "Value_2") && !pack.get(name, "Offset_2")) break;
                Patch patch;
                if (!read_patch(pack, name, suffix, patch, error)) return false;
                target.patches.push_back(std::move(patch));
                found = true;
            }
            if (!found || pack.get(name, "Signature_3")) { error = name + ": expected one or two signatures"; return false; }
        }
        config.files.push_back(std::move(target));
    }
    return true;
}

} // namespace bts

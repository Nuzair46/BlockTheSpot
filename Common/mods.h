#pragma once
#include "config.h"
#include <filesystem>

namespace bts {

struct IniMod {
    std::string spotify_version;
    bool enabled = true;
    std::vector<FilePatch> native;
    std::vector<FilePatch> files;
};

inline bool ini_flag(const Ini& ini, const std::string& section, bool& flag, std::string& error) {
    if (const auto* text = ini.get(section, "Enable")) {
        size_t value;
        if (!parse_number(*text, value) || value > 1) { error = section + "/Enable: expected 0 or 1"; return false; }
        flag = value != 0;
    }
    return true;
}

inline bool load_ini_mod(const Ini& ini, IniMod& mod, std::string& error) {
    mod = {};
    error.clear();
    if (!ini_flag(ini, "Mod", mod.enabled, error)) return false;
    if (!mod.enabled) return true;
    auto version = ini.get("Compatibility", "Spotify");
    if (!version || version->empty()) { error = "missing Compatibility/Spotify version"; return false; }
    mod.spotify_version = *version;
    std::vector<std::string> names;
    bool native_enabled = true;
    if (!ini_flag(ini, "NativePatches", native_enabled, error)) return false;
    if (ini.sections.contains("nativepatches") && native_enabled) {
        if (!numbered(ini, "NativePatches", 256, names, error)) return false;
        std::set<std::string> seen;
        for (const auto& name : names) {
            if (!seen.insert(lower(name)).second) { error = "duplicate native patch " + name; return false; }
            auto section = ini.sections.find(lower(name));
            if (section == ini.sections.end()) { error = "missing patch section " + name; return false; }
            for (const auto& [key, unused] : section->second) {
                if (key != "enable" && key != "module" && key != "signature" && key != "offset" && key != "value") {
                    error = name + ": unknown native patch field " + key; return false;
                }
            }
            bool enabled = true;
            if (!ini_flag(ini, name, enabled, error)) return false;
            if (!enabled) continue;
            std::string module = "Spotify.dll";
            if (const auto* value = ini.get(name, "Module")) module = *value;
            // Only patch already-loaded application code, never a path supplied by a mod.
            if (lower(module) != "spotify.dll" && lower(module) != "spotify.exe") {
                error = name + ": Module must be Spotify.dll or Spotify.exe"; return false;
            }
            Patch patch;
            if (!read_patch(ini, name, "", patch, error)) return false;
            auto target = std::find_if(mod.native.begin(), mod.native.end(), [&](const auto& f) { return lower(f.file) == lower(module); });
            if (target == mod.native.end()) { mod.native.push_back({module, {}}); target = std::prev(mod.native.end()); }
            target->patches.push_back(std::move(patch));
        }
    }
    if (ini.sections.contains("buffer_modify")) {
        bool enabled = true;
        if (!ini_flag(ini, "Buffer_modify", enabled, error)) return false;
        if (enabled) {
            std::vector<std::string> files;
            if (!numbered(ini, "Buffer_modify", 256, files, error)) return false;
            std::set<std::string> seen;
            for (const auto& file : files) {
                if (file.empty() || file.find_first_of("/\\:\r\n") != std::string::npos || !seen.insert(lower(file)).second) {
                    error = "invalid or duplicate SPA filename"; return false;
                }
                FilePatch target{file, {}};
                if (!numbered(ini, file, 256, names, error) || names.empty()) { if (error.empty()) error = file + ": no patches"; return false; }
                for (const auto& name : names) {
                    for (size_t i = 1; i <= 2; ++i) {
                        auto suffix = "_" + std::to_string(i);
                        if (i == 2 && !ini.get(name, "Signature_2") && !ini.get(name, "Value_2") && !ini.get(name, "Offset_2")) break;
                        Patch patch;
                        if (!read_patch(ini, name, suffix, patch, error)) return false;
                        target.patches.push_back(std::move(patch));
                    }
                    const auto& section = ini.sections.at(lower(name));
                    for (const auto& [key, unused] : section) {
                        if (key != "signature_1" && key != "offset_1" && key != "value_1" &&
                            key != "signature_2" && key != "offset_2" && key != "value_2") {
                            error = name + ": unknown patch field " + key; return false;
                        }
                    }
                }
                mod.files.push_back(std::move(target));
            }
        }
    }
    if (!ini.sections.contains("nativepatches") && !ini.sections.contains("buffer_modify")) {
        error = "INI mod needs NativePatches or Buffer_modify; DLL settings need a same-named DLL"; return false;
    }
    return true;
}

struct ModFile {
    std::filesystem::path path;
    std::filesystem::path settings;
    std::string id;
    bool dll = false;
};

// Input is a flat directory listing. A same-stem INI belongs to its DLL, so it
// is never also interpreted as a declarative patch pack.
inline std::vector<ModFile> group_mod_files(std::vector<std::filesystem::path> paths) {
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    std::set<std::string> dlls;
    for (const auto& path : paths) if (lower(path.extension().string()) == ".dll") dlls.insert(lower(path.stem().string()));
    std::vector<ModFile> result;
    for (const auto& path : paths) {
        const auto id = lower(path.filename().string());
        bool dll = lower(path.extension().string()) == ".dll";
        if (!mod_filename(id) || (!dll && dlls.contains(lower(path.stem().string())))) continue;
        ModFile mod{path, {}, id, dll};
        if (dll) {
            for (const auto& candidate : paths) {
                if (lower(candidate.filename().string()) == lower(path.stem().string()) + ".ini") { mod.settings = candidate; break; }
            }
        }
        result.push_back(std::move(mod));
    }
    return result;
}

struct PatchGroup { std::string owner; std::vector<Patch> patches; };
struct GroupResult { std::string owner; std::string error; };

// Resolve every group against the same original bytes. A failing/conflicting
// group contributes no writes; earlier valid groups keep precedence.
inline std::vector<GroupResult> plan_groups(std::span<const uint8_t> data, const std::vector<PatchGroup>& groups,
                                         std::vector<Write>& accepted) {
    accepted.clear();
    std::vector<GroupResult> results;
    for (const auto& group : groups) {
        std::vector<Write> proposed;
        std::string error;
        if (plan(data, group.patches, proposed, error)) {
            for (const auto& next : proposed) {
                for (const auto& prior : accepted) {
                    if (next.offset < prior.offset + prior.value.size() && prior.offset < next.offset + next.value.size()) {
                        error = "write conflicts with an earlier patch group"; break;
                    }
                }
                if (!error.empty()) break;
            }
            if (error.empty()) accepted.insert(accepted.end(), proposed.begin(), proposed.end());
        }
        results.push_back({group.owner, error});
    }
    return results;
}

} // namespace bts

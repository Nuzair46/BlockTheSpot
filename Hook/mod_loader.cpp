#include "pch.h"
#include "mod_loader.h"
#include "log_thread.h"
#include "pattern.h"
#include "memory.h"
#include "../Common/windows.h"
#include "../include/blockthespot_mod.h"
#include <memory>

namespace {
struct LoadedIni { std::string id; bts::IniMod config; };
struct LoadedDll {
    bts::ModFile file;
    std::wstring directory, config_path;
    std::string version;
    BtsModHost host{};
    HMODULE module = nullptr;
    std::unique_ptr<bts::IniMod> registration;
    bool initializing = false;
};
std::vector<LoadedIni> ini_mods;
std::vector<std::unique_ptr<LoadedDll>> dll_mods;
bool discovered = false, dlls_loaded = false;

std::string status_name(const std::string& id) { return "Mod " + id; }

void __cdecl mod_log(void* context, int level, const char* message) {
    if (!context || !message) return;
    const auto& mod = *static_cast<LoadedDll*>(context);
    std::string text = "[" + mod.file.id + "] " + std::string(message);
    if (level <= 0) log_error(text.c_str());
    else if (level == 1) log_info(text.c_str());
    else log_debug(text.c_str());
}

int __cdecl register_ini(void* context, const char* source, size_t length) {
    if (!context || !source || length > 1024 * 1024) return 0;
    auto& mod = *static_cast<LoadedDll*>(context);
    if (!mod.initializing || mod.registration) return 0;
    bts::Ini ini;
    auto parsed = std::make_unique<bts::IniMod>();
    std::string error;
    if (!bts::parse_ini({source, length}, ini, error) || !bts::load_ini_mod(ini, *parsed, error)) {
        mod_log(context, 0, error.c_str()); return 0;
    }
    if (!parsed->enabled || parsed->spotify_version != mod.version) {
        mod_log(context, 0, "registered pack is disabled or targets another Spotify version"); return 0;
    }
    mod.registration = std::move(parsed);
    return 1;
}

void report_results(const std::vector<bts::GroupResult>& results, bool written) {
    for (const auto& result : results) {
        if (!result.error.empty()) set_status(result.owner, "failed", result.error);
        else if (!written) set_status(result.owner, "failed", "memory protection or instruction-cache update failed");
        else set_status(result.owner, "applied");
    }
}
}

void discover_mods(const std::string& spotify_version) {
    if (discovered) return;
    discovered = true;
    if (!runtime_config.mods_enabled || !compatible_spotify) {
        set_status("Mods", "skipped", compatible_spotify ? "disabled in config.ini" : "unsupported Spotify version"); return;
    }
    std::filesystem::path directory = std::filesystem::path(install_directory) / L"patches";
    DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) {
        set_status("Mods", "skipped", "no patches folder"); return;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        set_status("Mods", "failed", "patches must be a readable local directory, not a link"); return;
    }
    std::error_code ec;
    std::filesystem::directory_iterator entries(directory, ec), end;
    std::vector<std::filesystem::path> paths;
    while (!ec && entries != end) {
        const auto path = entries->path();
        const auto name = path.filename().wstring();
        // IDs are portable ASCII filenames. The installation path may be Unicode.
        if (std::all_of(name.begin(), name.end(), [](wchar_t c) { return c >= 32 && c < 127; })) {
            std::string id;
            for (wchar_t c : name) id += static_cast<char>(c);
            if (bts::mod_filename(id)) {
                DWORD attr = GetFileAttributesW(path.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES || (attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
                    set_status(status_name(id), "failed", "mod must be a regular file, not a link or directory");
                } else paths.push_back(path);
            }
        }
        if (paths.size() > 256) { set_status("Mods", "failed", "patches folder exceeds 256 DLL/INI files"); return; }
        entries.increment(ec);
    }
    if (ec) { set_status("Mods", "failed", "cannot enumerate patches: " + ec.message()); return; }
    for (const auto& file : bts::group_mod_files(std::move(paths))) {
        const auto label = status_name(file.id);
        if (file.id == "bts-loader.dll" || file.id == "chrome_elf.dll" || file.id == "chrome_elf_required.dll") {
            set_status(label, "failed", "core DLLs belong beside Spotify.exe, not in patches"); continue;
        }
        bts::Ini ini;
        std::string text, error;
        const auto& config = file.dll ? file.settings : file.path;
        if (!config.empty() && (!bts::read_text(config.wstring(), text) || !bts::parse_ini(text, ini, error))) {
            set_status(label, "failed", error.empty() ? "cannot read mod INI (UTF-8, at most 1 MiB)" : error); continue;
        }
        bool enabled = true;
        if (!bts::ini_flag(ini, "Mod", enabled, error)) { set_status(label, "failed", error); continue; }
        if (!enabled) { set_status(label, "skipped", "disabled by Mod/Enable"); continue; }
        auto version = ini.get("Compatibility", "Spotify");
        if (version && *version != spotify_version) {
            set_status(label, "skipped", "targets Spotify " + *version); continue;
        }
        if (!file.dll && ini.get("Mod", "Type") && bts::lower(*ini.get("Mod", "Type")) == "dll") {
            set_status(label, "skipped", "companion INI requires its same-named DLL"); continue;
        }
        if (file.dll) {
            auto mod = std::make_unique<LoadedDll>();
            mod->file = file;
            mod->directory = directory.wstring();
            mod->config_path = file.settings.wstring();
            mod->version = spotify_version;
            dll_mods.push_back(std::move(mod));
            set_status(label, "pending", "waiting for initialization");
        } else {
            bts::IniMod parsed;
            if (!bts::load_ini_mod(ini, parsed, error)) { set_status(label, "failed", error); continue; }
            if (parsed.native.empty() && parsed.files.empty() && parsed.urls.empty() && parsed.stylesheets.empty()) { set_status(label, "skipped", "no enabled patches"); continue; }
            ini_mods.push_back({file.id, std::move(parsed)});
            set_status(label, "ready", "INI registered; see per-target results");
        }
    }
    set_status("Mods", "ready", std::to_string(ini_mods.size()) + " INI and " + std::to_string(dll_mods.size()) + " DLL mods registered");
}

std::vector<bts::PatchGroup> frontend_mod_groups(const std::string& file, std::span<const uint8_t> bytes) {
    std::vector<bts::PatchGroup> groups;
    for (const auto& mod : ini_mods) {
        for (const auto& target : mod.config.files) if (target.file == file)
            groups.push_back({status_name(mod.id) + "/" + file, target.patches});
        for (const auto& target : mod.config.stylesheets) {
            if (!std::string_view(file).ends_with(target.extension)) continue;
            const auto& pattern = target.patch.pattern;
            if (bytes.size() < pattern.bytes.size()) continue;
            bool match = true;
            for (size_t i = 0; i < pattern.bytes.size(); ++i)
                if (pattern.exact[i] && bytes[i] != pattern.bytes[i]) { match = false; break; }
            if (match) groups.push_back({status_name(mod.id) + "/" + target.patch.name, {target.patch}});
        }
    }
    return groups;
}

bool has_frontend_mods() {
    return std::any_of(ini_mods.begin(), ini_mods.end(), [](const auto& mod) { return !mod.config.files.empty() || !mod.config.stylesheets.empty(); });
}

void report_frontend_mods(bool ready) {
    for (const auto& mod : ini_mods) {
        const auto report = [&](const std::string& target) {
            set_status(status_name(mod.id) + "/" + target, ready ? "pending" : "failed",
                ready ? "waiting for complete ZIP read" : "CEF reader hook unavailable");
        };
        for (const auto& file : mod.config.files) report(file.file);
        for (const auto& css : mod.config.stylesheets) report(css.patch.name);
    }
}

std::vector<UrlMod> url_mods() {
    std::vector<UrlMod> result;
    for (const auto& mod : ini_mods) if (!mod.config.urls.empty()) {
        UrlMod entry{status_name(mod.id) + "/URL_block", {}};
        for (const auto& rule : mod.config.urls) entry.rules.emplace_back(rule.begin(), rule.end());
        result.push_back(std::move(entry));
    }
    return result;
}

void apply_native_mods(HMODULE spotify) {
    for (const auto& module_name : {std::string("Spotify.dll"), std::string("Spotify.exe")}) {
        std::vector<bts::PatchGroup> groups;
        for (const auto& mod : ini_mods) for (const auto& target : mod.config.native)
            if (bts::lower(target.file) == bts::lower(module_name))
                groups.push_back({status_name(mod.id) + "/" + module_name, target.patches});
        if (groups.empty()) continue;
        DLL_section section{};
        HMODULE module = module_name == "Spotify.dll" ? spotify : GetModuleHandleW(nullptr);
        if (!get_text_section(module, &section)) {
            for (const auto& group : groups) set_status(group.owner, "failed", "missing .text section");
            continue;
        }
        std::vector<bts::Write> writes;
        auto results = bts::plan_groups({section.address, section.size}, groups, writes);
        bool written = writes.empty() || patch_instructions(section.address, section.size, writes);
        report_results(results, written);
    }
}

void load_dll_mods() {
    if (dlls_loaded) return;
    dlls_loaded = true;
    if (!runtime_config.mods_enabled || !compatible_spotify) return;
    for (auto& mod : dll_mods) {
        const auto label = status_name(mod->file.id);
        mod->module = LoadLibraryExW(mod->file.path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!mod->module) { set_status(label, "failed", "LoadLibraryExW error " + std::to_string(GetLastError())); continue; }
        mod->host = {sizeof(BtsModHost), BTS_MOD_API_VERSION, install_directory.c_str(), mod->directory.c_str(),
            mod->config_path.empty() ? nullptr : mod->config_path.c_str(), mod->version.c_str(), mod.get(), mod_log, register_ini};
        auto init = reinterpret_cast<BtsModInit>(GetProcAddress(mod->module, "bts_mod_init"));
        // Loaded modules and host contexts stay alive until process exit, even
        // if init fails: a plugin may already have registered hooks or workers.
        bool success = false;
        mod->initializing = true;
        try {
            success = !init || init(&mod->host);
            if (!success) set_status(label, "failed", "bts_mod_init returned failure");
        } catch (...) { set_status(label, "failed", "bts_mod_init threw an exception"); }
        mod->initializing = false;
        if (success) {
            if (mod->registration) ini_mods.push_back({mod->file.id, std::move(*mod->registration)});
            set_status(label, "loaded", init ? "API initialized" : "DllMain-only module");
        }
    }
    // DLL and standalone INI registrations share one precedence order.
    std::sort(ini_mods.begin(), ini_mods.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    set_status("Mods", "ready", std::to_string(ini_mods.size()) + " patch packs registered");
}

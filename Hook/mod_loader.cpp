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
        set_status("Mods", "skipped", compatible_spotify ? "disabled in settings.ini" : "unsupported Spotify version"); return;
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
        if (file.id == "blockthespot.dll" || file.id == "chrome_elf.dll" || file.id == "chrome_elf_required.dll") {
            set_status(label, "failed", "core DLLs belong beside Spotify.exe, not in patches"); continue;
        }
        auto override = runtime_config.mod_overrides.find(file.id);
        if (override != runtime_config.mod_overrides.end() && !override->second) {
            set_status(label, "skipped", "disabled in settings.ini"); continue;
        }
        bts::Ini ini;
        std::string text, error;
        const auto& config = file.dll ? file.settings : file.path;
        if (!config.empty() && (!bts::read_text(config.wstring(), text) || !bts::parse_ini(text, ini, error))) {
            set_status(label, "failed", error.empty() ? "cannot read mod INI (UTF-8, at most 1 MiB)" : error); continue;
        }
        if (override != runtime_config.mod_overrides.end()) ini.sections["mod"]["enable"] = "1";
        bool enabled = true;
        if (!bts::ini_flag(ini, "Mod", enabled, error)) { set_status(label, "failed", error); continue; }
        if (!enabled) { set_status(label, "skipped", "disabled by Mod/Enable"); continue; }
        auto version = ini.get("Compatibility", "Spotify");
        if (version && *version != spotify_version) {
            set_status(label, "skipped", "targets Spotify " + *version); continue;
        }
        if (file.dll) {
            auto mod = std::make_unique<LoadedDll>();
            mod->file = file;
            mod->directory = directory.wstring();
            mod->config_path = file.settings.wstring();
            mod->version = spotify_version;
            dll_mods.push_back(std::move(mod));
            set_status(label, "pending", "waiting for host hooks");
        } else {
            bts::IniMod parsed;
            if (!bts::load_ini_mod(ini, parsed, error)) { set_status(label, "failed", error); continue; }
            if (parsed.native.empty() && parsed.files.empty()) { set_status(label, "skipped", "no enabled patches"); continue; }
            ini_mods.push_back({file.id, std::move(parsed)});
            set_status(label, "ready", "INI registered; see per-target results");
        }
    }
    set_status("Mods", "ready", std::to_string(ini_mods.size()) + " INI and " + std::to_string(dll_mods.size()) + " DLL mods registered");
}

std::vector<bts::PatchGroup> frontend_mod_groups(const std::string& file) {
    std::vector<bts::PatchGroup> groups;
    for (const auto& mod : ini_mods) {
        for (const auto& target : mod.config.files) if (target.file == file)
            groups.push_back({status_name(mod.id) + "/" + file, target.patches});
    }
    return groups;
}

bool has_frontend_mods() {
    return std::any_of(ini_mods.begin(), ini_mods.end(), [](const auto& mod) { return !mod.config.files.empty(); });
}

void report_frontend_mods(bool ready) {
    for (const auto& mod : ini_mods) for (const auto& file : mod.config.files)
        set_status(status_name(mod.id) + "/" + file.file, ready ? "pending" : "failed",
            ready ? "waiting for complete ZIP read" : "CEF reader hook unavailable");
}

void apply_native_mods(HMODULE spotify) {
    if (!runtime_config.developer_enabled || !compatible_spotify)
        set_status("Developer", "skipped", compatible_spotify ? "disabled" : "unsupported Spotify version");
    for (const auto& module_name : {std::string("Spotify.dll"), std::string("Spotify.exe")}) {
        std::vector<bts::PatchGroup> groups;
        if (module_name == "Spotify.dll" && compatible_spotify && runtime_config.developer_enabled)
            groups.push_back({"Developer", {runtime_config.developer}});
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
    for (auto& mod : dll_mods) {
        const auto label = status_name(mod->file.id);
        mod->module = LoadLibraryExW(mod->file.path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!mod->module) { set_status(label, "failed", "LoadLibraryExW error " + std::to_string(GetLastError())); continue; }
        mod->host = {sizeof(BtsModHost), BTS_MOD_API_VERSION, install_directory.c_str(), mod->directory.c_str(),
            mod->config_path.empty() ? nullptr : mod->config_path.c_str(), mod->version.c_str(), mod.get(), mod_log};
        auto init = reinterpret_cast<BtsModInit>(GetProcAddress(mod->module, "bts_mod_init"));
        // Loaded modules and host contexts stay alive until process exit, even
        // if init fails: a plugin may already have registered hooks or workers.
        try {
            if (init && !init(&mod->host)) { set_status(label, "failed", "bts_mod_init returned failure"); continue; }
        } catch (...) { set_status(label, "failed", "bts_mod_init threw an exception"); continue; }
        set_status(label, "loaded", init ? "API v1 initialized" : "DllMain-only module");
    }
}

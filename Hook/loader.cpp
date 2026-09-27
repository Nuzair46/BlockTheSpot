#include "pch.h"
#include "loader.h"
#include "developer_mode.h"
#include "IAT_hook.h"
#include "log_thread.h"
#include "cef_url_hook.h"
#include "cef_zip_reader_hook.h"
#include "libcef_hook.h"
#include "../Common/windows.h"
#pragma comment(lib, "version.lib")

namespace {
bool load_configuration(std::string& error) {
    bts::Ini pack, settings;
    std::string text;
    if (!bts::read_text(install_directory + L"\\config.ini", text)) { error = "cannot read config.ini next to the patch DLL"; return false; }
    if (!bts::parse_ini(text, pack, error)) return false;
    auto settings_path = install_directory + L"\\settings.ini";
    if (GetFileAttributesW(settings_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!bts::read_text(settings_path, text) || !bts::parse_ini(text, settings, error)) {
            if (error.empty()) error = "cannot read settings.ini";
            return false;
        }
    }
    return bts::load_config(pack, settings, runtime_config, error);
}
}

VOID CALLBACK bts_main(ULONG_PTR) {
    install_directory = bts::module_directory(patch_module);
    if (install_directory.empty()) return;
    original_chrome_elf = install_directory + L"\\chrome_elf_required.dll";
    // Installed callbacks live for the process lifetime. No logger or hook
    // teardown is attempted under DllMain's loader lock.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&bts_main), &pinned);
    HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbghelp) ImageDirectoryEntryToDataEx = reinterpret_cast<ImageDirectoryEntryToDataEx_t>(
        GetProcAddress(dbghelp, "ImageDirectoryEntryToDataEx"));
    bool trust_hooked = process_IAT_hook_GetProcAddress(GetModuleHandleW(nullptr));
    std::wstring_view command(GetCommandLineW());
    bool crashpad = command.find(L"--type=crashpad-handler") != std::wstring_view::npos ||
        (command.find(L"--database=") != std::wstring_view::npos && command.find(L"--url=") != std::wstring_view::npos);
    bool child = command.find(L"--type=") != std::wstring_view::npos;
    if (child && !crashpad) return;
    std::string error;
    bool configured = load_configuration(error);
    if (crashpad) {
        // Deferred APC, outside DllMain: do not leave a permanently sleeping process.
        if (configured && runtime_config.block_crashpad) ExitProcess(0);
        return;
    }
    init_log(configured ? runtime_config.log_level : 0);
    set_status("Initialization", "starting");
    if (!trust_hooked) set_status("Trust hook", "failed", "GetProcAddress import not found or not writable");
    else set_status("Trust hook", "ready");
    if (!configured) { set_status("Configuration", "failed", error); return; }
    const auto version = bts::file_version(install_directory + L"\\Spotify.exe");
    set_status("Spotify", "detected", version);
    compatible_spotify = version == runtime_config.spotify_version;
    set_status("Compatibility", compatible_spotify ? "supported" : "failed",
        "signature pack targets " + runtime_config.spotify_version);
    auto original_version = bts::file_version(original_chrome_elf);
    auto cef_version = bts::cef_chromium_version(install_directory + L"\\libcef.dll");
    if (original_version == "unknown" || original_version != cef_version) {
        set_status("Installation", "failed", "original chrome_elf DLL is missing or differs from libcef; repair the installation");
        return;
    }
    set_status("Installation", "verified", "original CEF DLL versions agree");
    auto spotify_path = install_directory + L"\\Spotify.dll";
    auto cef_path = install_directory + L"\\libcef.dll";
    HMODULE spotify = LoadLibraryExW(spotify_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    HMODULE libcef = LoadLibraryExW(cef_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!spotify || !libcef) { set_status("Initialization", "failed", "Spotify.dll or libcef.dll could not be loaded"); return; }
    hook_developer_mode(spotify);
    hook_cef_url(libcef);
    hook_cef_reader(libcef);
    if (!libcef_IAT_hook_GetProcAddress(spotify)) { set_status("CEF hooks", "failed", "GetProcAddress import is not writable"); return; }
    set_status("CEF hooks", "ready");
    set_status("Initialization", "complete");
}

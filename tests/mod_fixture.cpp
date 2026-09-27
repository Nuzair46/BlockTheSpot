#include <windows.h>
#include "../include/blockthespot_mod.h"
#include <string>

static const BtsModHost* saved = nullptr;
static int calls = 0;
static HMODULE self = nullptr;
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self = module;
    return TRUE;
}
extern "C" __declspec(dllexport) int bts_test_calls() { return calls; }
extern "C" __declspec(dllexport) int bts_test_host_valid() {
    if (!saved || saved->size != sizeof(BtsModHost) || saved->api_version != BTS_MOD_API_VERSION || !saved->log_context) return 0;
    if (!saved->spotify_directory || !saved->mod_directory || std::string(saved->spotify_version) != "1.3.1.234") return 0;
    saved->log(saved->log_context, 1, "host context remains valid after initialization");
    const char* source = "[Compatibility]\nSpotify=1.3.1.234\n[URL_block]\n1=/late/\n";
    return !saved->register_ini(saved->log_context, source, strlen(source));
}
extern "C" __declspec(dllexport) int bts_mod_init(const BtsModHost* host) {
    ++calls; saved = host;
    wchar_t path[1024]{};
    GetModuleFileNameW(self, path, 1024);
    const auto filename = std::wstring(path);
    if (filename.find(L"fail.dll") != std::wstring::npos || filename.find(L"register.dll") != std::wstring::npos) {
        const std::string rules = "[Compatibility]\nSpotify=1.3.1.234\n[URL_block]\n1=" +
            std::string(filename.find(L"fail.dll") != std::wstring::npos ? "/failed/" : "/registered/") +
            "\n[Buffer_modify]\n1=test.js\n[test.js]\n1=change\n[change]\nSignature_1=AA BB\nOffset_1=0\nValue_1=11\n";
        if (!host->register_ini(host->log_context, rules.data(), rules.size())) return 0;
        if (host->register_ini(host->log_context, rules.data(), rules.size())) return 0; // No duplicate registration.
        if (filename.find(L"fail.dll") != std::wstring::npos) return 0;
    }
    host->log(host->log_context, 1, "fixture initialized");
    if (host->config_path) {
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Custom", L"value", L"", value, 32, host->config_path);
        if (std::wstring(value) != L"kept") return 0;
    }
    return 1;
}

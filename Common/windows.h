#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace bts {
inline std::wstring module_directory(HMODULE module) {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length == buffer.size()) return {};
    std::wstring path(buffer.data(), length);
    return path.substr(0, path.find_last_of(L"\\/"));
}
inline bool read_text(const std::wstring& path, std::string& text) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!input) return false;
    auto size = input.tellg();
    if (size < 0 || size > 1024 * 1024) return false;
    text.resize(static_cast<size_t>(size));
    input.seekg(0);
    return static_cast<bool>(input.read(text.data(), static_cast<std::streamsize>(size)));
}
inline std::string file_version(const std::wstring& path) {
    DWORD unused = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!size) return "unknown";
    std::vector<uint8_t> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return "unknown";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        length < sizeof(*info) || info->dwSignature != 0xfeef04bd) return "unknown";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
        std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
        std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
        std::to_string(LOWORD(info->dwFileVersionLS));
}
inline std::string cef_chromium_version(const std::wstring& path) {
    DWORD unused = 0, size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!size) return "unknown";
    std::vector<uint8_t> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return "unknown";
    struct Translation { WORD language, codepage; };
    Translation* translations = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length)) return "unknown";
    for (size_t i = 0; i < length / sizeof(Translation); ++i) {
        wchar_t key[96];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileVersion", translations[i].language, translations[i].codepage);
        wchar_t* value = nullptr;
        UINT count = 0;
        if (!VerQueryValueW(buffer.data(), key, reinterpret_cast<void**>(&value), &count) || !count) continue;
        std::wstring_view text(value, count - 1);
        auto position = text.find(L"chromium-");
        if (position == std::wstring_view::npos) continue;
        std::string version;
        for (wchar_t c : text.substr(position + 9)) {
            if ((c < L'0' || c > L'9') && c != L'.') break;
            version += static_cast<char>(c);
        }
        if (!version.empty()) return version;
    }
    return "unknown";
}
}

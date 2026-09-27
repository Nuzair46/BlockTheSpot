#include <windows.h>
#include <cstdint>
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    auto folder = std::filesystem::absolute(argv[1]);
    auto path = folder / "chrome_elf.dll";
    HMODULE proxy = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!proxy) { std::cerr << "Proxy load failed: " << GetLastError() << '\n'; return 1; }
    using target_t = uint64_t(*)(uint64_t, double, uint64_t, double, uint64_t, uint64_t, uint64_t, uint64_t);
    auto base = reinterpret_cast<const uint8_t*>(proxy);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    auto names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    if (!exports->NumberOfNames) return 1;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        auto name = reinterpret_cast<const char*>(base + names[i]);
        auto target = reinterpret_cast<target_t>(GetProcAddress(proxy, name));
        if (!target || target(1, 2.0, 3, 4.0, 5, 6, 7, 8) != 36) return 1;
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target), &owner) || owner == proxy) return 1;
    }
    std::cout << "Loader: all " << exports->NumberOfNames
        << " exports preserve register, floating-point, and stack arguments\n";
    // Dependencies remain loaded until process exit; never unload a queued APC.
}

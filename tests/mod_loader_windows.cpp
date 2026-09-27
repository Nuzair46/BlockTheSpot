#include "../Hook/mod_loader.h"
#include "../Hook/log_thread.h"
#include "../Hook/cef_zip_reader_hook.h"
#include "../Hook/funct_pointer.h"
#include "../Hook/memory.h"
#include "../Common/windows.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace fs = std::filesystem;
static void write(const fs::path& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary); out << body; CHECK(out.good());
}
static int call(HMODULE module, const char* name) {
    auto function = reinterpret_cast<int(*)()>(GetProcAddress(module, name)); CHECK(function); return function();
}
static HMODULE loaded(const fs::path& path) { return GetModuleHandleW(path.c_str()); }
static bts::Patch patch(const char* pattern, const char* value) {
    bts::Patch result; result.name = "builtin";
    bts::Pattern bytes; std::string error;
    CHECK(bts::parse_bytes(pattern, true, result.pattern, error));
    CHECK(bts::parse_bytes(value, false, bytes, error)); result.value = bytes.bytes;
    return result;
}
int main(int argc, char** argv) {
    CHECK(argc == 3);
    const auto fixtures = fs::absolute(argv[1]);
    const std::string mode = argv[2];
    const auto root = fixtures / L"unicode-\u6a21" / ("run-" + mode);
    fs::remove_all(root); fs::create_directories(root / L"patches");
    const auto mods = root / L"patches";
    install_directory = root.wstring();
    runtime_config = {};
    runtime_config.spotify_version = "1.3.1.234";
    runtime_config.mods_enabled = mode != "disabled";
    compatible_spotify = mode != "unsupported";
    runtime_config.developer = patch("AA BB", "11");
    runtime_config.buffers_enabled = false; // External frontend patches work independently.
    runtime_config.mod_overrides["05-disabled.dll"] = false;
    for (const auto* name : {"05-disabled.dll", "10-incompatible.dll", "20-hello.dll", "30-fail.dll", "40-after.dll"})
        fs::copy_file(fixtures / "mod-fixture.dll", mods / name);
    fs::copy_file(fixtures / "plain-mod.dll", mods / "50-plain.dll");
    write(mods / "00-broken.dll", "not a PE image");
    write(mods / "10-incompatible.ini", "[Compatibility]\nSpotify=0.0.0.0\n");
    write(mods / "20-hello.ini", "[Mod]\nEnable=1\n[Compatibility]\nSpotify=1.3.1.234\n[Custom]\nvalue=kept\n");
    write(mods / "01-invalid.ini", "invalid ini");
    const std::string prefix = "[Compatibility]\nSpotify=1.3.1.234\n";
    write(mods / "60-valid.ini", prefix + "[NativePatches]\n1=change\n[change]\nSignature=AA BB\nOffset=1\nValue=66\n"
        "[Buffer_modify]\n1=test.js\n[test.js]\n1=frontend\n[frontend]\nSignature_1=AA BB\nOffset_1=1\nValue_1=66\n");
    write(mods / "61-conflict.ini", prefix + "[NativePatches]\n1=change\n[change]\nSignature=AA BB\nOffset=0\nValue=22\n"
        "[Buffer_modify]\n1=test.js\n[test.js]\n1=frontend\n[frontend]\nSignature_1=AA BB\nOffset_1=1\nValue_1=22\n");
    write(mods / "62-later.ini", prefix + "[Buffer_modify]\n1=test.js\n[test.js]\n1=frontend\n[frontend]\nSignature_1=DD EE\nOffset_1=0\nValue_1=77\n");
    init_log(2);
    fs::current_path(fixtures); // Must discover beside the host, not the current directory.
    discover_mods("1.3.1.234");
    discover_mods("1.3.1.234");
    if (mode != "enabled") {
        load_dll_mods();
        CHECK(!loaded(mods / "20-hello.dll") && !has_frontend_mods());
        std::cout << "Mods disabled/unsupported gate passed: " << mode << '\n'; return 0;
    }
    CHECK(has_frontend_mods() && frontend_mod_groups("test.js").size() == 3);
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const auto page_size = system.dwPageSize;
    auto memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page_size * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); CHECK(memory);
    DWORD previous = 0;
    CHECK(VirtualProtect(memory + page_size, page_size, PAGE_READONLY, &previous));
    std::vector<bts::Write> boundary{{page_size - 1, {0x12, 0x34}}};
    CHECK(patch_instructions(memory, page_size * 3, boundary));
    CHECK(memory[page_size - 1] == 0x12 && memory[page_size] == 0x34);
    MEMORY_BASIC_INFORMATION info{};
    CHECK(VirtualQuery(memory, &info, sizeof(info)) && info.Protect == PAGE_READWRITE);
    CHECK(VirtualQuery(memory + page_size, &info, sizeof(info)) && info.Protect == PAGE_READONLY);
    CHECK(VirtualFree(memory + page_size * 2, page_size, MEM_DECOMMIT));
    boundary = {{0, {0x55}}, {page_size * 2, {0x66}}};
    CHECK(!patch_instructions(memory, page_size * 3, boundary) && memory[0] == 0);
    CHECK(VirtualQuery(memory, &info, sizeof(info)) && info.Protect == PAGE_READWRITE);
    VirtualFree(memory, 0, MEM_RELEASE);
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); CHECK(image);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image); dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + 128);
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
    auto section = IMAGE_FIRST_SECTION(nt); memcpy(section->Name, ".text", 5);
    section->VirtualAddress = 4096; section->Misc.VirtualSize = 1024;
    const unsigned char initial[]{0xAA, 0xBB, 0xCC, 0xDD, 0xEE}; memcpy(image + 4096, initial, 5);
    apply_native_mods(reinterpret_cast<HMODULE>(image));
    CHECK(image[4096] == 0x11 && image[4097] == 0x66); // Builtin + valid; conflicting mod skipped.
    VirtualFree(image, 0, MEM_RELEASE);

    HMODULE cef = LoadLibraryW((fixtures / "cef-fixture.dll").c_str()); CHECK(cef);
    hook_cef_reader(cef); CHECK(cef_reader_ready());
    auto set_partial = reinterpret_cast<void(*)(int)>(GetProcAddress(cef, "bts_test_partial")); CHECK(set_partial);
    using read_t = int(__stdcall*)(void*, void*, size_t);
    std::array<uint8_t, 5> buffer{};
    set_partial(1);
    auto reader = cef_zip_reader_create_stub(nullptr);
    CHECK(get_funct_t<read_t>(reader, 112)(reader, buffer.data(), buffer.size()) == 5);
    CHECK((buffer == std::array<uint8_t, 5>{0xAA, 0xBB, 0xCC, 0xDD, 0xEE}));
    set_partial(0);
    reader = cef_zip_reader_create_stub(nullptr);
    CHECK(get_funct_t<read_t>(reader, 112)(reader, buffer.data(), buffer.size()) == 5);
    CHECK((buffer == std::array<uint8_t, 5>{0xAA, 0x66, 0xCC, 0x77, 0xEE}));
    runtime_config.buffers_enabled = true;
    runtime_config.files.push_back({"test.js", {patch("AA BB", "11")}});
    reader = cef_zip_reader_create_stub(nullptr);
    CHECK(get_funct_t<read_t>(reader, 112)(reader, buffer.data(), buffer.size()) == 5);
    CHECK((buffer == std::array<uint8_t, 5>{0x11, 0x66, 0xCC, 0x77, 0xEE}));

    load_dll_mods(); load_dll_mods();
    CHECK(!loaded(mods / "05-disabled.dll") && !loaded(mods / "10-incompatible.dll"));
    for (const auto* name : {"20-hello.dll", "30-fail.dll", "40-after.dll", "50-plain.dll"}) {
        auto module = loaded(mods / name); CHECK(module && call(module, "bts_test_calls") == 1);
    }
    CHECK(call(loaded(mods / "20-hello.dll"), "bts_test_host_valid"));
    std::string status;
    CHECK(bts::read_text(install_directory + L"\\blockthespot-status.txt", status));
    for (const auto* expected : {"Mod 00-broken.dll: failed", "Mod 01-invalid.ini: failed", "Mod 20-hello.dll: loaded", "Mod 30-fail.dll: failed", "Mod 40-after.dll: loaded", "Mod 50-plain.dll: loaded", "Mod 61-conflict.ini/Spotify.dll: failed", "Mod 61-conflict.ini/test.js: failed", "Mod 62-later.ini/test.js: applied"}) CHECK(status.find(expected) != std::string::npos);
    CHECK(status.find("Mod 20-hello.ini:") == std::string::npos);
    std::cout << "Windows mod loader: DLL initialization/lifetime/failure isolation, settings pairing, disable/version gates, native conflicts and full/partial SPA reads passed\n";
}

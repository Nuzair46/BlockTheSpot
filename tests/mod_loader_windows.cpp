#include "../Hook/mod_loader.h"
#include "../Hook/log_thread.h"
#include "../Hook/cef_zip_reader_hook.h"
#include "../Hook/cef_url_hook.h"
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
static std::vector<uint8_t> fixture_bytes(const std::vector<bts::Patch>& patches) {
    std::vector<uint8_t> result;
    for (const auto& patch : patches) {
        result.insert(result.end(), patch.pattern.bytes.begin(), patch.pattern.bytes.end());
        result.insert(result.end(), 16 + patch.offset + patch.value.size(), 0xFE);
    }
    return result;
}
static void bundled_test(const fs::path& fixtures, const fs::path& bundle, const fs::path& mods, const std::string& mode) {
    const bool active = mode == "bundled" || mode == "bundled-css" || mode == "renamed";
    const auto stem = mode == "renamed" ? "renamed" : "blockthespot";
    std::string text, error;
    CHECK(bts::read_text((bundle / "blockthespot.ini").wstring(), text));
    if (mode == "bundled-css") text.replace(text.find("Enable=0"), 8, "Enable=1");
    bts::Ini ini; bts::IniMod config;
    CHECK(bts::parse_ini(text, ini, error) && bts::load_ini_mod(ini, config, error));
    if (mode != "empty") {
        if (mode != "bundled-missing") fs::copy_file(bundle / "blockthespot.dll", mods / (std::string(stem) + ".dll"));
        if (mode == "bundled-disabled") text.replace(text.find("Enable=1"), 8, "Enable=0");
        write(mods / (std::string(stem) + ".ini"), text);
        // A different INI-only mod works regardless of the bundled DLL's presence or flag.
        write(mods / "othermod.ini", "[Compatibility]\nSpotify=1.3.1.234\n[URL_block]\n1=/other-mod/\n");
    }
    discover_mods("1.3.1.234"); load_dll_mods();
    CHECK(has_frontend_mods() == active);
    CHECK(bool(loaded(mods / (std::string(stem) + ".dll"))) == active);
    HMODULE cef = LoadLibraryW((fixtures / "cef-fixture.dll").c_str()); CHECK(cef);
    hook_cef_url(cef); hook_cef_reader(cef);
    CHECK(cef_reader_ready() == active);
    CHECK(cef_url_ready() == (mode != "empty"));
    auto request = reinterpret_cast<void*(*)(const wchar_t*)>(GetProcAddress(cef, "bts_test_request")); CHECK(request);
    if (cef_url_ready()) {
        CHECK((cef_urlrequest_create_stub(request(L"https://example.com/ads/a"), nullptr, nullptr) == nullptr) == active);
        CHECK(cef_urlrequest_create_stub(request(L"https://example.com/music?next=/ads/a"), nullptr, nullptr));
        CHECK(!cef_urlrequest_create_stub(request(L"https://example.com/other-mod/a"), nullptr, nullptr));
    }
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); CHECK(image);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image); dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + 128);
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
    auto section = IMAGE_FIRST_SECTION(nt); memcpy(section->Name, ".text", 5);
    section->VirtualAddress = 4096; section->Misc.VirtualSize = 1024;
    auto native = fixture_bytes(config.native[0].patches);
    memcpy(image + 4096, native.data(), native.size());
    apply_native_mods(reinterpret_cast<HMODULE>(image));
    if (active) CHECK(bts::apply(native, config.native[0].patches, error));
    CHECK(std::equal(native.begin(), native.end(), image + 4096));
    VirtualFree(image, 0, MEM_RELEASE);
    auto set_file = reinterpret_cast<void(*)(const wchar_t*, const uint8_t*, size_t)>(GetProcAddress(cef, "bts_test_file")); CHECK(set_file);
    using read_t = int(__stdcall*)(void*, void*, size_t);
    for (const auto& file : config.files) {
        auto source = fixture_bytes(file.patches), expected = source;
        if (active) CHECK(bts::apply(expected, file.patches, error));
        std::wstring filename(file.file.begin(), file.file.end());
        set_file(filename.c_str(), source.data(), source.size());
        if (active) {
            auto reader = cef_zip_reader_create_stub(nullptr);
            CHECK(get_funct_t<read_t>(reader, 112)(reader, source.data(), source.size()) == static_cast<int>(source.size()));
        } else CHECK(frontend_mod_groups(file.file, source).empty());
        CHECK(source == expected);
    }
    for (const auto& css : config.stylesheets) {
        auto clean = fixture_bytes({css.patch}), expected = clean;
        CHECK(bts::apply(expected, {&css.patch, 1}, error));
        set_file(L"random-name.css", clean.data(), clean.size());
        auto reader = cef_zip_reader_create_stub(nullptr);
        auto output = clean;
        CHECK(get_funct_t<read_t>(reader, 112)(reader, output.data(), output.size()) == static_cast<int>(output.size()));
        CHECK(output == expected && output != clean);
        // An embedded copy does not select another stylesheet.
        clean.insert(clean.begin(), 0xFE);
        set_file(L"embedded.css", clean.data(), clean.size());
        reader = cef_zip_reader_create_stub(nullptr); output = clean;
        CHECK(get_funct_t<read_t>(reader, 112)(reader, output.data(), output.size()) == static_cast<int>(output.size()));
        CHECK(output == clean);
        // Even with a prefix match, a duplicate signature rejects the whole patch.
        clean.erase(clean.begin());
        auto repeated = clean; repeated.insert(repeated.end(), clean.begin(), clean.end());
        set_file(L"repeated.css", repeated.data(), repeated.size());
        reader = cef_zip_reader_create_stub(nullptr); output = repeated;
        CHECK(get_funct_t<read_t>(reader, 112)(reader, output.data(), output.size()) == static_cast<int>(output.size()));
        CHECK(output == repeated);
    }
    std::cout << "Bundled DLL native/frontend/URL registration and independence passed: " << mode << '\n';
}
int main(int argc, char** argv) {
    CHECK(argc == 4);
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
    init_log(2);
    if (mode == "bundled" || mode == "bundled-css" || mode == "bundled-disabled" || mode == "bundled-missing" || mode == "renamed" || mode == "empty") {
        bundled_test(fixtures, fs::absolute(argv[3]), mods, mode); return 0;
    }
    write(mods / "05-disabled.ini", "[Mod]\nEnable=0\n");
    for (const auto* name : {"05-disabled.dll", "10-incompatible.dll", "20-hello.dll", "25-register.dll", "30-fail.dll", "40-after.dll"})
        fs::copy_file(fixtures / "mod-fixture.dll", mods / name);
    fs::copy_file(fixtures / "plain-mod.dll", mods / "50-plain.dll");
    write(mods / "00-broken.dll", "not a PE image");
    write(mods / "10-incompatible.ini", "[Compatibility]\nSpotify=0.0.0.0\n");
    write(mods / "20-hello.ini", "[Mod]\nEnable=1\n[Compatibility]\nSpotify=1.3.1.234\n[Custom]\nvalue=kept\n");
    write(mods / "01-invalid.ini", "invalid ini");
    const std::string prefix = "[Compatibility]\nSpotify=1.3.1.234\n";
    write(mods / "60-valid.ini", prefix + "[NativePatches]\n1=change\n[change]\nSignature=AA BB\nOffset=1\nValue=66\n"
        "[Buffer_modify]\n1=test.js\n[test.js]\n1=frontend\n[frontend]\nSignature_1=AA BB\nOffset_1=1\nValue_1=66\n");
    write(mods / "61-conflict.ini", prefix + "[NativePatches]\n1=change\n[change]\nSignature=AA BB\nOffset=1\nValue=22\n"
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
    load_dll_mods(); load_dll_mods();
    CHECK(has_frontend_mods() && frontend_mod_groups("test.js").size() == 4);
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
    CHECK(image[4096] == 0xAA && image[4097] == 0x66); // First valid mod wins over a later conflict.
    VirtualFree(image, 0, MEM_RELEASE);

    HMODULE cef = LoadLibraryW((fixtures / "cef-fixture.dll").c_str()); CHECK(cef);
    hook_cef_reader(cef); CHECK(cef_reader_ready());
    hook_cef_url(cef); CHECK(cef_url_ready());
    auto request = reinterpret_cast<void*(*)(const wchar_t*)>(GetProcAddress(cef, "bts_test_request")); CHECK(request);
    CHECK(!cef_urlrequest_create_stub(request(L"https://example.com/registered/a"), nullptr, nullptr));
    CHECK(cef_urlrequest_create_stub(request(L"https://example.com/failed/a"), nullptr, nullptr));
    CHECK(cef_urlrequest_create_stub(request(L"https://example.com/ads/a"), nullptr, nullptr));
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
    CHECK((buffer == std::array<uint8_t, 5>{0x11, 0x66, 0xCC, 0x77, 0xEE}));
    load_dll_mods(); load_dll_mods();
    CHECK(!loaded(mods / "05-disabled.dll") && !loaded(mods / "10-incompatible.dll"));
    for (const auto* name : {"20-hello.dll", "25-register.dll", "30-fail.dll", "40-after.dll", "50-plain.dll"}) {
        auto module = loaded(mods / name); CHECK(module && call(module, "bts_test_calls") == 1);
    }
    CHECK(call(loaded(mods / "20-hello.dll"), "bts_test_host_valid"));
    std::string status;
    CHECK(bts::read_text(install_directory + L"\\blockthespot-status.txt", status));
    for (const auto* expected : {"Mod 00-broken.dll: failed", "Mod 01-invalid.ini: failed", "Mod 20-hello.dll: loaded", "Mod 30-fail.dll: failed", "Mod 40-after.dll: loaded", "Mod 50-plain.dll: loaded", "Mod 61-conflict.ini/Spotify.dll: failed", "Mod 61-conflict.ini/test.js: failed", "Mod 62-later.ini/test.js: applied"}) CHECK(status.find(expected) != std::string::npos);
    CHECK(status.find("Mod 20-hello.ini:") == std::string::npos);
    std::cout << "Windows mod loader: DLL initialization/lifetime/failure isolation, settings pairing, disable/version gates, native conflicts and full/partial SPA reads passed\n";
}

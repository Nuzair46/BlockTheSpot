#include "../Common/mods.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

static bool read(const std::filesystem::path& path, std::string& text, size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    auto size = input.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > limit) return false;
    text.resize(static_cast<size_t>(size)); input.seekg(0);
    return static_cast<bool>(input.read(text.data(), static_cast<std::streamsize>(size)));
}

int main(int argc, char** argv) {
    bool all = argc > 1 && std::string_view(argv[argc - 1]) == "--all";
    if (all) --argc;
    const std::string_view command = argc > 1 ? argv[1] : "";
    if (!((argc == 3 && (command == "inspect" || command == "inspect-mod")) ||
          (argc == 6 && command == "apply-mod")) || (all && command == "inspect")) {
        std::cerr << "Usage: patch-tool inspect CONFIG | inspect-mod MOD [--all] | apply-mod MOD TARGET INPUT OUTPUT [--all]\n";
        return 2;
    }
    std::string source, error;
    bts::Ini ini;
    if (!read(argv[2], source, 1024 * 1024) || !bts::parse_ini(source, ini, error)) {
        std::cerr << "Invalid config: " << (error.empty() ? "file is unreadable" : error) << '\n'; return 1;
    }
    if (command == "inspect") {
        bts::Config config;
        if (!bts::load_config(ini, config, error)) { std::cerr << error << '\n'; return 1; }
        std::cout << "VERSION\t" << config.spotify_version << '\n';
        return 0;
    }
    // Explicit offline validation can include features disabled at runtime.
    if (all) for (auto& [section, entries] : ini.sections) {
        auto flag = entries.find("enable");
        if (flag != entries.end()) {
            size_t value;
            if (!bts::parse_number(flag->second, value) || value > 1) { std::cerr << section << ": invalid Enable flag\n"; return 1; }
            flag->second = "1";
        }
    }
    bts::IniMod mod;
    if (!bts::load_ini_mod(ini, mod, error)) { std::cerr << error << '\n'; return 1; }
    if (command == "inspect-mod") {
        std::cout << "ENABLED\t" << mod.enabled << '\n' << "VERSION\t" << mod.spotify_version << '\n';
        std::cout << "URL_RULES\t" << mod.urls.size() << '\n';
        for (const auto& target : mod.native) std::cout << "NATIVE\t" << target.file << '\n';
        for (const auto& target : mod.files) std::cout << "FILE\t" << target.file << '\n';
        for (const auto& target : mod.stylesheets) std::cout << "STYLE\t" << target.patch.name << '\t' << target.extension << '\n';
        return 0;
    }
    if (!mod.enabled) { std::cerr << "Mod is disabled\n"; return 1; }
    std::vector<bts::Patch> patches;
    bool prefix = false;
    for (const auto& target : mod.native) if (bts::lower(target.file) == bts::lower(argv[3])) patches = target.patches;
    for (const auto& target : mod.files) if (target.file == argv[3]) patches = target.patches;
    for (const auto& target : mod.stylesheets) if (target.patch.name == argv[3]) { patches = {target.patch}; prefix = true; }
    if (patches.empty()) { std::cerr << "Unknown or disabled patch target\n"; return 1; }
    if (!read(argv[4], source, 128 * 1024 * 1024)) { std::cerr << "Input is unreadable or too large\n"; return 1; }
    auto bytes = std::span(reinterpret_cast<uint8_t*>(source.data()), source.size());
    std::vector<bts::Write> writes;
    if (!bts::plan(bytes, patches, writes, error)) { std::cerr << error << '\n'; return 1; }
    if (prefix && writes[0].offset != patches[0].offset) {
        std::cerr << "Stylesheet signature must start at byte zero\n"; return 1;
    }
    for (const auto& write : writes) std::copy(write.value.begin(), write.value.end(), bytes.begin() + write.offset);
    std::ofstream output(argv[5], std::ios::binary);
    if (!output.write(source.data(), static_cast<std::streamsize>(source.size()))) return 1;
    std::cout << argv[3] << ": " << writes.size() << " unique, bounded writes\n";
}

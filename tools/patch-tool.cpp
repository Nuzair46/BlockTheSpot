#include "../Common/config.h"
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
    if (argc != 3 && argc != 6) {
        std::cerr << "Usage: patch-tool inspect CONFIG | patch-tool apply CONFIG TARGET INPUT OUTPUT\n"; return 2;
    }
    std::string source, error;
    bts::Ini ini;
    bts::Config config;
    if (!read(argv[2], source, 1024 * 1024) || !bts::parse_ini(source, ini, error) ||
        !bts::load_config(ini, {}, config, error)) {
        std::cerr << "Invalid config: " << (error.empty() ? "file is unreadable" : error) << '\n'; return 1;
    }
    if (argc == 3 && std::string_view(argv[1]) == "inspect") {
        std::cout << "VERSION\t" << config.spotify_version << '\n';
        std::cout << "URL_RULES\t" << config.urls.size() << '\n';
        for (auto& target : config.files) std::cout << "FILE\t" << target.file << '\n';
        return 0;
    }
    if (argc != 6 || std::string_view(argv[1]) != "apply") return 2;
    std::vector<bts::Patch> patches;
    if (std::string_view(argv[3]) == "Developer") patches.push_back(config.developer);
    else if (std::string_view(argv[3]) == "Homepage_vbar") patches.push_back(config.css);
    else for (const auto& target : config.files) if (target.file == argv[3]) patches = target.patches;
    if (patches.empty()) { std::cerr << "Unknown patch target\n"; return 1; }
    if (!read(argv[4], source, 128 * 1024 * 1024)) { std::cerr << "Input is unreadable or too large\n"; return 1; }
    auto bytes = std::span(reinterpret_cast<uint8_t*>(source.data()), source.size());
    std::vector<bts::Write> writes;
    if (!bts::plan(bytes, patches, writes, error)) { std::cerr << error << '\n'; return 1; }
    if (std::string_view(argv[3]) == "Homepage_vbar" && writes[0].offset != config.css.offset) {
        std::cerr << "CSS signature must start at byte zero\n"; return 1;
    }
    if (!bts::apply(bytes, patches, error)) { std::cerr << error << '\n'; return 1; }
    std::ofstream output(argv[5], std::ios::binary);
    if (!output.write(source.data(), static_cast<std::streamsize>(source.size()))) return 1;
    std::cout << argv[3] << ": " << writes.size() << " unique, bounded writes\n";
}

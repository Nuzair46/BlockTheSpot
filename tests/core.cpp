#include "../Common/config.h"
#include "../Common/url.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

bts::Patch patch(std::string pattern, size_t offset, std::string value) {
    bts::Patch p; p.name = "test"; p.offset = offset; std::string error;
    bts::Pattern bytes;
    CHECK(bts::parse_bytes(pattern, true, p.pattern, error));
    CHECK(bts::parse_bytes(value, false, bytes, error)); p.value = bytes.bytes;
    return p;
}
int main() {
    std::string error;
    bts::Pattern pattern;
    CHECK(bts::parse_bytes("00 FF ff ??", true, pattern, error));
    CHECK(pattern.bytes[1] == 255 && pattern.exact[3] == 0);
    for (auto bad : {"", " ", "?", "??", "GG", "F", "AABB", "00 ?X"})
        CHECK(!bts::parse_bytes(bad, true, pattern, error));
    CHECK(!bts::parse_bytes("??", false, pattern, error));
    CHECK(bts::parse_bytes("AA ??", true, pattern, error));
    size_t n = 0;
    for (auto bad : {"", "-1", "+1", "1.2", "2x", "18446744073709551616"}) CHECK(!bts::parse_number(bad, n));
    CHECK(bts::parse_number(" 0 ", n) && n == 0);
    std::array<uint8_t, 1> tiny{0xAA};
    CHECK(!bts::unique_match(tiny, pattern, n, error));
    std::array<uint8_t, 4> input{0, 0xAA, 0xBB, 0xCC};
    std::vector<bts::Patch> patches{patch("BB CC", 1, "FF")};
    CHECK(bts::apply(input, patches, error) && input[3] == 255);
    input = {0, 0xAA, 0xBB, 0xCC}; const auto original = input;
    patches = {patch("AA BB", 0, "FF"), patch("DD EE", 0, "00")};
    CHECK(!bts::apply(input, patches, error) && input == original);
    patches = {patch("AA BB", 0, "FF"), patch("BB CC", 0, "00")};
    CHECK(bts::apply(input, patches, error)); // All matches use the original bytes.
    input = original;
    patches = {patch("AA BB", 0, "FF FF"), patch("BB CC", 0, "00")};
    CHECK(!bts::apply(input, patches, error) && input == original);
    for (size_t offset : {size_t(4), std::numeric_limits<size_t>::max()}) {
        patches = {patch("AA BB", offset, "FF")};
        CHECK(!bts::apply(input, patches, error) && input == original);
    }
    std::array<uint8_t, 3> repeated{0xAA, 0xAA, 0xAA};
    patches = {patch("AA AA", 0, "00")};
    CHECK(!bts::apply(repeated, patches, error)); // Includes overlapping matches.
    bts::Ini ini;
    CHECK(bts::parse_ini("\xEF\xBB\xBF[Test]\r\nKey=one\r\n", ini, error));
    CHECK(*ini.get("TEST", "KEY") == "one");
    CHECK(!bts::parse_ini("[s]\nx=1\nX=2", ini, error));
    CHECK(!bts::parse_ini("x=1", ini, error));
    for (size_t count : {size_t(5), size_t(10), size_t(256)}) {
        std::string source = "[list]\n";
        for (size_t i = 1; i <= count; ++i) source += std::to_string(i) + "=item\n";
        CHECK(bts::parse_ini(source, ini, error));
        std::vector<std::string> entries;
        CHECK(bts::numbered(ini, "list", 256, entries, error) && entries.size() == count);
    }
    CHECK(bts::parse_ini("[list]\n1=a\n3=b", ini, error));
    std::vector<std::string> entries;
    CHECK(!bts::numbered(ini, "list", 256, entries, error));
    CHECK(bts::parse_ini("[list]\n-1=a", ini, error));
    CHECK(!bts::numbered(ini, "list", 256, entries, error));
    bts::Ini pack, settings;
    CHECK(bts::parse_ini("[Compatibility]\nSpotify=1.2.3.4\n"
        "[Developer]\nSignature=AA\nValue=FF\nOffset=0\n"
        "[Homepage_vbar]\nSignature=AA\nValue=FF\nOffset=0\n"
        "[URL_block]\n1=/ads/\n[Buffer_modify]\n1=test.js\n"
        "[test.js]\n1=change\n[change]\nSignature_1=AA\nValue_1=FF\nOffset_1=0", pack, error));
    bts::Config config;
    CHECK(bts::load_config(pack, settings, config, error));
    CHECK(config.developer_enabled && config.urls.size() == 1 && config.files.size() == 1);
    CHECK(bts::parse_ini("[Developer]\nEnable=0\n[Log]\nLevel=2", settings, error));
    CHECK(bts::load_config(pack, settings, config, error));
    CHECK(!config.developer_enabled && config.log_level == 2);
    settings.sections["developer"]["enable"] = "2";
    CHECK(!bts::load_config(pack, settings, config, error));
    settings = {};
    settings.sections["developer"]["offset"] = "1";
    CHECK(!bts::load_config(pack, settings, config, error));
    settings = {};
    pack.sections["change"]["value_2"] = "FF";
    CHECK(!bts::load_config(pack, settings, config, error));
    pack.sections["change"].erase("value_2");
    pack.sections["change"]["signature_3"] = "AA";
    CHECK(!bts::load_config(pack, settings, config, error));
    pack.sections["change"].erase("signature_3");
    pack.sections["libcef"]["cef_request_get_url_offset"] = "49";
    CHECK(!bts::load_config(pack, settings, config, error));
    CHECK(bts::url_path(L"https://example.com/ads/a?secret=yes#x") == L"/ads/a");
    CHECK(bts::url_path(L"https://example.com?next=/ads/a").empty());
    CHECK(bts::url_path(L"https://example.com/music?next=/ads/a") == L"/music");
    std::wstring long_url = L"https://example.com/ads/" + std::wstring(10000, L'x');
    CHECK(bts::url_path(long_url).starts_with(L"/ads/"));
    std::cout << "Core: parser, bounds, uniqueness, transactions, full lists, and URL handling passed\n";
}

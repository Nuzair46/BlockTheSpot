#include "../Common/mods.h"
#include <array>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

static bts::Patch patch(const char* signature, size_t offset, const char* value) {
    bts::Patch result; result.name = signature; result.offset = offset;
    bts::Pattern replacement; std::string error;
    CHECK(bts::parse_bytes(signature, true, result.pattern, error));
    CHECK(bts::parse_bytes(value, false, replacement, error));
    result.value = replacement.bytes; return result;
}
int main() {
    std::string error;
    bts::Ini ini;
    bts::IniMod mod;
    const std::string valid = "[Compatibility]\nSpotify=1.3.1.234\n[NativePatches]\n1=one\n2=two\n"
        "[one]\nSignature=AA BB\nOffset=0\nValue=CC\n[two]\nEnable=0\n"
        "[Buffer_modify]\n1=test.js\n2=style.css\n[test.js]\n1=change\n[style.css]\n1=change\n"
        "[change]\nSignature_1=AA BB\nOffset_1=1\nValue_1=FF\n";
    CHECK(bts::parse_ini(valid, ini, error));
    CHECK(bts::load_ini_mod(ini, mod, error));
    CHECK(mod.native.size() == 1 && mod.native[0].patches.size() == 1 && mod.files.size() == 2);
    CHECK(mod.native[0].file == "Spotify.dll" && mod.files[1].file == "style.css");
    ini.sections["nativepatches"]["enable"] = "0";
    CHECK(bts::load_ini_mod(ini, mod, error) && mod.native.empty() && mod.files.size() == 2);
    ini.sections["nativepatches"]["enable"] = "1";
    ini.sections["one"]["modlue"] = "Spotify.exe";
    CHECK(!bts::load_ini_mod(ini, mod, error));
    ini.sections["one"].erase("modlue");
    for (auto module : {"../evil.dll", "C:\\evil.dll", "libcef.dll"}) {
        ini.sections["one"]["module"] = module;
        CHECK(!bts::load_ini_mod(ini, mod, error));
    }
    ini.sections["one"]["module"] = "Spotify.exe";
    CHECK(bts::load_ini_mod(ini, mod, error) && mod.native[0].file == "Spotify.exe");
    ini.sections["change"]["value_2"] = "FF";
    CHECK(!bts::load_ini_mod(ini, mod, error));
    ini.sections["change"].erase("value_2");
    ini.sections["change"]["signature_3"] = "AA";
    CHECK(!bts::load_ini_mod(ini, mod, error));
    ini.sections["change"].erase("signature_3");
    ini.sections["nativepatches"]["2"] = "one";
    CHECK(!bts::load_ini_mod(ini, mod, error));
    ini.sections["mod"]["enable"] = "0";
    CHECK(bts::load_ini_mod(ini, mod, error) && !mod.enabled && mod.files.empty());
    for (const auto& body : {"[Buffer_modify]\n", "[Compatibility]\nSpotify=1.2.3.4", "[Mod]\nEnable=2"}) {
        CHECK(bts::parse_ini(body, ini, error)); CHECK(!bts::load_ini_mod(ini, mod, error));
    }
    auto files = bts::group_mod_files({"/mods/Z.ini", "/mods/B.dll", "/mods/b.INI", "/mods/ignore.txt", "/mods/a.INI"});
    CHECK(files.size() == 3 && files[0].id == "a.ini" && files[1].id == "b.dll" && files[2].id == "z.ini");
    CHECK(files[1].dll && files[1].settings.filename() == "b.INI" && files[0].settings.empty());
    for (const auto* bad : {"../x.dll", "foo/bar.ini", "C:bad.dll", ".ini", "bad\n.dll", "bad.txt"}) CHECK(!bts::mod_filename(bad));
    CHECK(bts::mod_filename("My-Mod.DLL"));

    CHECK(bts::parse_ini("[Compatibility]\nSpotify=1.3.1.234\n[URL_block]\n1=/ads/\n"
        "[Stylesheets]\n1=cosmetic\n[cosmetic]\nExtension=.css\nSignature=AA BB\nOffset=0\nValue=CC\n", ini, error));
    CHECK(bts::load_ini_mod(ini, mod, error) && mod.urls.size() == 1 && mod.stylesheets.size() == 1);
    for (const auto* invalid : {"ads", "/ads/?token", "/ads/#part", "/ads/\t"}) {
        ini.sections["url_block"]["1"] = invalid;
        CHECK(!bts::load_ini_mod(ini, mod, error));
    }
    ini.sections["url_block"]["1"] = "/ads/";
    ini.sections["cosmetic"]["extension"] = ".js";
    CHECK(!bts::load_ini_mod(ini, mod, error));
    ini.sections["cosmetic"]["extension"] = ".css";
    ini.sections["cosmetic"]["enable"] = "0";
    ini.sections["url_block"]["enable"] = "0";
    CHECK(bts::load_ini_mod(ini, mod, error) && mod.urls.empty() && mod.stylesheets.empty());
    ini.sections["stylesheets"]["enable"] = "2";
    CHECK(!bts::load_ini_mod(ini, mod, error));

    const std::array<uint8_t, 5> original{0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    std::vector<bts::PatchGroup> groups{
        {"first-mod", {patch("AA BB", 0, "11")}},
        {"overlaps", {patch("AA BB", 0, "22"), patch("DD EE", 0, "33")}},
        {"missing", {patch("BB CC", 0, "44"), patch("FA FB", 0, "55")}},
        {"valid", {patch("AA BB", 1, "66"), patch("DD EE", 0, "77")}}
    };
    std::vector<bts::Write> writes;
    auto results = bts::plan_groups(original, groups, writes);
    CHECK(results.size() == 4 && results[0].error.empty() && !results[1].error.empty() && !results[2].error.empty() && results[3].error.empty());
    CHECK(writes.size() == 3); // Bad groups contributed no writes; valid mod matched original AA BB.
    auto output = original;
    for (const auto& write : writes) std::copy(write.value.begin(), write.value.end(), output.begin() + write.offset);
    CHECK((output == std::array<uint8_t, 5>{0x11, 0x66, 0xCC, 0x77, 0xEE}));
    std::cout << "Mods: independent INIs, target validation, discovery grouping/order, and conflict isolation passed\n";
}

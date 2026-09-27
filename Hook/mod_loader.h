#pragma once
#include "loader.h"
#include "../Common/mods.h"

void discover_mods(const std::string& spotify_version);
void apply_native_mods(HMODULE spotify);
void load_dll_mods();
bool has_frontend_mods();
std::vector<bts::PatchGroup> frontend_mod_groups(const std::string& file, std::span<const uint8_t> bytes = {});
void report_frontend_mods(bool ready);

struct UrlMod { std::string owner; std::vector<std::wstring> rules; };
std::vector<UrlMod> url_mods();

#pragma once
#include "pch.h"
#include "../Common/config.h"
#include <string>

inline HMODULE patch_module = nullptr;
inline std::wstring install_directory;
inline std::wstring original_chrome_elf;
inline bts::Config runtime_config;
inline bool compatible_spotify = false;
inline constexpr char PATCH_VERSION[] = "2.0.0";

using ImageDirectoryEntryToDataEx_t = PVOID(WINAPI*)(HMODULE, BOOLEAN, USHORT, PULONG, PIMAGE_SECTION_HEADER*);
inline ImageDirectoryEntryToDataEx_t ImageDirectoryEntryToDataEx = nullptr;
VOID CALLBACK bts_main(ULONG_PTR);

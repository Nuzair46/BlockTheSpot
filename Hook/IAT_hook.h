#pragma once
#include "loader.h"

using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
inline GetProcAddress_t GetProcAddress_orig = GetProcAddress;

bool hook_get_proc_address(HMODULE module, GetProcAddress_t replacement) noexcept;

bool process_IAT_hook_GetProcAddress(HMODULE module) noexcept;

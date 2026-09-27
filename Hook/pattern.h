#pragma once
#include "loader.h"
struct DLL_section { size_t size; BYTE* address; };
bool get_text_section(HMODULE module, DLL_section* section) noexcept;

#pragma once
#include "../Common/patch.h"
#include "loader.h"
bool patch_instructions(void* base, size_t size, std::span<const bts::Write> writes) noexcept;

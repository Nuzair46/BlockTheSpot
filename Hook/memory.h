#pragma once
#include "loader.h"
bool patch_instruction(void* address, const void* value, size_t size) noexcept;

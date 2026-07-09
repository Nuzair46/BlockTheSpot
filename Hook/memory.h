#pragma once
#include "loader.h"

bool patch_instruction(void* address, const void* value, SIZE_T patch_size) noexcept;

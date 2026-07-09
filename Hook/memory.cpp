#include "pch.h"
#include "memory.h"

bool patch_instruction(void* address, const void* value, SIZE_T patch_size) noexcept
{
	if (!address || !value || 0 == patch_size) {
		return false;
	}

	DWORD oldProtect = 0;
	if (FALSE == VirtualProtect(address, patch_size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}

	memcpy(address, value, patch_size);
	FlushInstructionCache(GetCurrentProcess(), address, patch_size);

	DWORD ignored = 0;
	return FALSE != VirtualProtect(address, patch_size, oldProtect, &ignored);
}

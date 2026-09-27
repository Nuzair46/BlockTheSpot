#include "pch.h"
#include "memory.h"

bool patch_instruction(void* address, const void* value, size_t size) noexcept {
    if (!address || !value || !size) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    memcpy(address, value, size);
    DWORD unused = 0;
    bool restored = VirtualProtect(address, size, old_protect, &unused) != FALSE;
    return FlushInstructionCache(GetCurrentProcess(), address, size) != FALSE && restored;
}

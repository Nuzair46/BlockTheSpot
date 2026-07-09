#pragma once
#include <cstdint>

template<typename T>
inline T get_funct_t(void* base, size_t offset)
{
    if (!base) {
        return nullptr;
    }

    return *reinterpret_cast<T*>(
        reinterpret_cast<uintptr_t>(base) + offset
        );
}

template<typename T>
inline bool overwrite_funct_t(void* base, size_t offset, T replacement)
{
    if (!base || !replacement) {
        return false;
    }

    auto slot = reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(base) + offset
        );
    DWORD old = 0;
    if (FALSE == VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }

    *slot = reinterpret_cast<void*>(replacement);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

    DWORD ignored = 0;
    return FALSE != VirtualProtect(slot, sizeof(void*), old, &ignored);
}

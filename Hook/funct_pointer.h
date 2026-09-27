#pragma once
#include "pch.h"
#include <cstring>

// CEF objects begin with cef_base_ref_counted_t.size. Refuse slots outside it.
template<typename T> T get_funct_t(void* base, size_t offset) noexcept {
    if (!base) return nullptr;
    size_t size = *static_cast<const size_t*>(base);
    if (offset > size || sizeof(T) > size - offset) return nullptr;
    T result = nullptr;
    memcpy(&result, static_cast<const char*>(base) + offset, sizeof(result));
    return result;
}
template<typename T> bool overwrite_funct_t(void* base, size_t offset, T replacement) noexcept {
    if (!get_funct_t<T>(base, offset)) return false;
    auto slot = reinterpret_cast<T*>(static_cast<char*>(base) + offset);
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(T), PAGE_READWRITE, &old)) return false;
    *slot = replacement;
    DWORD unused = 0;
    return VirtualProtect(slot, sizeof(T), old, &unused) != FALSE;
}
struct CefString { wchar_t* str; size_t length; void (*dtor)(wchar_t*); };
using free_cef_string_t = void (*)(CefString*);

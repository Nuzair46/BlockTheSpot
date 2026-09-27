#include "pch.h"
#include "memory.h"
#include <set>

bool patch_instructions(void* base, size_t size, std::span<const bts::Write> writes) noexcept {
    if (!base || writes.empty()) return false;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    std::set<uintptr_t> pages;
    for (const auto& write : writes) {
        if (write.value.empty() || write.offset > size || write.value.size() > size - write.offset) return false;
        auto start = reinterpret_cast<uintptr_t>(base) + write.offset;
        const auto end = start + write.value.size();
        for (auto page = start - start % system.dwPageSize; page < end; page += system.dwPageSize) pages.insert(page);
    }
    struct Protection { void* address; DWORD previous; };
    std::vector<Protection> protections;
    for (auto page : pages) protections.push_back({reinterpret_cast<void*>(page), 0});
    size_t changed = 0;
    for (; changed < protections.size(); ++changed) {
        auto& page = protections[changed];
        if (!VirtualProtect(page.address, system.dwPageSize, PAGE_EXECUTE_READWRITE, &page.previous)) break;
    }
    // Change every affected page before writing anything. Each page retains its
    // own original protection, including writes that straddle page boundaries.
    bool success = changed == protections.size();
    auto bytes = static_cast<uint8_t*>(base);
    if (success) {
        for (const auto& write : writes) {
            memcpy(bytes + write.offset, write.value.data(), write.value.size());
            if (!FlushInstructionCache(GetCurrentProcess(), bytes + write.offset, write.value.size())) success = false;
        }
    }
    while (changed) {
        const auto& page = protections[--changed];
        DWORD unused = 0;
        if (!VirtualProtect(page.address, system.dwPageSize, page.previous, &unused)) success = false;
    }
    return success;
}

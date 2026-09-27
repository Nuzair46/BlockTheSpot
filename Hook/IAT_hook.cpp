#include "pch.h"
#include "IAT_hook.h"
#include "WinTrust_hook.h"

static FARPROC WINAPI GetProcAddress_hook(HMODULE hModule, LPCSTR lpProcName)
{
	if (!lpProcName || reinterpret_cast<uintptr_t>(lpProcName) <= 0xffff)
		return GetProcAddress_orig(hModule, lpProcName);

	if (0 == lstrcmpiA(lpProcName, "WinVerifyTrust")) {
		if (hModule == GetModuleHandleW(L"WinTrust.dll")) {
			return reinterpret_cast<FARPROC>(WinVerifyTrust_hook);
		}
	}

	return GetProcAddress_orig(hModule, lpProcName);
}

// Match the imported function name, since newer Spotify builds import it
// through api-ms-win-core-libraryloader-* rather than kernel32.dll.
bool hook_get_proc_address(HMODULE module, GetProcAddress_t replacement) noexcept
{
	if (!module || !ImageDirectoryEntryToDataEx) {
		return false;
	}

	ULONG size = 0;
	auto base = reinterpret_cast<BYTE*>(module);
	auto imports = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
		ImageDirectoryEntryToDataEx(module, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size, nullptr));
	if (!imports) {
		return false;
	}

	bool hooked = false;
	for (; imports->Name; ++imports) {
		auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imports->FirstThunk);
		auto names = imports->OriginalFirstThunk
			? reinterpret_cast<PIMAGE_THUNK_DATA>(base + imports->OriginalFirstThunk)
			: nullptr;
		for (size_t i = 0; thunk[i].u1.Function; ++i) {
			auto func = reinterpret_cast<PROC*>(&thunk[i].u1.Function);
			bool matches = *func == reinterpret_cast<PROC>(GetProcAddress);
			if (names) {
				if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) {
					continue;
				}
				auto name = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + names[i].u1.AddressOfData);
				matches = 0 == strcmp(reinterpret_cast<const char*>(name->Name), "GetProcAddress");
			}
			if (!matches) {
				continue;
			}
			if (*func == reinterpret_cast<PROC>(replacement)) {
				hooked = true;
				continue;
			}

			DWORD old_protect;
			if (!VirtualProtect(func, sizeof(PROC), PAGE_READWRITE, &old_protect)) {
				return false;
			}
			// Keep the system resolver, never a previously installed hook.
			if (!GetProcAddress_orig) {
				GetProcAddress_orig = GetProcAddress;
			}
			*func = reinterpret_cast<PROC>(replacement);
			DWORD unused;
			if (!VirtualProtect(func, sizeof(PROC), old_protect, &unused)) return false;
			hooked = true;
		}
	}
	return hooked;
}

bool process_IAT_hook_GetProcAddress(HMODULE module) noexcept
{
	return hook_get_proc_address(module, GetProcAddress_hook);
}

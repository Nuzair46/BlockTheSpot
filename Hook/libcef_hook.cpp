#include "pch.h"
#include "libcef_hook.h"
#include "IAT_hook.h"
#include "cef_url_hook.h"
#include "cef_zip_reader_hook.h"

static FARPROC WINAPI GetProcAddress_hook(HMODULE hModule, LPCSTR lpProcName)
{
	if (!lpProcName || 0 == HIWORD(lpProcName))
		return GetProcAddress_orig(hModule, lpProcName);

	if (0 == lstrcmpiA(lpProcName, "cef_urlrequest_create")) {
		if (hModule == GetModuleHandleW(L"libcef.dll")) {
			return reinterpret_cast<FARPROC>(cef_urlrequest_create_stub);
		}
	}
	if (0 == lstrcmpiA(lpProcName, "cef_zip_reader_create")) {
		if (hModule == GetModuleHandleW(L"libcef.dll")) {
			return reinterpret_cast<FARPROC>(cef_zip_reader_create_stub);
		}
	}
	return GetProcAddress_orig(hModule, lpProcName);
}

bool libcef_IAT_hook_GetProcAddress(HMODULE spotify_dll_handle) noexcept
{
	return hook_get_proc_address(spotify_dll_handle, GetProcAddress_hook);
}

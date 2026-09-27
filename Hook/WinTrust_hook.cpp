#include "pch.h"
#include "WinTrust_hook.h"

LONG WINAPI WinVerifyTrust_hook(HWND window, GUID* action, LPVOID data) {
    auto request = static_cast<WINTRUST_DATA*>(data);
    if (!request || request->cbStruct < sizeof(WINTRUST_DATA) || request->dwUnionChoice != WTD_CHOICE_FILE ||
        !request->pFile || !request->pFile->pcwszFilePath || original_chrome_elf.empty())
        return WinVerifyTrust(window, action, data);
    const wchar_t* name = wcsrchr(request->pFile->pcwszFilePath, L'\\');
    name = name ? name + 1 : request->pFile->pcwszFilePath;
    if (lstrcmpiW(name, L"chrome_elf.dll")) return WinVerifyTrust(window, action, data);
    // Use the original path without modifying caller-owned WINTRUST_DATA.
    WINTRUST_DATA copy = *request;
    WINTRUST_FILE_INFO file = *request->pFile;
    file.pcwszFilePath = original_chrome_elf.c_str();
    file.hFile = nullptr;
    copy.pFile = &file;
    LONG result = WinVerifyTrust(window, action, &copy);
    request->hWVTStateData = copy.hWVTStateData;
    return result;
}

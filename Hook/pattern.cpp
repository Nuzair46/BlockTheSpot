#include "pch.h"
#include "pattern.h"

bool get_text_section(HMODULE module, DLL_section* const dll_section) noexcept
{
	if (nullptr == dll_section || !module) {
		return false;
	}

	constexpr const char TEXT_STR[] = ".text";
	constexpr size_t TEXT_LEN = ARRAYSIZE(TEXT_STR) - 1;
	static_assert(
		TEXT_LEN <= IMAGE_SIZEOF_SHORT_NAME,
		"PE section name too long"
		);

	PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
	PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(
		reinterpret_cast<BYTE*>(module) + dos->e_lfanew
		);

	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
		if (0 == memcmp(section->Name, TEXT_STR, TEXT_LEN)) {
			dll_section->address = reinterpret_cast<BYTE*>(module) + section->VirtualAddress;
			dll_section->size = section->Misc.VirtualSize;
			return true;
		}
	}
	return false;
}

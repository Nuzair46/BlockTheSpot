#include "pch.h"
#include "pattern.h"

struct Hex_table
{
	BYTE data[256];
};

static inline constexpr BYTE hexchar_to_byte(char c)
{
	if (c >= '0' && c <= '9')
		return static_cast<BYTE>(c - '0');
	else if (c >= 'a' && c <= 'f')
		return static_cast<BYTE>(c - 'a' + 10);
	else if (c >= 'A' && c <= 'F')
		return static_cast<BYTE>(c - 'A' + 10);
	else
		return 0xFF;
}

static consteval Hex_table make_hex_table()
{
	Hex_table t{};

	for (int i = 0; i < 256; ++i)
		t.data[i] = hexchar_to_byte(static_cast<unsigned char>(i));

	return t;
}

static inline constexpr Hex_table lookup_hex = make_hex_table();

static inline constexpr BYTE hexchar(char c)
{
	return lookup_hex.data[(unsigned char)c];
}

static constexpr BYTE hex_pair(char hi, char lo)
{
	BYTE h = hexchar(hi);
	BYTE l = hexchar(lo);
	return (h | l) == 0xFF ? 0xFF : (BYTE)((h << 4) | l);
}

static constexpr bool is_pattern_space(char c) noexcept
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

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
	if (IMAGE_DOS_SIGNATURE != dos->e_magic) {
		return false;
	}

	PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(
		reinterpret_cast<BYTE*>(module) + dos->e_lfanew
		);
	if (IMAGE_NT_SIGNATURE != nt->Signature) {
		return false;
	}

	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
		if (0 == memcmp(section->Name, TEXT_STR, TEXT_LEN)) {
			dll_section->address = reinterpret_cast<BYTE*>(module) + section->VirtualAddress;
			dll_section->size = static_cast<size_t>(section->Misc.VirtualSize);
			return true;
		}
	}
	return false;
}

bool DataCompare(const BYTE* pData, const BYTE* bSig, const char* szMask, size_t pattern_size) noexcept
{
	if (!pData || !bSig || !szMask || 0 == pattern_size) {
		return false;
	}

	for (size_t i = 0; i < pattern_size; ++i) {
		if ('x' == szMask[i] && pData[i] != bSig[i]) {
			return false;
		}
	}
	return true;
}

BYTE* FindPattern(BYTE* dwAddress, size_t dwSize, const BYTE* pbSig, const char* szMask, size_t pattern_size) noexcept
{
	if (!dwAddress || !pbSig || !szMask || 0 == pattern_size || dwSize < pattern_size) {
		return nullptr;
	}

	const size_t last = dwSize - pattern_size;
	for (size_t i = 0; i <= last; ++i) {
		if (DataCompare(dwAddress + i, pbSig, szMask, pattern_size)) {
			return dwAddress + i;
		}
	}
	return nullptr;
}

size_t CountPatternMatches(BYTE* dwAddress, size_t dwSize, const BYTE* pbSig, const char* szMask, size_t pattern_size, size_t stop_after) noexcept
{
	if (!dwAddress || !pbSig || !szMask || 0 == pattern_size || dwSize < pattern_size || 0 == stop_after) {
		return 0;
	}

	size_t count = 0;
	const size_t last = dwSize - pattern_size;
	for (size_t i = 0; i <= last; ++i) {
		if (DataCompare(dwAddress + i, pbSig, szMask, pattern_size)) {
			++count;
			if (count >= stop_after) {
				break;
			}
		}
	}
	return count;
}

// return SIZE_MAX on error.
size_t parse_signature(
	const char* src,
	size_t src_len,
	BYTE* out_bytes,
	char* out_mask,
	size_t limit
) noexcept
{
	if (!src || !out_bytes || !out_mask || 0 == limit) {
		return SIZE_MAX;
	}

	size_t i = 0;
	size_t out = 0;

	while (i < src_len)
	{
		// skip whitespace
		const char c = src[i];
		if (is_pattern_space(c))
		{
			++i;
			continue;
		}

		// wildcard ??
		if (i + 1 < src_len && src[i] == '?' && src[i + 1] == '?')
		{
			if (out >= limit)
				return SIZE_MAX;

			out_bytes[out] = 0x00;
			out_mask[out] = '?';
			++out;
			i += 2;
			continue;
		}

		// need two chars for hex byte
		if (i + 1 >= src_len)
			return SIZE_MAX;

		const BYTE b = hex_pair(src[i], src[i + 1]);
		if (b == 0xFF)
			return SIZE_MAX;

		if (out >= limit)
			return SIZE_MAX;

		out_bytes[out] = b;
		out_mask[out] = 'x';
		++out;
		i += 2;
	}

	return out; // length of signature/mask
}

size_t parse_signaure(
	const char* src,
	size_t src_len,
	BYTE* out_bytes,
	char* out_mask,
	size_t limit
) noexcept
{
	return parse_signature(src, src_len, out_bytes, out_mask, limit);
}

size_t parse_hex(
	const char* src,
	size_t src_len,
	BYTE* out_bytes,
	size_t out_cap
) noexcept
{
	if (!src || !out_bytes || 0 == out_cap) {
		return SIZE_MAX;
	}

	size_t i = 0;
	size_t out = 0;

	while (i < src_len)
	{
		// skip whitespace
		const char c = src[i];
		if (is_pattern_space(c))
		{
			++i;
			continue;
		}

		// need two chars for a byte
		if (i + 1 >= src_len)
			return SIZE_MAX;  // invalid

		const BYTE b = hex_pair(src[i], src[i + 1]);
		if (b == 0xFF)
			return SIZE_MAX;  // invalid hex

		if (out >= out_cap)
			return SIZE_MAX;  // overflow

		out_bytes[out] = b;
		++out;
		i += 2;
	}

	return out;
}

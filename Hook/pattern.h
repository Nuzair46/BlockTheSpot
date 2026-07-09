#pragma once
#include "loader.h"

struct DLL_section
{
	size_t size;
	BYTE* address;
};

struct Modify
{
	uint8_t signature[SHARED_BUFFER_SIZE];
	char mask[SHARED_BUFFER_SIZE];
	UINT offset;
	uint8_t value[SHARED_BUFFER_SIZE];
	size_t patch_size;
};

// https://www.unknowncheats.me/forum/1064672-post23.html
bool DataCompare(const BYTE* pData, const BYTE* bSig, const char* szMask, size_t pattern_size) noexcept;
BYTE* FindPattern(BYTE* dwAddress, size_t dwSize, const BYTE* pbSig, const char* szMask, size_t pattern_size) noexcept;
size_t CountPatternMatches(BYTE* dwAddress, size_t dwSize, const BYTE* pbSig, const char* szMask, size_t pattern_size, size_t stop_after) noexcept;

bool get_text_section(HMODULE module, DLL_section* const dll_section) noexcept;

size_t parse_signature(
	const char* src,
	size_t src_len,
	BYTE* out_bytes,
	char* out_mask,
	size_t out_cap
) noexcept;

size_t parse_signaure(
	const char* src,
	size_t src_len,
	BYTE* out_bytes,
	char* out_mask,
	size_t out_cap
) noexcept;

size_t parse_hex(
	const char* src,
	size_t src_len,
	BYTE* out_bytes,
	size_t out_cap
) noexcept;

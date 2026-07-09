#include "pch.h"
#include "spotify_native_patch.h"
#include "loader.h"
#include "pattern.h"
#include "memory.h"
#include "log_thread.h"

static constexpr const char *SPOTIFY_NATIVE_PATCH_LIST_SECTION = "NativePatches";
static constexpr size_t MAX_SPOTIFY_NATIVE_PATCHES = 64;
static constexpr size_t MAX_SPOTIFY_NATIVE_PATCH_NAME = 128;

static inline void log_native_patch_debug(const char *section, const char *message) noexcept
{
	_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%s: %s", section, message);
	log_debug(shared_buffer);
}

static inline void log_native_patch_info(const char *section, const char *message) noexcept
{
	_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%s: %s", section, message);
	log_info(shared_buffer);
}

static inline bool is_spotify_native_patch_enabled(const char *section) noexcept
{
	const auto result = GetPrivateProfileIntA(
			section,
			"Enable",
			0,
			CONFIG_FILEA);
	return 0 != result;
}

static inline void apply_spotify_native_patch(HMODULE spotify_dll_handle, const char *section) noexcept
{
	Modify modify{};
	char signature_buffer[SHARED_BUFFER_SIZE]{};
	char value_buffer[SHARED_BUFFER_SIZE]{};

	const auto signature_raw_length = GetPrivateProfileStringA(
			section,
			"Signature",
			"",
			signature_buffer,
			sizeof(signature_buffer),
			CONFIG_FILEA);

	if (0 == signature_raw_length)
	{
		log_native_patch_debug(section, "Signature is empty.");
		return;
	}

	const auto signature_hex_size = parse_signature(signature_buffer,
																								 signature_raw_length,
																								 modify.signature,
																								 modify.mask,
																								 ARRAYSIZE(modify.signature) - 1);

	if (SIZE_MAX == signature_hex_size)
	{
		log_native_patch_debug(section, "parse_signature failed.");
		return;
	}

	if (0 == signature_hex_size)
	{
		log_native_patch_debug(section, "Signature parsed to zero bytes.");
		return;
	}

	modify.mask[signature_hex_size] = '\0';

	modify.offset = GetPrivateProfileIntA(
			section,
			"Offset",
			0,
			CONFIG_FILEA);

	const auto value_raw_length = GetPrivateProfileStringA(
			section,
			"Value",
			"",
			value_buffer,
			sizeof(value_buffer),
			CONFIG_FILEA);

	if (0 == value_raw_length)
	{
		log_native_patch_debug(section, "Value is empty.");
		return;
	}

	modify.patch_size = parse_hex(
			value_buffer,
			value_raw_length,
			modify.value,
			ARRAYSIZE(modify.value));

	if (SIZE_MAX == modify.patch_size)
	{
		log_native_patch_debug(section, "parse_hex failed.");
		return;
	}

	if (0 == modify.patch_size)
	{
		log_native_patch_debug(section, "Value parsed to zero bytes.");
		return;
	}

	const auto offset = static_cast<size_t>(modify.offset);
	if (offset > signature_hex_size || modify.patch_size > signature_hex_size - offset)
	{
		log_native_patch_debug(section, "patch range exceeds signature.");
		return;
	}

	DLL_section dll{};
	if (false == get_text_section(
									 spotify_dll_handle,
									 &dll))
	{
		log_native_patch_debug(section, "get_text_section failed.");
		return;
	}

	const auto matches = CountPatternMatches(
			dll.address,
			dll.size,
			modify.signature,
			reinterpret_cast<char *>(&modify.mask),
			signature_hex_size,
			2);
	if (0 == matches)
	{
		log_native_patch_debug(section, "FindPattern failed.");
		return;
	}
	if (1 != matches)
	{
		log_native_patch_debug(section, "signature matched more than once.");
		return;
	}

	const auto address = FindPattern(
			dll.address,
			dll.size,
			modify.signature,
			reinterpret_cast<char *>(&modify.mask),
			signature_hex_size);

	if (nullptr == address)
	{
		log_native_patch_debug(section, "FindPattern failed.");
		return;
	}

	const auto match_offset = static_cast<size_t>(address - dll.address);
	if (match_offset > dll.size ||
			offset > dll.size - match_offset ||
			modify.patch_size > dll.size - match_offset - offset)
	{
		log_native_patch_debug(section, "patch overflow.");
		return;
	}

	if (patch_instruction(address + offset, modify.value, modify.patch_size))
	{
		log_native_patch_info(section, "patch applied.");
		return;
	}
	log_native_patch_debug(section, "patch_instruction failed.");
}

static inline void apply_configured_spotify_native_patches(HMODULE spotify_dll_handle) noexcept
{
	char patch_section[MAX_SPOTIFY_NATIVE_PATCH_NAME]{};
	char patch_key[16]{};

	for (size_t i = 0; i < MAX_SPOTIFY_NATIVE_PATCHES; ++i)
	{
		const size_t display_idx = i + 1;
		_snprintf_s(patch_key, sizeof(patch_key), _TRUNCATE, "%zu", display_idx);
		const auto len = GetPrivateProfileStringA(
				SPOTIFY_NATIVE_PATCH_LIST_SECTION,
				patch_key,
				"",
				patch_section,
				sizeof(patch_section),
				CONFIG_FILEA);

		if (0 == len)
		{
			if (0 == i)
			{
				log_native_patch_debug(SPOTIFY_NATIVE_PATCH_LIST_SECTION, "no patch sections configured.");
			}
			return;
		}

		if (true == is_spotify_native_patch_enabled(patch_section))
		{
			apply_spotify_native_patch(spotify_dll_handle, patch_section);
		}
	}

	log_native_patch_debug(SPOTIFY_NATIVE_PATCH_LIST_SECTION, "patch list limit reached.");
}

void hook_spotify_native_patches(HMODULE spotify_dll_handle) noexcept
{
	if (!spotify_dll_handle)
	{
		log_native_patch_debug(SPOTIFY_NATIVE_PATCH_LIST_SECTION, "spotify.dll handle is null.");
		return;
	}
	apply_configured_spotify_native_patches(spotify_dll_handle);
}

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

	const auto signature_raw_length = GetPrivateProfileStringA(
			section,
			"Signature",
			"",
			shared_buffer,
			SHARED_BUFFER_SIZE,
			CONFIG_FILEA);

	const auto signature_hex_size = parse_signaure(shared_buffer,
																								 signature_raw_length,
																								 modify.signature,
																								 modify.mask,
																								 SHARED_BUFFER_SIZE);

	if (SIZE_MAX == signature_hex_size)
	{
		log_native_patch_debug(section, "parse_signaure limit exceed.");
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
			shared_buffer,
			SHARED_BUFFER_SIZE,
			CONFIG_FILEA);

	modify.patch_size = parse_hex(
			shared_buffer,
			value_raw_length,
			modify.value,
			SHARED_BUFFER_SIZE);

	if (SIZE_MAX == modify.patch_size)
	{
		log_native_patch_debug(section, "parse_hex limit exceed.");
		return;
	}

	if (modify.patch_size > signature_hex_size)
	{
		log_native_patch_debug(section, "patch_size > signature_hex_size.");
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

	const auto address = FindPattern(
			dll.address,
			dll.size,
			modify.signature,
			reinterpret_cast<char *>(&modify.mask));

	if (nullptr == address)
	{
		log_native_patch_debug(section, "FindPattern failed.");
		return;
	}

	const auto start = address + modify.offset + modify.patch_size;
	const auto end = dll.address + dll.size;
	if (start > end)
	{
		log_native_patch_debug(section, "patch overflow.");
		return;
	}

	if (address)
	{
		patch_instruction(reinterpret_cast<LPVOID *>(address + modify.offset), modify.value, modify.patch_size);
		log_native_patch_info(section, "patch applied.");
		return;
	}
	log_native_patch_debug(section, "fail to patch.");
}

static inline void apply_configured_spotify_native_patches(HMODULE spotify_dll_handle) noexcept
{
	char patch_section[MAX_SPOTIFY_NATIVE_PATCH_NAME]{};

	for (size_t i = 0; i < MAX_SPOTIFY_NATIVE_PATCHES; ++i)
	{
		const size_t display_idx = i + 1;
		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%zu", display_idx);
		const auto len = GetPrivateProfileStringA(
				SPOTIFY_NATIVE_PATCH_LIST_SECTION,
				shared_buffer,
				"",
				patch_section,
				sizeof(patch_section),
				CONFIG_FILEA);

		if (0 == len)
		{
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
	apply_configured_spotify_native_patches(spotify_dll_handle);
}

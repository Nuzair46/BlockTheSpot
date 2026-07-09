#include "pch.h"
#include "css_cosmetic.h"
#include "config.h"
#include "loader.h"
#include "log_thread.h"
#include "pattern.h"

void vbar_noop(const char* file_name, void* buffer, size_t bufferSize) noexcept {}

static inline bool is_homepage_vbar_hide() noexcept
{
	auto result = GetPrivateProfileIntA(
		"Homepage_vbar",
		"Enable",
		0,
		get_frontend_patch_config_file()
	);
	return 0 != result;
}

static inline void do_hide_vbar(const char* file_name, void* buffer, size_t bufferSize) noexcept
{
	if (!file_name || !buffer || 0 == bufferSize) {
		return;
	}

	static char vbar_buffer[2048]{};
	size_t len = strnlen_s(file_name, 128);
	if (len < 4 || 0 != lstrcmpiA(file_name + len - 4, ".css")) {
		return;
	}
	
	Modify modify{};

	const auto signature_raw_length = GetPrivateProfileStringA(
		"Homepage_vbar",
		"Signature",
		"",
		vbar_buffer,
		sizeof(vbar_buffer),
		get_frontend_patch_config_file()
	);

	if (0 == signature_raw_length) {
		log_debug("do_hide_vbar: Signature is empty.");
		return;
	}

	const auto signature_hex_size = parse_signature(vbar_buffer,
		signature_raw_length,
		modify.signature,
		modify.mask,
		ARRAYSIZE(modify.signature) - 1);

	if (SIZE_MAX == signature_hex_size) {
		log_debug("do_hide_vbar: parse_signature failed.");
		return;
	}

	if (0 == signature_hex_size) {
		log_debug("do_hide_vbar: Signature parsed to zero bytes.");
		return;
	}

	modify.mask[signature_hex_size] = '\0';

	modify.offset = GetPrivateProfileIntA(
		"Homepage_vbar",
		"Offset",
		0,
		get_frontend_patch_config_file()
	);

	const auto value_raw_length = GetPrivateProfileStringA(
		"Homepage_vbar",
		"Value",
		"",
		vbar_buffer,
		sizeof(vbar_buffer),
		get_frontend_patch_config_file()
	);

	if (0 == value_raw_length) {
		log_debug("do_hide_vbar: Value is empty.");
		return;
	}

	modify.patch_size = parse_hex(
		vbar_buffer,
		value_raw_length,
		modify.value,
		ARRAYSIZE(modify.value)
	);

	if (SIZE_MAX == modify.patch_size) {
		log_debug("do_hide_vbar: parse_hex failed.");
		return;
	}

	if (0 == modify.patch_size) {
		log_debug("do_hide_vbar: Value parsed to zero bytes.");
		return;
	}

	const auto offset = static_cast<size_t>(modify.offset);
	if (offset > signature_hex_size || modify.patch_size > signature_hex_size - offset) {
		log_debug("do_hide_vbar: patch range exceeds signature.");
		return;
	}

	const auto address = FindPattern(
		reinterpret_cast<BYTE*>(buffer),
		bufferSize,
		modify.signature,
		reinterpret_cast<char*>(&modify.mask),
		signature_hex_size
	);

	if (nullptr == address) {
		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_hide_vbar: %s FindPattern failed.", file_name);
		log_debug(shared_buffer);
		return;
	}

	if (buffer != address) {
		// it the first in the css file...
		return;
	}

	if (offset > bufferSize || modify.patch_size > bufferSize - offset) {
		log_debug("do_hide_vbar: patch overflow.");
		return;
	}

	memcpy(address + offset, modify.value, modify.patch_size);
	_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_hide_vbar: %s patched.", file_name);
	log_info(shared_buffer);
}

void modify_css_init() noexcept
{
	if (true == is_homepage_vbar_hide()) {
		css_hide_vbar = do_hide_vbar;
	}
}

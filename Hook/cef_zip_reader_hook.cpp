#include "pch.h"
#include "cef_zip_reader_hook.h"
#include "loader.h"
#include "funct_pointer.h"
#include "log_thread.h"
#include "pattern.h"
#include "IAT_hook.h"
#include "css_cosmetic.h"

static inline size_t cef_buffer_modify_count = 0;
static inline char cef_buffer_list[MAX_CEF_BUFFER_MODIFY_LIST][MAX_URL_LEN] = {};
static inline size_t debug_dumped_file_count = 0;
static inline char debug_dumped_files[MAX_CEF_BUFFER_MODIFY_LIST][MAX_URL_LEN] = {};

static bool debug_enabled() noexcept
{
	return 0 != GetPrivateProfileIntA("Debug", "Enable", 0, CONFIG_FILEA);
}

static bool debug_signature_log_enabled() noexcept
{
	return debug_enabled();
}

static void debug_make_safe_dump_name(const char* file_name, char* out, size_t out_size) noexcept
{
	if (!out || 0 == out_size) {
		return;
	}

	size_t i = 0;
	for (; file_name && file_name[i] && i + 1 < out_size; ++i) {
		const char c = file_name[i];
		switch (c) {
		case '/':
		case '\\':
		case ':':
		case '*':
		case '?':
		case '"':
		case '<':
		case '>':
		case '|':
			out[i] = '_';
			break;
		default:
			out[i] = c;
			break;
		}
	}
	out[i] = '\0';
}

static bool debug_file_already_dumped(const char* file_name) noexcept
{
	for (size_t i = 0; i < debug_dumped_file_count; ++i) {
		if (0 == lstrcmpiA(file_name, debug_dumped_files[i])) {
			return true;
		}
	}
	return false;
}

static void debug_mark_file_dumped(const char* file_name) noexcept
{
	if (debug_dumped_file_count >= MAX_CEF_BUFFER_MODIFY_LIST) {
		return;
	}

	strncpy_s(debug_dumped_files[debug_dumped_file_count], MAX_URL_LEN, file_name, _TRUNCATE);
	++debug_dumped_file_count;
}

static void debug_dump_configured_file(const char* file_name, const void* buffer, size_t bufferSize) noexcept
{
	if (!file_name || !buffer || 0 == bufferSize || bufferSize > MAXDWORD) {
		return;
	}

	if (!debug_enabled()) {
		return;
	}

	if (debug_file_already_dumped(file_name)) {
		return;
	}
	debug_mark_file_dumped(file_name);

	char dump_dir[MAX_PATH]{};
	strcpy_s(dump_dir, "debugjs");

	if (FALSE == CreateDirectoryA(dump_dir, nullptr) && ERROR_ALREADY_EXISTS != GetLastError()) {
		return;
	}

	char safe_name[MAX_PATH]{};
	debug_make_safe_dump_name(file_name, safe_name, sizeof(safe_name));
	if ('\0' == safe_name[0]) {
		return;
	}

	char output_path[MAX_PATH]{};
	if (_snprintf_s(output_path, sizeof(output_path), _TRUNCATE, "%s\\%s", dump_dir, safe_name) < 0) {
		return;
	}

	const HANDLE file = CreateFileA(
		output_path,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (INVALID_HANDLE_VALUE == file) {
		return;
	}

	DWORD written = 0;
	if (FALSE != WriteFile(
		file,
		buffer,
		static_cast<DWORD>(bufferSize),
		&written,
		nullptr)) {
		_snprintf_s(
			shared_buffer,
			SHARED_BUFFER_SIZE,
			_TRUNCATE,
			"debug_dump_configured_file: wrote %s (%lu bytes)",
			output_path,
			static_cast<unsigned long>(written)
		);
		log_info(shared_buffer);
	}

	CloseHandle(file);
}

using cef_zip_reader_create_t = void* (*)(void* stream);
static inline cef_zip_reader_create_t cef_zip_reader_create_orig = nullptr;
static inline cef_zip_reader_create_t cef_zip_reader_create_impl = nullptr;

using cef_zip_reader_read_file_t = int(CALLBACK*)(void* self, void* buffer, size_t bufferSize);
static cef_zip_reader_read_file_t cef_zip_reader_read_file_orig = nullptr;

// compare file name in spa vs config.ini
static bool need_patch(const char* in_file) noexcept {
	for (size_t i = 0; i < cef_buffer_modify_count; ++i) {
		const char* target = cef_buffer_list[i];

		if (0 == lstrcmpiA(in_file, target)) {
			return true;
		}
	}
	return false;
}

static inline bool do_patch_buffer(const char* file_name, const char* patch_name, void* buffer, size_t bufferSize) noexcept
{
	constexpr auto PAIR_MODIFY = 2;
	Modify modify[PAIR_MODIFY] = {};

	char temp_buffer[SHARED_BUFFER_SIZE];
	size_t modify_count = 0;

	for (size_t i = 0; i < PAIR_MODIFY; ++i) {
		const size_t display_idx = i + 1;
		// get signature
		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "Signature_%zu", display_idx);
		const auto signature_raw_length = GetPrivateProfileStringA(
			patch_name,
			shared_buffer,
			"",
			temp_buffer,
			SHARED_BUFFER_SIZE,
			CONFIG_FILEA
		);

		if (0 == signature_raw_length) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_patch_buffer: %s %s signature_%zu empty, stop processing", file_name, patch_name, display_idx);
			log_debug(shared_buffer);
			break;
		}

		const auto signature_hex_size = parse_signaure(temp_buffer,
			signature_raw_length,
			modify[i].signature,
			modify[i].mask,
			SHARED_BUFFER_SIZE);

		if (SIZE_MAX == signature_hex_size) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_patch_buffer: %s %s signature_%zu parse fail, limit exceed", file_name, patch_name, display_idx);
			log_debug(shared_buffer);
			return false;
		}

		modify[i].mask[signature_hex_size] = '\0';

		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "Offset_%zu", display_idx);
		modify[i].offset = GetPrivateProfileIntA(
			patch_name,
			shared_buffer,
			0,
			CONFIG_FILEA
		);

		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "Value_%zu", display_idx);
		const auto value_raw_length = GetPrivateProfileStringA(
			patch_name,
			shared_buffer,
			"",
			temp_buffer,
			SHARED_BUFFER_SIZE,
			CONFIG_FILEA
		);

		modify[i].patch_size = parse_hex(
			temp_buffer,
			value_raw_length,
			modify[i].value,
			SHARED_BUFFER_SIZE
		);

		if (SIZE_MAX == modify[i].patch_size) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_patch_buffer: %s %s signature_%zu parse hex limit exceed", file_name, patch_name, display_idx);
			log_debug(shared_buffer);
			return false;
		}

		if (modify[i].patch_size > signature_hex_size) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_patch_buffer: %s %s signature_%zu patch_size > signature_hex_size", file_name, patch_name, display_idx);
			log_debug(shared_buffer);
			return false;
		}

		if (debug_signature_log_enabled()) {
			_snprintf_s(
				shared_buffer,
				SHARED_BUFFER_SIZE,
				_TRUNCATE,
				"debug_signature: %s %s signature_%zu bytes=%zu offset=%u value_bytes=%zu",
				file_name,
				patch_name,
				display_idx,
				signature_hex_size,
				modify[i].offset,
				modify[i].patch_size
			);
			log_debug(shared_buffer);
		}

		modify_count = display_idx;
	}

	if (0 == modify_count) {
		return false;
	}

	for (size_t i = 0; i < modify_count; ++i) {
		const size_t display_idx = i + 1;
		const auto address = FindPattern(
			reinterpret_cast<BYTE*>(buffer),
			static_cast<DWORD>(bufferSize),
			modify[i].signature,
			reinterpret_cast<char*>(&modify[i].mask)
		);
		if (nullptr == address) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "do_patch_buffer: %s %s signature_%zu FindPattern failed.", file_name, patch_name, display_idx);
			log_debug(shared_buffer);
			return false;
		}
		if (debug_signature_log_enabled()) {
			_snprintf_s(
				shared_buffer,
				SHARED_BUFFER_SIZE,
				_TRUNCATE,
				"debug_signature: %s %s signature_%zu matched=%p patch_at=%p",
				file_name,
				patch_name,
				display_idx,
				address,
				address + modify[i].offset
			);
			log_debug(shared_buffer);
		}
		memcpy(address + modify[i].offset, modify[i].value, modify[i].patch_size);
	}

	return true;
}

static void patch_file(const char* file_name, void* buffer, size_t bufferSize) noexcept
{
	char patch_name[MAX_URL_LEN]{};

	for (size_t i = 0; i < MAX_CEF_BUFFER_MODIFY_LIST; ++i) {
		const size_t display_idx = i + 1;
		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%zu", display_idx);
		const auto len = GetPrivateProfileStringA(
			file_name,
			shared_buffer,
			"",
			patch_name,
			MAX_URL_LEN,
			CONFIG_FILEA
		);

		if (0 == len) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%s buffer modify %zu: empty, stop processing", file_name, display_idx);
			log_debug(shared_buffer);
			break;
		}
		do_patch_buffer(file_name, patch_name, buffer, bufferSize);
	}
}

#ifdef USE_LIBCEF
int CALLBACK cef_zip_reader_t_read_file_hook(struct _cef_zip_reader_t* self, void* buffer, size_t bufferSize)
#else
int CALLBACK cef_zip_reader_read_file_hook(void* self, void* buffer, size_t bufferSize)
#endif
{
	int _retval = cef_zip_reader_read_file_orig(self, buffer, bufferSize);

#ifdef USE_LIBCEF
	std::wstring file_name = Utils::ToString(self->get_file_name(self)->str);
#else
	using get_file_name_t = void* (__stdcall*)(void*);
	const auto get_file_name = get_funct_t<get_file_name_t>(
		self, CEF_ZIP_READER_GET_FILE_NAME_OFFSET);
	const wchar_t* file_name = *reinterpret_cast<wchar_t**>(get_file_name(self));
#endif

	char ansi_file_name[MAX_URL_LEN];
	const auto len = WideCharToMultiByte(CP_ACP, 0, file_name, -1, ansi_file_name, MAX_URL_LEN, NULL, NULL);
	if (0 == len) {
		return _retval;
	}

	const bool do_patch = need_patch(ansi_file_name);

	char log_buf[256]{};
	_snprintf_s(
		log_buf,
		sizeof(log_buf),
		_TRUNCATE,
		"cef_zip_reader_read_file_hook: %s %s",
		do_patch ? "patching" : "skip",
		ansi_file_name
	);
	log_debug(log_buf);

	if (true == do_patch) {
		debug_dump_configured_file(ansi_file_name, buffer, bufferSize);
		patch_file(ansi_file_name, buffer, bufferSize);
	}
	css_hide_vbar(ansi_file_name, buffer, bufferSize);

	return _retval;
}

void* cef_zip_reader_create_stub(void* stream)
{
	return cef_zip_reader_create_impl(stream);
}

#ifdef USE_LIBCEF
cef_zip_reader_t* cef_zip_reader_create_hook(cef_stream_reader_t* stream)
#else
void* cef_zip_reader_create_hook(void* stream)
#endif
{
#ifdef USE_LIBCEF
	cef_zip_reader_t* zip_reader = (cef_zip_reader_t*)cef_zip_reader_create_orig(stream);
	cef_zip_reader_t_read_file_orig = (_cef_zip_reader_t_read_file)zip_reader->read_file;
#else
	auto zip_reader = cef_zip_reader_create_orig(stream);
	cef_zip_reader_read_file_orig =
		get_funct_t<cef_zip_reader_read_file_t>(
			zip_reader, CEF_ZIP_READER_GET_READ_FILE_OFFSET);
	overwrite_funct_t<cef_zip_reader_read_file_t>(
		zip_reader, CEF_ZIP_READER_GET_READ_FILE_OFFSET, cef_zip_reader_read_file_hook);
#endif
	return zip_reader;
}

static inline void do_hook_cef_zip_reader(HMODULE libcef_dll_handle) noexcept
{
	cef_zip_reader_create_impl = cef_zip_reader_create_hook;
	log_debug("do_hook_cef_zip_reader: cef_zip_reader_create_impl = cef_zip_reader_create_hook.");
	log_info("do_hook_cef_zip_reader: patch applied.");
}

static inline void load_cef_reader_config()
{
	CEF_ZIP_READER_GET_READ_FILE_OFFSET = GetPrivateProfileIntA(
		"LIBCEF",
		"CEF_ZIP_READER_GET_READ_FILE_OFFSET",
		static_cast<INT>(CEF_ZIP_READER_GET_READ_FILE_OFFSET),
		CONFIG_FILEA
	);

	CEF_ZIP_READER_GET_FILE_NAME_OFFSET = GetPrivateProfileIntA(
		"LIBCEF",
		"CEF_ZIP_READER_GET_FILE_NAME_OFFSET",
		static_cast<INT>(CEF_ZIP_READER_GET_FILE_NAME_OFFSET),
		CONFIG_FILEA
	);

	for (size_t i = 0; i < MAX_CEF_BUFFER_MODIFY_LIST; ++i) {
		const size_t display_idx = i + 1;
		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%zu", display_idx);
		const auto len = GetPrivateProfileStringA(
			"Buffer_modify",
			shared_buffer,
			"",
			cef_buffer_list[i],
			MAX_URL_LEN,
			CONFIG_FILEA
		);
		if (0 == len) {
			_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "Load buffer modify %zu: fail, stop processing", display_idx);
			log_debug(shared_buffer);
			cef_buffer_modify_count = i;
			break;
		}
		_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "Load buffer modify %zu:%s", display_idx, cef_buffer_list[i]);
		log_debug(shared_buffer);
	}
	_snprintf_s(shared_buffer, SHARED_BUFFER_SIZE, _TRUNCATE, "%zu modify list loaded", cef_buffer_modify_count);
	log_info(shared_buffer);
}

static inline bool is_cef_reader_hook() noexcept
{
	auto is_enable = GetPrivateProfileIntA(
		"Buffer_modify",
		"Enable",
		0,
		CONFIG_FILEA
	);
	return 0 != is_enable;
}

void hook_cef_reader(HMODULE libcef_dll_handle) noexcept
{
	cef_zip_reader_create_orig =
		reinterpret_cast<cef_zip_reader_create_t>(
			GetProcAddress_orig(libcef_dll_handle, "cef_zip_reader_create"));
	cef_zip_reader_create_impl = cef_zip_reader_create_orig;

	if (true == is_cef_reader_hook()) {
		load_cef_reader_config();
		do_hook_cef_zip_reader(libcef_dll_handle);
	}
}

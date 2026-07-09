#pragma once
#include "loader.h"

inline constexpr auto DEFAULT_NATIVE_PATCH_CONFIG_FILEA = "./patches/native.ini";
inline constexpr auto DEFAULT_FRONTEND_PATCH_CONFIG_FILEA = "./patches/frontend.ini";

inline char native_patch_config_file[MAX_PATH] = "./patches/native.ini";
inline char frontend_patch_config_file[MAX_PATH] = "./patches/frontend.ini";
inline bool patch_config_files_loaded = false;

static inline void load_patch_config_files() noexcept
{
	if (patch_config_files_loaded) {
		return;
	}

	GetPrivateProfileStringA(
		"PatchFiles",
		"Native",
		DEFAULT_NATIVE_PATCH_CONFIG_FILEA,
		native_patch_config_file,
		MAX_PATH,
		CONFIG_FILEA
	);
	if ('\0' == native_patch_config_file[0]) {
		strcpy_s(native_patch_config_file, MAX_PATH, DEFAULT_NATIVE_PATCH_CONFIG_FILEA);
	}

	GetPrivateProfileStringA(
		"PatchFiles",
		"Frontend",
		DEFAULT_FRONTEND_PATCH_CONFIG_FILEA,
		frontend_patch_config_file,
		MAX_PATH,
		CONFIG_FILEA
	);
	if ('\0' == frontend_patch_config_file[0]) {
		strcpy_s(frontend_patch_config_file, MAX_PATH, DEFAULT_FRONTEND_PATCH_CONFIG_FILEA);
	}

	patch_config_files_loaded = true;
}

static inline const char* get_native_patch_config_file() noexcept
{
	load_patch_config_files();
	return native_patch_config_file;
}

static inline const char* get_frontend_patch_config_file() noexcept
{
	load_patch_config_files();
	return frontend_patch_config_file;
}

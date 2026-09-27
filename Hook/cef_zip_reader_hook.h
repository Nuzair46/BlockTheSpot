#pragma once
#include "loader.h"
void hook_cef_reader(HMODULE libcef) noexcept;
bool cef_reader_ready() noexcept;
void* cef_zip_reader_create_stub(void* stream);

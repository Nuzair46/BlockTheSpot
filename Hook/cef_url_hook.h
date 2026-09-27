#pragma once
#include "loader.h"
void hook_cef_url(HMODULE libcef) noexcept;
bool cef_url_ready() noexcept;
void* cef_urlrequest_create_stub(void* request, void* client, void* context);

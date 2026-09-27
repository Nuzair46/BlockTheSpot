#include "pch.h"
#include "cef_url_hook.h"
#include "funct_pointer.h"
#include "log_thread.h"
#include "../Common/url.h"
#include "mod_loader.h"

namespace {
using create_t = void* (*)(void*, void*, void*);
create_t original = nullptr;
free_cef_string_t free_string = nullptr;
bool enabled = false;
std::vector<UrlMod> mods;
}

bool cef_url_ready() noexcept { return enabled; }

void* cef_urlrequest_create_stub(void* request, void* client, void* context) {
    using get_url_t = CefString* (__stdcall*)(void*);
    auto get_url = get_funct_t<get_url_t>(request, runtime_config.url_offset);
    if (!get_url) {
        for (const auto& mod : mods) set_status(mod.owner, "failed", "CEF request layout is incompatible");
        return original(request, client, context);
    }
    auto raw = get_url(request);
    if (!raw) return original(request, client, context);
    bool blocked = false;
    if (raw->str) {
        auto path = bts::url_path(std::wstring_view(raw->str, raw->length));
        for (const auto& mod : mods) {
            for (const auto& rule : mod.rules) if (path.find(rule) != std::wstring_view::npos) {
                blocked = true;
                set_status(mod.owner, "active", "matching requests blocked");
                break;
            }
        }
    }
    free_string(raw);
    log_debug(blocked ? "URL request blocked (query redacted)" : "URL request allowed (query redacted)");
    return blocked ? nullptr : original(request, client, context);
}

void hook_cef_url(HMODULE libcef) noexcept {
    mods = url_mods();
    if (!compatible_spotify || mods.empty()) return;
    original = reinterpret_cast<create_t>(GetProcAddress(libcef, "cef_urlrequest_create"));
    free_string = reinterpret_cast<free_cef_string_t>(GetProcAddress(libcef, "cef_string_userfree_utf16_free"));
    enabled = original && free_string;
    for (const auto& mod : mods)
        set_status(mod.owner, enabled ? "ready" : "failed",
            enabled ? "waiting for requests" : "required CEF exports missing");
}

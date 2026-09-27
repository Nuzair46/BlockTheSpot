#include "pch.h"
#include "cef_url_hook.h"
#include "funct_pointer.h"
#include "log_thread.h"
#include "../Common/url.h"
#include <atomic>

namespace {
using create_t = void* (*)(void*, void*, void*);
create_t original = nullptr;
free_cef_string_t free_string = nullptr;
bool enabled = false;
std::vector<std::wstring> rules;
std::atomic<bool> reported_block = false;
}

bool cef_url_ready() noexcept { return enabled; }

void* cef_urlrequest_create_stub(void* request, void* client, void* context) {
    using get_url_t = CefString* (__stdcall*)(void*);
    auto get_url = get_funct_t<get_url_t>(request, runtime_config.url_offset);
    if (!get_url) {
        set_status("URL_block", "failed", "CEF request layout is incompatible");
        return original(request, client, context);
    }
    auto raw = get_url(request);
    if (!raw) return original(request, client, context);
    bool blocked = false;
    if (raw->str) {
        auto path = bts::url_path(std::wstring_view(raw->str, raw->length));
        for (const auto& rule : rules) if (path.find(rule) != std::wstring_view::npos) { blocked = true; break; }
    }
    free_string(raw);
    if (blocked && !reported_block.exchange(true)) set_status("URL_block", "active", "matching requests blocked");
    log_debug(blocked ? "URL request blocked (query redacted)" : "URL request allowed (query redacted)");
    return blocked ? nullptr : original(request, client, context);
}

void hook_cef_url(HMODULE libcef) noexcept {
    if (!runtime_config.urls_enabled || !compatible_spotify) {
        set_status("URL_block", "skipped", compatible_spotify ? "disabled" : "unsupported Spotify version"); return;
    }
    original = reinterpret_cast<create_t>(GetProcAddress(libcef, "cef_urlrequest_create"));
    free_string = reinterpret_cast<free_cef_string_t>(GetProcAddress(libcef, "cef_string_userfree_utf16_free"));
    if (!original || !free_string) { set_status("URL_block", "failed", "required CEF exports missing"); return; }
    for (const auto& rule : runtime_config.urls) rules.emplace_back(rule.begin(), rule.end());
    enabled = !rules.empty();
    set_status("URL_block", enabled ? "ready" : "skipped", enabled ? "waiting for requests" : "empty rule list");
}

#include "pch.h"
#include "cef_zip_reader_hook.h"
#include "funct_pointer.h"
#include "log_thread.h"
#include "css_cosmetic.h"
#include <atomic>

namespace {
using create_t = void* (*)(void*);
using read_t = int(CALLBACK*)(void*, void*, size_t);
create_t original_create = nullptr;
std::atomic<read_t> original_read = nullptr;
free_cef_string_t free_string = nullptr;
bool enabled = false;

int CALLBACK read_file(void* self, void* buffer, size_t capacity) {
    auto read = original_read.load();
    int count = read(self, buffer, capacity);
    if (count <= 0 || !buffer) return count;
    if (static_cast<size_t>(count) > capacity) { set_status("SPA", "failed", "CEF returned an invalid read length"); return count; }
    using name_t = CefString* (__stdcall*)(void*);
    auto get_name = get_funct_t<name_t>(self, runtime_config.name_offset);
    if (!get_name) { set_status("SPA", "failed", "CEF filename member is missing"); return count; }
    auto raw = get_name(self);
    if (!raw) return count;
    std::string name;
    if (raw->str) {
        // SPA entry names are ASCII; skip anything else without lossy conversion.
        for (size_t i = 0; i < raw->length; ++i) {
            if (raw->str[i] > 127 || raw->str[i] == 0) { name.clear(); break; }
            name += static_cast<char>(raw->str[i]);
        }
    }
    free_string(raw);
    if (name.empty()) return count;
    const bts::FilePatch* target = nullptr;
    if (runtime_config.buffers_enabled) {
        for (const auto& file : runtime_config.files) if (file.file == name) { target = &file; break; }
    }
    bool css = runtime_config.css_enabled && std::string_view(name).ends_with(".css");
    if (!target && !css) return count;
    // Full-file transactions cannot safely be applied after earlier chunks
    // have already been returned to CEF. Leave split reads untouched.
    using size_t_fn = int64_t(__stdcall*)(void*);
    auto get_size = get_funct_t<size_t_fn>(self, runtime_config.name_offset + sizeof(void*));
    auto tell = get_funct_t<size_t_fn>(self, runtime_config.read_offset + sizeof(void*));
    if (!get_size || !tell || get_size(self) != count || tell(self) != count) {
        set_status(target ? name : "Homepage_vbar", "skipped", "partial ZIP read; buffer unchanged"); return count;
    }
    if (target) {
        std::string error;
        if (!bts::apply({static_cast<uint8_t*>(buffer), static_cast<size_t>(count)}, target->patches, error))
            set_status(name, "failed", error);
        else set_status(name, "applied", std::to_string(target->patches.size()) + " validated writes");
    }
    if (css) css_hide_vbar(name.c_str(), buffer, static_cast<size_t>(count));
    return count;
}
}

bool cef_reader_ready() noexcept { return enabled; }

void* cef_zip_reader_create_stub(void* stream) {
    void* reader = original_create(stream);
    if (!reader) return nullptr;
    auto read = get_funct_t<read_t>(reader, runtime_config.read_offset);
    if (!read) { set_status("SPA", "failed", "CEF reader layout is incompatible"); return reader; }
    if (read == read_file) return reader;
    read_t expected = nullptr;
    if (!original_read.compare_exchange_strong(expected, read) && expected != read) {
        set_status("SPA", "failed", "reader callback changed; object left unmodified"); return reader;
    }
    if (!overwrite_funct_t(reader, runtime_config.read_offset, read_file))
        set_status("SPA", "failed", "unable to protect reader callback");
    return reader;
}

void hook_cef_reader(HMODULE libcef) noexcept {
    enabled = compatible_spotify && (runtime_config.buffers_enabled || runtime_config.css_enabled);
    for (const auto& target : runtime_config.files)
        set_status(target.file, enabled && runtime_config.buffers_enabled ? "pending" : "skipped",
            !compatible_spotify ? "unsupported Spotify version" : runtime_config.buffers_enabled ? "not loaded yet" : "disabled");
    set_status("Homepage_vbar", enabled && runtime_config.css_enabled ? "pending" : "skipped",
        runtime_config.css_enabled ? "waiting for matching CSS" : "disabled");
    if (!enabled) return;
    original_create = reinterpret_cast<create_t>(GetProcAddress(libcef, "cef_zip_reader_create"));
    free_string = reinterpret_cast<free_cef_string_t>(GetProcAddress(libcef, "cef_string_userfree_utf16_free"));
    if (!original_create || !free_string) { enabled = false; set_status("SPA", "failed", "required CEF exports missing"); return; }
    set_status("SPA", "ready", "waiting for ZIP reads");
}

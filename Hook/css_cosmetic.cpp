#include "pch.h"
#include "css_cosmetic.h"
#include "log_thread.h"

void css_hide_vbar(const char* file_name, void* buffer, size_t size) noexcept {
    if (!compatible_spotify || !runtime_config.css_enabled || !std::string_view(file_name).ends_with(".css")) return;
    std::vector<bts::Write> writes;
    std::string error;
    if (!bts::plan({static_cast<uint8_t*>(buffer), size}, {&runtime_config.css, 1}, writes, error)) return;
    size_t match = writes.front().offset - runtime_config.css.offset;
    if (match != 0) return;
    std::copy(writes.front().value.begin(), writes.front().value.end(), static_cast<uint8_t*>(buffer) + writes.front().offset);
    set_status("Homepage_vbar", "applied", file_name);
}

#include "pch.h"
#include "developer_mode.h"
#include "pattern.h"
#include "memory.h"
#include "log_thread.h"

void hook_developer_mode(HMODULE module) noexcept {
    if (!runtime_config.developer_enabled || !compatible_spotify) {
        set_status("Developer", "skipped", compatible_spotify ? "disabled" : "unsupported Spotify version");
        return;
    }
    DLL_section section{};
    if (!get_text_section(module, &section)) { set_status("Developer", "failed", "missing .text section"); return; }
    std::vector<bts::Write> writes;
    std::string error;
    if (!bts::plan({section.address, section.size}, {&runtime_config.developer, 1}, writes, error)) {
        set_status("Developer", "failed", error); return;
    }
    const auto& write = writes.front();
    if (!patch_instruction(section.address + write.offset, write.value.data(), write.value.size())) {
        set_status("Developer", "failed", "memory protection or instruction-cache update failed"); return;
    }
    set_status("Developer", "applied");
}

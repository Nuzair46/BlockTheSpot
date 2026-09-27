#pragma once
#include "loader.h"
#include <string_view>

void init_log(int level) noexcept;
void log_debug(const char* message) noexcept;
void log_info(const char* message) noexcept;
void log_error(const char* message) noexcept;
void set_status(std::string_view feature, std::string_view state, std::string_view detail = {});

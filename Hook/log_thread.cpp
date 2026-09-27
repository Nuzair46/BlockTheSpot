#include "pch.h"
#include "log_thread.h"
#include <map>
#include <string>

namespace {
SRWLOCK log_lock = SRWLOCK_INIT;
SRWLOCK status_lock = SRWLOCK_INIT;
HANDLE log_file = INVALID_HANDLE_VALUE;
int log_level = 0;
size_t log_bytes = 0;
constexpr size_t max_log_bytes = 1024 * 1024;
std::map<std::string, std::string> statuses;

void open_log() {
    auto path = install_directory + L"\\blockthespot.log";
    log_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER size{};
    if (log_file != INVALID_HANDLE_VALUE && GetFileSizeEx(log_file, &size))
        log_bytes = static_cast<size_t>(size.QuadPart);
}

void write_log(int level, const char* label, const char* message) noexcept {
    if (!message || level > log_level) return;
    AcquireSRWLockExclusive(&log_lock);
    if (log_bytes >= max_log_bytes && log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
        auto path = install_directory + L"\\blockthespot.log";
        MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
        log_bytes = 0;
    }
    if (log_file == INVALID_HANDLE_VALUE) open_log();
    // A reader may deny rotation. Keep the size bound even in that case.
    if (log_file != INVALID_HANDLE_VALUE && log_bytes < max_log_bytes) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        char line[2048];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u [%s] %s\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, label, message);
        DWORD written = 0;
        WriteFile(log_file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        log_bytes += written;
    }
    ReleaseSRWLockExclusive(&log_lock);
}
}

// Synchronous, bounded logging avoids a worker thread and DLL-detach waits.
// Level 0 still records errors. Request URLs and query strings are never logged.
void init_log(int level) noexcept { log_level = level; }
void log_debug(const char* message) noexcept { write_log(2, "DEBUG", message); }
void log_info(const char* message) noexcept { write_log(1, "INFO", message); }
void log_error(const char* message) noexcept { write_log(0, "ERROR", message); }

void set_status(std::string_view feature, std::string_view state, std::string_view detail) {
    std::string value(state);
    if (!detail.empty()) value += " - " + std::string(detail);
    AcquireSRWLockExclusive(&status_lock);
    auto& previous = statuses[std::string(feature)];
    if (previous == value) { ReleaseSRWLockExclusive(&status_lock); return; }
    // Preserve a failure even if a later read succeeds; do not hide partial health.
    if (previous.starts_with("failed") && state != "failed") {
        ReleaseSRWLockExclusive(&status_lock); return;
    }
    previous = value;
    std::string report = "BlockTheSpot " + std::string(PATCH_VERSION) + "\r\n";
    SYSTEMTIME now;
    GetLocalTime(&now);
    char timestamp[96];
    _snprintf_s(timestamp, sizeof(timestamp), _TRUNCATE, "Updated: %04u-%02u-%02u %02u:%02u:%02u (PID %lu)\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    report += timestamp;
    for (const auto& [name, status] : statuses) report += name + ": " + status + "\r\n";
    auto path = install_directory + L"\\blockthespot-status.txt";
    auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        bool ok = WriteFile(file, report.data(), static_cast<DWORD>(report.size()), &written, nullptr) && written == report.size();
        CloseHandle(file);
        if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            DeleteFileW(temporary.c_str());
    }
    ReleaseSRWLockExclusive(&status_lock);
    const std::string message = std::string(feature) + ": " + value;
    if (state == "failed") log_error(message.c_str());
    else log_info(message.c_str());
}

#include "glo/client_log.hpp"
#include "glo/log_rotation.hpp"
#include "glo/user_paths.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace glo {
namespace {

// Elevated worker and unelevated frontend can append concurrently. Protect
// file size check + rollover + append with one named mutex per logon session.
class CrossProcessLogLock {
public:
    CrossProcessLogLock() {
        handle_ = CreateMutexW(nullptr, FALSE, L"Local\\GLO_DebugLog_Write_v1");
        if (!handle_) return;
        const DWORD result = WaitForSingleObject(handle_, 1000);
        acquired_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    ~CrossProcessLogLock() {
        if (handle_) {
            if (acquired_) ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
    }
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }
private:
    HANDLE handle_{nullptr};
    bool acquired_{false};
};

const char* level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

std::string wall_clock() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::ostringstream o;
    o << std::setfill('0') << std::setw(4) << st.wYear << '-' << std::setw(2) << st.wMonth << '-'
      << std::setw(2) << st.wDay << ' ' << std::setw(2) << st.wHour << ':' << std::setw(2) << st.wMinute
      << ':' << std::setw(2) << st.wSecond << '.' << std::setw(3) << st.wMilliseconds;
    return o.str();
}

}  // namespace

bool ClientLog::enable_debug_file(std::string& error) {
    std::scoped_lock lock(mu_);
    error.clear();
    const auto path = debug_log_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        enabled_ = false;
        error = "could not create the GLO LocalAppData log directory";
        return false;
    }
    const auto wide = path.wstring();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    path_.assign(static_cast<std::size_t>(needed > 0 ? needed : 0), '\0');
    if (needed > 0) WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), path_.data(), needed, nullptr, nullptr);

    // Validate append access without keeping the file locked. The normal frontend
    // and elevated network worker may both write dbg_log.txt in the same session.
    std::ofstream probe(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!probe) {
        enabled_ = false;
        error = "could not open the GLO debug log in LocalAppData";
        return false;
    }
    probe.flush();
    enabled_ = true;
    last_.clear();
    suppressed_.clear();
    return true;
}

void ClientLog::disable() {
    std::scoped_lock lock(mu_);
    enabled_ = false;
    last_.clear();
    suppressed_.clear();
}

void ClientLog::debug(const std::string& code, const std::string& message) {
    write(LogLevel::Debug, code, message, {});
}
void ClientLog::info(const std::string& code, const std::string& message) {
    write(LogLevel::Info, code, message, {});
}
bool ClientLog::warn(const std::string& code, const std::string& message, std::chrono::milliseconds interval) {
    return write(LogLevel::Warn, code, message, interval);
}
void ClientLog::error(const std::string& code, const std::string& message, std::chrono::milliseconds interval) {
    write(LogLevel::Error, code, message, interval);
}

bool ClientLog::write(LogLevel level, const std::string& code, const std::string& message,
                      std::chrono::milliseconds min_interval) {
    std::scoped_lock lock(mu_);
    if (!enabled_) return false;

    const auto now = std::chrono::steady_clock::now();
    if (min_interval.count() > 0) {
        const auto it = last_.find(code);
        if (it != last_.end() && now - it->second < min_interval) {
            ++suppressed_[code];
            return false;
        }
        last_[code] = now;
    }

    std::ostringstream line;
    line << wall_clock() << ' ' << level_name(level) << " code=" << code << ' ' << message;
    if (const auto it = suppressed_.find(code); it != suppressed_.end() && it->second) {
        line << " suppressed=" << it->second;
        it->second = 0;
    }
    line << "\r\n";

    // The mutex protects both processes' size checks and rotations. Never
    // bypass the bound on contention/rotation failure.
    CrossProcessLogLock interprocess;
    if (!interprocess.acquired()) return false;
    if (!log_rotation::append(debug_log_path(), line.str())) return false;
    return true;
}

}  // namespace glo

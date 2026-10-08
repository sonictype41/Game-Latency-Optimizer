#pragma once

#include <chrono>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace glo {

enum class LogLevel { Debug, Info, Warn, Error };

class ClientLog {
public:
    ClientLog() = default;
    bool enable_debug_file(std::string& error);
    void disable();
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    void debug(const std::string& code, const std::string& message);
    void info(const std::string& code, const std::string& message);
    bool warn(const std::string& code, const std::string& message,
              std::chrono::milliseconds min_interval = std::chrono::milliseconds{0});
    void error(const std::string& code, const std::string& message,
               std::chrono::milliseconds min_interval = std::chrono::milliseconds{0});

private:
    bool write(LogLevel level, const std::string& code, const std::string& message,
               std::chrono::milliseconds min_interval);

    std::mutex mu_;
    std::string path_;
    // The route-control thread reads this while the worker toggles logging.
    std::atomic_bool enabled_{false};
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_;
    std::unordered_map<std::string, unsigned> suppressed_;
};

}  // namespace glo

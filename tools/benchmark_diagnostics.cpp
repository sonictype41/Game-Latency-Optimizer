// Standalone, reproducible CPU microbenchmark; NOT an end-to-end Windows RTT test.
// g++ -std=c++20 -O2 -pthread tools/benchmark_diagnostics.cpp -o /tmp/glo_diag_bench
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include "glo/log_rotation.hpp"

struct Counters {
    std::atomic<std::uint64_t> epoch{1}, total{0};
    std::atomic_bool enabled{false};
    void r1(std::uint64_t e) noexcept {
        if (e && epoch.load(std::memory_order_relaxed) == e) total.fetch_add(1, std::memory_order_relaxed);
    }
    void r1_optimized(std::uint64_t e) noexcept {
        if (enabled.load(std::memory_order_relaxed) && e && epoch.load(std::memory_order_relaxed) == e)
            total.fetch_add(1, std::memory_order_relaxed);
    }
};

// One counter observation per simulated datagram; repeat the same work in each mode.
template<typename Func>
double bench(Func f, int n) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
int main() {
    constexpr int N = 25000000;
    Counters c;
    double r1_ms = bench([&]{ c.r1(1); }, N);
    const auto r1_total = c.total.exchange(0);
    double disabled_ms = bench([&]{ c.r1_optimized(1); }, N);
    const auto disabled_total = c.total.exchange(0);
    c.enabled = true;
    double enabled_ms = bench([&]{ c.r1_optimized(1); }, N);
    const auto enabled_total = c.total.load();
    std::cout << std::fixed << std::setprecision(2)
              << "metric_observations=" << N << '\n'
              << "r1_always_count_ms=" << r1_ms << " writes=" << r1_total << '\n'
              << "r1_optimized_debug_off_ms=" << disabled_ms << " writes=" << disabled_total << '\n'
              << "r1_optimized_debug_on_verification_ms=" << enabled_ms << " writes=" << enabled_total << '\n';
    if (r1_total != N || disabled_total != 0 || enabled_total != N) return 1;

    const auto dir = std::filesystem::temp_directory_path() / "glo-diagnostic-log-benchmark";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    constexpr int LOG_LINES = 30000;
    const std::string line(256, 'x');
    {
        const auto f = dir / "unbounded_r1.log";
        const auto elapsed = bench([&] {
            std::ofstream out(f, std::ios::binary | std::ios::app);
            out.write(line.data(), static_cast<std::streamsize>(line.size()));
        }, LOG_LINES);
        std::cout << "r1_unbounded_append_30000_records_ms=" << elapsed
                  << " bytes=" << std::filesystem::file_size(f) << '\n';
    }
    {
        const auto f = dir / "dbg_log.txt";
        bool ok = true;
        const auto elapsed = bench([&] {
            if (!glo::log_rotation::append(f, line)) ok = false;
        }, LOG_LINES);
        std::cout << "r1_bounded_append_30000_records_ms=" << elapsed
                  << " current_bytes=" << std::filesystem::file_size(f)
                  << " backup_bytes=" << std::filesystem::file_size(f.wstring() + L".1") << '\n';
        if (!ok) return 1;
    }
    std::filesystem::remove_all(dir);
}

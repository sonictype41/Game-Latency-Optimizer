#include "glo/log_rotation.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>

static void require(bool ok) {
    if (!ok) throw std::runtime_error("bounded log rotation regression");
}

int main() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "glo-log-rotation-regression";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto path = dir / "dbg_log.txt";
    const std::string record(64 * 1024, 'x');
    std::uint64_t successes = 0;
    for (int i = 0; i < 290; ++i) {
        require(glo::log_rotation::append(path, record));
        ++successes;
        for (const auto& p : {path, fs::path(path.wstring() + L".1"), fs::path(path.wstring() + L".2")}) {
            if (fs::exists(p)) require(fs::file_size(p) <= glo::log_rotation::kMaxBytes);
        }
    }
    require(!glo::log_rotation::append(path, std::string(glo::log_rotation::kMaxBytes + 1, 'x')));
    require(fs::exists(path));
    require(fs::exists(fs::path(path.wstring() + L".1")));
    require(fs::exists(fs::path(path.wstring() + L".2")));
    // Upgrade from an old release with an oversized log: bound it and retain
    // recent records, not the oldest prefix.
    {
        std::ofstream old(path, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 120; ++i) old << std::string(64000, 'z') << "\n";
        old << "LAST_RECORD_TO_PRESERVE\n";
    }
    require(fs::file_size(path) > glo::log_rotation::kMaxBytes);
    require(glo::log_rotation::append(path, "NEW_RECORD\n"));
    require(fs::file_size(path) <= glo::log_rotation::kMaxBytes);
    {
        std::ifstream verify(path, std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(verify)), std::istreambuf_iterator<char>());
        require(data.find("LAST_RECORD_TO_PRESERVE") != std::string::npos);
        require(data.find("NEW_RECORD") != std::string::npos);
    }
    std::cout << "PASS log_rotation_test records=" << successes << " max_bytes=" << glo::log_rotation::kMaxBytes
              << " backups=" << glo::log_rotation::kBackups << '\n';
    fs::remove_all(dir);
}

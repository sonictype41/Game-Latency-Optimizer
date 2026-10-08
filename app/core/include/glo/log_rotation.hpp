#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <string_view>
#include <system_error>

namespace glo {
namespace log_rotation {

// A deliberately small, portable append/rotation policy. The caller MUST hold
// an inter-process lock across this entire function: frontend and elevated
// network worker write to the same file.
inline constexpr std::uintmax_t kMaxBytes = 5u * 1024u * 1024u;
inline constexpr unsigned kBackups = 2; // dbg_log.txt, .1, .2 = <= 15 MiB

inline bool append(const std::filesystem::path& path, std::string_view record) {
    if (record.size() > kMaxBytes) return false;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return false;
    std::uintmax_t bytes = exists ? std::filesystem::file_size(path, ec) : 0;
    if (ec) return false;
    if (bytes > kMaxBytes) {
        // An older unbounded release may have left a giant dbg_log.txt. Keep
        // only its most recent bounded tail, rather than rotating a giant
        // backup or silently deleting all useful recent diagnostics.
        const auto temp = std::filesystem::path(path.wstring() + L".compact.tmp");
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        const auto keep = kMaxBytes - record.size();
        in.seekg(static_cast<std::streamoff>(bytes - keep), std::ios::beg);
        if (!in) return false;
        std::string tail(static_cast<std::size_t>(keep), '\0');
        in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
        if (static_cast<std::size_t>(in.gcount()) != tail.size()) return false;
        in.close();
        const auto newline = tail.find('\n');
        const auto start = newline == std::string::npos ? tail.size() : newline + 1;
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(tail.data() + start, static_cast<std::streamsize>(tail.size() - start));
            out.flush();
            if (!out) return false;
        }
        std::filesystem::remove(path, ec);
        if (ec) return false;
        std::filesystem::rename(temp, path, ec);
        if (ec) return false;
        bytes = tail.size() - start;
    }
    if (bytes + record.size() > kMaxBytes) {
        // No unbounded append if a rotation fails (e.g. an editor holds the
        // original open without FILE_SHARE_DELETE). Dropping a debug record is
        // preferable to filling the user's disk.
        for (unsigned index = kBackups; index > 0; --index) {
            const auto from = index == 1 ? path : std::filesystem::path(path.wstring() + L"." + std::to_wstring(index - 1));
            const auto to = std::filesystem::path(path.wstring() + L"." + std::to_wstring(index));
            std::filesystem::remove(to, ec);
            if (ec) return false;
            if (std::filesystem::exists(from, ec)) {
                if (ec) return false;
                std::filesystem::rename(from, to, ec);
                if (ec) return false;
            } else if (ec) return false;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return false;
    out.write(record.data(), static_cast<std::streamsize>(record.size()));
    out.flush();
    return static_cast<bool>(out);
}

} // namespace log_rotation
} // namespace glo

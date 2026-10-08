#include "glo/game_log_hint_watcher.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace glo {
namespace {
namespace fs = std::filesystem;
std::wstring local_appdata() {
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
    if (n < 2 || n > 32768) return {};
    std::wstring result(n,L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA",result.data(),n);
    if (copied == 0 || copied >= n) return {};
    result.resize(copied);
    return result;
}
bool player_log(const fs::path& file) {
    if (file.extension() != L".log") return false;
    const auto name = file.filename().wstring();
    return name.find(L"_Player_") != std::wstring::npos;
}
fs::path newest_log(const fs::path& root) {
    std::error_code ec;
    fs::path candidate;
    fs::file_time_type latest{};
    for (fs::directory_iterator it(root,fs::directory_options::skip_permission_denied,ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !player_log(it->path())) continue;
        const auto modified = it->last_write_time(ec);
        if (!ec && (candidate.empty() || modified > latest)) {
            latest = modified;
            candidate = it->path();
        }
    }
    return candidate;
}
} // namespace
void GameLogHintWatcher::run(const std::atomic_bool& stop, const Callback& cb) const {
    if (game_ != GameId::Roblox || !cb) return;
    const auto appdata=local_appdata();
    if (appdata.empty()) return;
    const fs::path dir = fs::path(appdata)/L"Roblox"/L"logs";
    std::error_code ec;
    fs::path active=newest_log(dir);
    std::uintmax_t offset=active.empty() ? 0 : fs::file_size(active,ec);
    if (ec) offset=0;
    std::string pending;
    bool discard_oversize=false;
    const auto watcher_started = fs::file_time_type::clock::now();
    unsigned ticks=0;
    while (!stop.load(std::memory_order_acquire)) {
        // Re-scan at most every 200ms, and read changed bytes at 10ms cadence.
        if (++ticks % 20 == 0) {
            const auto selected=newest_log(dir);
            if (!selected.empty() && selected != active) {
                active=selected;
                // Never replay stale logs: a previously existing file may
                // become "newest" because of a filesystem timestamp change.
                std::error_code tm_error;
                const auto modified=fs::last_write_time(active,tm_error);
                const bool fresh = !tm_error && modified + std::chrono::seconds(2) >= watcher_started;
                offset = fresh ? 0 : fs::file_size(active,tm_error);
                if (tm_error) offset=0;
                pending.clear();
                discard_oversize=false;
            }
        }
        if (!active.empty()) {
            ec.clear();
            const auto size=fs::file_size(active,ec);
            if (!ec && size < offset) { offset=0; pending.clear(); discard_oversize=false; }
            if (!ec && size > offset) {
                std::ifstream in(active,std::ios::binary);
                if (in) {
                    in.seekg(static_cast<std::streamoff>(offset));
                    char buf[16384];
                    const auto want=static_cast<std::streamsize>(std::min<std::uintmax_t>(sizeof(buf),size-offset));
                    in.read(buf,want);
                    const auto n=in.gcount();
                    if (n > 0) {
                        offset+=static_cast<std::uintmax_t>(n);
                        for (std::streamsize i=0;i<n;++i) {
                            const char c=buf[i];
                            if (c == '\n') {
                                if (!discard_oversize) {
                                    const auto hint=parse_game_session_hint(game_,pending);
                                    if (hint.kind != GameHintKind::None) cb(hint);
                                }
                                pending.clear();
                                discard_oversize=false;
                            } else if (!discard_oversize && pending.size() < 8192) pending.push_back(c);
                            else discard_oversize=true;
                        }
                    }
                }
            }
        }
        Sleep(10);
    }
}
} // namespace glo

#include "glo/roblox_detector.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <utility>

namespace glo {
namespace {

bool iequals(const wchar_t* lhs, const wchar_t* rhs) {
    if (!lhs || !rhs) return false;
    while (*lhs || *rhs) {
        if (std::towlower(*lhs) != std::towlower(*rhs)) return false;
        if (*lhs) ++lhs;
        if (*rhs) ++rhs;
    }
    return true;
}

bool istarts_with(const wchar_t* value, const wchar_t* prefix) {
    if (!value || !prefix) return false;
    while (*prefix) {
        if (!*value || std::towlower(*value) != std::towlower(*prefix)) return false;
        ++value;
        ++prefix;
    }
    return true;
}

bool iends_with(const wchar_t* value, const wchar_t* suffix) {
    if (!value || !suffix) return false;
    const auto vlen = std::wcslen(value);
    const auto slen = std::wcslen(suffix);
    if (slen > vlen) return false;
    return iequals(value + (vlen - slen), suffix);
}

bool roblox_process_name(const wchar_t* name) {
    if (iequals(name, L"Windows10Universal.exe")) return true;
    if (!istarts_with(name, L"RobloxPlayer") || !iends_with(name, L".exe")) return false;
    if (iequals(name, L"RobloxPlayerLauncher.exe")) return false;
    return true;
}

 }  // namespace

bool query_process_image_path(std::uint32_t pid, std::wstring& image_path, std::string& error) {
    image_path.clear();
    error.clear();
    if (pid == 0) {
        error = "Roblox PID is zero";
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        std::ostringstream s;
        s << "OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) failed for Roblox PID " << pid
          << " (Win32=" << GetLastError() << ')';
        error = s.str();
        return false;
    }

    std::wstring buffer(32768, L'\0');
    DWORD chars = static_cast<DWORD>(buffer.size());
    const BOOL ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &chars);
    const DWORD query_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);

    if (!ok || chars == 0 || chars > buffer.size()) {
        std::ostringstream s;
        s << "QueryFullProcessImageNameW failed for Roblox PID " << pid
          << " (Win32=" << query_error << ')';
        error = s.str();
        return false;
    }

    buffer.resize(static_cast<std::size_t>(chars));
    image_path = std::move(buffer);
    return true;
}

std::vector<std::uint32_t> RobloxDetector::process_ids() const {
    std::vector<std::uint32_t> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (roblox_process_name(pe.szExeFile) && pe.th32ProcessID != 0) out.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

RobloxState RobloxDetector::poll() {
    RobloxState out;
    const auto pids = process_ids();
    out.process_running = !pids.empty();
    if (!out.process_running) {
        out.detail = "Roblox not running";
        return out;
    }
    out.primary_pid = pids.front();
    std::ostringstream s;
    s << "Roblox PID " << out.primary_pid;
    if (pids.size() > 1) s << "; instances=" << pids.size();
    out.detail = s.str();
    return out;
}

}  // namespace glo

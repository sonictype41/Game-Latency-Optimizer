#include "glo/user_paths.hpp"

#include <windows.h>

#include <array>

namespace glo {

std::filesystem::path user_data_dir() {
    std::array<wchar_t, 32768> value{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (n > 0 && n < value.size()) return std::filesystem::path(std::wstring(value.data(), n)) / L"GLO";
    return std::filesystem::current_path() / L"GLO";
}

std::filesystem::path settings_path() {
    return user_data_dir() / L"settings.json";
}

std::filesystem::path debug_log_path() {
    return user_data_dir() / L"logs" / L"dbg_log.txt";
}

}  // namespace glo

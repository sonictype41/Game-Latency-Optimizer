#pragma once

#include <filesystem>

namespace glo {

std::filesystem::path user_data_dir();
std::filesystem::path settings_path();
std::filesystem::path debug_log_path();

}  // namespace glo

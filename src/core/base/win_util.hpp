#pragma once

#if defined(_WIN32)

#include <filesystem>
#include <string>
#include <string_view>

namespace b_launcher_base::detail
{

std::wstring utf8_to_wide (std::string_view text);
std::string wide_to_utf8 (std::wstring_view text);
std::wstring native_path (const std::filesystem::path &path);
std::string last_error_message (unsigned long code);

}

#endif

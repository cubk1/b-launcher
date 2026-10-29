#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>
#include <string_view>

namespace exui {

    // The standard open and save dialogs. A filter lists names and patterns
    // separated by '|', for example L"Java|javaw.exe;java.exe|All files|*.*".
    namespace FileDialog {
        std::optional<std::filesystem::path> Open(HWND owner, std::wstring_view filter, std::wstring_view title);
        std::optional<std::filesystem::path> Save(HWND owner, std::wstring_view filter, std::wstring_view title,
                                                  std::wstring_view initialName = L"");
    } // namespace FileDialog

} // namespace exui

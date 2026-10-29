#include "ui/exui/widgets/FileDialog.h"
#include <commdlg.h>
#include <algorithm>
#include <string>
#include <vector>

namespace exui::FileDialog {

    namespace {
        std::optional<std::filesystem::path> Show(HWND owner, std::wstring_view pipeFilter, std::wstring_view title,
                                                  std::wstring_view initialName, bool save) {
            // '|' separators become the NULs OPENFILENAME expects, with a double NUL at the end.
            std::wstring filter(pipeFilter);
            std::replace(filter.begin(), filter.end(), L'|', L'\0');
            filter.append(2, L'\0');
            const std::wstring caption(title);
            std::vector<wchar_t> file(32768, L'\0');
            std::copy_n(initialName.data(), std::min(initialName.size(), file.size() - 1), file.data());
            OPENFILENAMEW ofn{sizeof(ofn)};
            ofn.hwndOwner = owner;
            ofn.lpstrFilter = filter.c_str();
            ofn.nFilterIndex = 1;
            ofn.lpstrFile = file.data();
            ofn.nMaxFile = DWORD(file.size());
            ofn.lpstrTitle = caption.c_str();
            ofn.Flags = OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return std::nullopt;
            return std::filesystem::path(file.data());
        }
    } // namespace

    std::optional<std::filesystem::path> Open(HWND owner, std::wstring_view filter, std::wstring_view title) {
        return Show(owner, filter, title, {}, false);
    }

    std::optional<std::filesystem::path> Save(HWND owner, std::wstring_view filter, std::wstring_view title,
                                              std::wstring_view initialName) {
        return Show(owner, filter, title, initialName, true);
    }

} // namespace exui::FileDialog

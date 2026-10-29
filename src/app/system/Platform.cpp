#include "app/system/Platform.h"

#include <core/base/strings.hpp>
#include <core/meta/game_layout.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <map>

namespace Platform {

    void* window = nullptr;

    namespace {
        const wchar_t* const STORE_KEY = L"Software\\BLAUNCHER";

        std::string utf8(const std::wstring& text) {
            if (text.empty()) {
                return {};
            }
            int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int) text.size(), nullptr, 0, nullptr, nullptr);
            std::string out(length, '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), (int) text.size(), out.data(), length, nullptr, nullptr);
            return out;
        }

        std::wstring wide(const std::string& text) {
            if (text.empty()) {
                return {};
            }
            int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int) text.size(), nullptr, 0);
            std::wstring out(length, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), (int) text.size(), out.data(), length);
            return out;
        }

        struct CaseLess {
            bool operator()(const std::wstring& a, const std::wstring& b) const {
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            }
        };

        std::vector<wchar_t> environmentBlock(const Environment& extra) {
            std::map<std::wstring, std::wstring, CaseLess> variables;
            std::vector<std::wstring> hidden;
            if (LPWCH strings = GetEnvironmentStringsW()) {
                for (LPWCH entry = strings; *entry != 0; entry += std::wcslen(entry) + 1) {
                    std::wstring text(entry);
                    size_t equals = text.find(L'=', 1);
                    if (text[0] == L'=' || equals == std::wstring::npos) {
                        hidden.push_back(text);
                        continue;
                    }
                    variables[text.substr(0, equals)] = text.substr(equals + 1);
                }
                FreeEnvironmentStringsW(strings);
            }
            for (const auto& [name, value] : extra) {
                if (!name.empty()) {
                    variables[wide(name)] = wide(value);
                }
            }
            std::vector<wchar_t> block;
            for (const std::wstring& text : hidden) {
                block.insert(block.end(), text.begin(), text.end());
                block.push_back(0);
            }
            for (const auto& [name, value] : variables) {
                block.insert(block.end(), name.begin(), name.end());
                block.push_back(L'=');
                block.insert(block.end(), value.begin(), value.end());
                block.push_back(0);
            }
            block.push_back(0);
            return block;
        }

        HANDLE spawn(const std::string& command, const Environment& environment, const std::filesystem::path& directory,
                     std::string& error) {
            const wchar_t* comspec = _wgetenv(L"ComSpec");
            std::wstring shell = comspec != nullptr && *comspec != 0 ? comspec : L"cmd.exe";
            std::wstring line = L"\"" + shell + L"\" /d /s /c \"" + wide(command) + L"\"";
            std::vector<wchar_t> buffer(line.begin(), line.end());
            buffer.push_back(0);
            std::vector<wchar_t> block = environmentBlock(environment);
            std::error_code missing;
            std::wstring folder = !directory.empty() && std::filesystem::is_directory(directory, missing)
                    ? directory.wstring()
                    : std::wstring();
            STARTUPINFOW startup = {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION info = {};
            if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
                                folder.empty() ? nullptr : folder.c_str(), &startup, &info)) {
                error = "无法运行命令（错误 " + std::to_string(GetLastError()) + "）";
                return nullptr;
            }
            CloseHandle(info.hThread);
            return info.hProcess;
        }
    }

    std::filesystem::path exeDirectory() {
        wchar_t buffer[MAX_PATH * 4];
        DWORD length = GetModuleFileNameW(nullptr, buffer, (DWORD) (sizeof(buffer) / sizeof(buffer[0])));
        if (length == 0) {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(std::wstring(buffer, length)).parent_path();
    }

    std::filesystem::path defaultMinecraft() {
        const wchar_t* appData = _wgetenv(L"APPDATA");
        if (appData == nullptr || *appData == 0) {
            return {};
        }
        return std::filesystem::path(appData) / L".minecraft";
    }

    std::filesystem::path cacheDirectory() {
        std::error_code error;
        for (const std::filesystem::path& root : {exeDirectory() / L".minecraft", defaultMinecraft()}) {
            if (!root.empty() && std::filesystem::is_directory(root, error)) {
                return b_launcher_base::meta::game_layout(root).cache();
            }
        }
        return {};
    }

    bool open(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return false;
        }
        HINSTANCE result =
                ShellExecuteW((HWND) window, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR) result > 32;
    }

    std::wstring widen(const std::string& text) {
        return wide(text);
    }

    namespace {
        const wchar_t* const MAIN_MARK = L"b-launcher.main";

        std::wstring folded(std::wstring text) {
            for (wchar_t& c : text) {
                c = (wchar_t) towlower(c);
            }
            return text;
        }

        std::wstring selfPath() {
            wchar_t buffer[32768];
            DWORD length = GetModuleFileNameW(nullptr, buffer, (DWORD) (sizeof(buffer) / sizeof(buffer[0])));
            return std::wstring(buffer, length);
        }

        std::wstring processPath(DWORD pid) {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process == nullptr) {
                return {};
            }
            wchar_t buffer[32768];
            DWORD length = (DWORD) (sizeof(buffer) / sizeof(buffer[0]));
            std::wstring out = QueryFullProcessImageNameW(process, 0, buffer, &length) ? std::wstring(buffer, length)
                                                                                       : std::wstring();
            CloseHandle(process);
            return out;
        }

        struct Running {
            std::wstring self;
            HWND found = nullptr;
        };

        BOOL CALLBACK findRunning(HWND hwnd, LPARAM data) {
            Running* search = (Running*) data;
            if (GetPropW(hwnd, MAIN_MARK) == nullptr) {
                return TRUE;
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId() || folded(processPath(pid)) != search->self) {
                return TRUE;
            }
            search->found = hwnd;
            return FALSE;
        }
    }

    std::string narrow(const std::wstring& text) {
        return utf8(text);
    }

    std::filesystem::path desktopDirectory() {
        std::filesystem::path folder;
        PWSTR desktop = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) && desktop != nullptr) {
            folder = desktop;
        }
        CoTaskMemFree(desktop);
        return folder;
    }

    bool insideLauncher(const std::filesystem::path& path) {
        std::wstring root = folded(exeDirectory().lexically_normal().wstring());
        std::wstring file = folded(path.lexically_normal().wstring());
        if (!root.empty() && root.back() != L'\\') {
            root.push_back(L'\\');
        }
        return !root.empty() && file.size() > root.size() && file.compare(0, root.size(), root) == 0;
    }

    std::vector<unsigned char> iconPixels(const std::filesystem::path& file, int size) {
        std::vector<unsigned char> rgba;
        HICON icon = (HICON) LoadImageW(nullptr, file.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE);
        if (icon == nullptr) {
            return rgba;
        }
        ICONINFO parts = {};
        if (GetIconInfo(icon, &parts) && parts.hbmColor != nullptr) {
            BITMAP shape = {};
            GetObjectW(parts.hbmColor, sizeof(shape), &shape);
            int w = shape.bmWidth;
            int h = shape.bmHeight;
            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            std::vector<unsigned char> color((size_t) w * h * 4);
            std::vector<unsigned char> mask((size_t) w * h * 4);
            HDC screen = GetDC(nullptr);
            bool read =
                    w > 0 && h > 0 && GetDIBits(screen, parts.hbmColor, 0, h, color.data(), &info, DIB_RGB_COLORS) == h;
            bool masked = read && parts.hbmMask != nullptr &&
                    GetDIBits(screen, parts.hbmMask, 0, h, mask.data(), &info, DIB_RGB_COLORS) == h;
            ReleaseDC(nullptr, screen);
            if (read && w == size && h == size) {
                bool alpha = false;
                for (size_t i = 3; i < color.size(); i += 4) {
                    alpha = alpha || color[i] != 0;
                }
                rgba.resize(color.size());
                for (size_t i = 0; i < color.size(); i += 4) {
                    rgba[i] = color[i + 2];
                    rgba[i + 1] = color[i + 1];
                    rgba[i + 2] = color[i];
                    rgba[i + 3] = alpha ? color[i + 3] : (masked && mask[i] != 0 ? 0 : 255);
                }
            }
        }
        if (parts.hbmColor != nullptr) {
            DeleteObject(parts.hbmColor);
        }
        if (parts.hbmMask != nullptr) {
            DeleteObject(parts.hbmMask);
        }
        DestroyIcon(icon);
        return rgba;
    }

    std::string createShortcut(const Shortcut& shortcut) {
        std::filesystem::path file = shortcut.file;
        if (folded(file.extension().wstring()) != L".lnk") {
            file += L".lnk";
        }
        std::wstring exe = selfPath();
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IShellLinkW* link = nullptr;
        HRESULT result = CoCreateInstance(__uuidof(ShellLink), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
        if (SUCCEEDED(result)) {
            link->SetPath(exe.c_str());
            link->SetArguments(wide(shortcut.arguments).c_str());
            link->SetDescription(wide(shortcut.description).c_str());
            if (shortcut.relative) {
                link->SetWorkingDirectory(L"");
                link->SetRelativePath(file.c_str(), 0);
            } else {
                link->SetWorkingDirectory(exeDirectory().wstring().c_str());
            }
            if (!shortcut.icon.empty()) {
                link->SetIconLocation(shortcut.icon.c_str(), 0);
            } else if (!shortcut.relative) {
                link->SetIconLocation(exe.c_str(), 0);
            }
            IPersistFile* store = nullptr;
            result = link->QueryInterface(IID_PPV_ARGS(&store));
            if (SUCCEEDED(result)) {
                result = store->Save(file.c_str(), TRUE);
                store->Release();
            }
            link->Release();
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        if (FAILED(result)) {
            char code[16];
            std::snprintf(code, sizeof(code), "%08lX", (unsigned long) result);
            return std::string("无法创建快捷方式（0x") + code + "）";
        }
        return {};
    }

    std::string launchArgument() {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (args == nullptr) {
            return {};
        }
        std::string key;
        for (int i = 1; i + 1 < count; i++) {
            if (std::wcscmp(args[i], L"--launch") == 0) {
                key = utf8(args[i + 1]);
            }
        }
        LocalFree(args);
        return key;
    }

    bool headlessArgument() {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (args == nullptr) {
            return false;
        }
        bool found = false;
        for (int i = 1; i < count; i++) {
            found = found || std::wcscmp(args[i], L"--headless") == 0;
        }
        LocalFree(args);
        return found;
    }

    bool forwardLaunch(const std::string& key, bool quiet) {
        Running search{folded(selfPath())};
        EnumWindows(findRunning, (LPARAM) &search);
        if (search.found == nullptr) {
            return false;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(search.found, &pid);
        AllowSetForegroundWindow(pid);
        std::wstring text = wide(key);
        COPYDATASTRUCT data = {(ULONG_PTR) (quiet ? LAUNCH_QUIET_MESSAGE : LAUNCH_MESSAGE),
                               (DWORD) ((text.size() + 1) * sizeof(wchar_t)), (PVOID) text.c_str()};
        DWORD_PTR answer = 0;
        return SendMessageTimeoutW(search.found, WM_COPYDATA, 0, (LPARAM) &data, SMTO_ABORTIFHUNG, 5000, &answer) !=
                0 &&
                answer == 1;
    }

    void claimLaunches() {
        if (window != nullptr) {
            SetPropW((HWND) window, MAIN_MARK, (HANDLE) 1);
        }
    }

    bool openUrl(const std::string& url) {
        HINSTANCE result = ShellExecuteW((HWND) window, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR) result > 32;
    }

    void reveal(const std::filesystem::path& path) {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.wstring().c_str());
        if (item != nullptr) {
            SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
            ILFree(item);
        } else {
            open(path.parent_path());
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
    }

    bool recycle(const std::filesystem::path& path) {
        std::wstring from = path.wstring();
        from.push_back(L'\0');
        SHFILEOPSTRUCTW operation = {};
        operation.hwnd = (HWND) window;
        operation.wFunc = FO_DELETE;
        operation.pFrom = from.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
    }

    std::optional<std::filesystem::path> pickFolder(const std::filesystem::path& initial) {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::optional<std::filesystem::path> picked;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(L"选择游戏目录");
            IShellItem* start = nullptr;
            std::error_code error;
            if (!initial.empty() && std::filesystem::exists(initial, error) &&
                SUCCEEDED(SHCreateItemFromParsingName(initial.wstring().c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                dialog->SetFolder(start);
                start->Release();
            }
            if (SUCCEEDED(dialog->Show((HWND) window))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item))) {
                    PWSTR name = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                        picked = std::filesystem::path(name);
                        CoTaskMemFree(name);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return picked;
    }

    namespace {
        std::optional<std::filesystem::path> pickFile(const std::filesystem::path& initial, const wchar_t* title,
                                                      const COMDLG_FILTERSPEC* filters, UINT filterCount) {
            HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            std::optional<std::filesystem::path> picked;
            IFileOpenDialog* dialog = nullptr;
            if (SUCCEEDED(
                        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                DWORD options = 0;
                dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
                dialog->SetTitle(title);
                dialog->SetFileTypes(filterCount, filters);
                std::error_code error;
                std::filesystem::path folder =
                        std::filesystem::is_directory(initial, error) ? initial : initial.parent_path();
                IShellItem* start = nullptr;
                if (!folder.empty() && std::filesystem::is_directory(folder, error) &&
                    SUCCEEDED(SHCreateItemFromParsingName(folder.wstring().c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                    dialog->SetFolder(start);
                    start->Release();
                }
                if (SUCCEEDED(dialog->Show((HWND) window))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item))) {
                        PWSTR name = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                            picked = std::filesystem::path(name);
                            CoTaskMemFree(name);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
            return picked;
        }
    }

    std::optional<std::filesystem::path> pickJava(const std::filesystem::path& initial) {
        const COMDLG_FILTERSPEC filters[] = {{L"Java (java.exe, javaw.exe)", L"java.exe;javaw.exe"},
                                             {L"程序 (*.exe)", L"*.exe"}};
        return pickFile(initial, L"选择 Java", filters, 2);
    }

    std::vector<std::filesystem::path> pickFiles(const std::filesystem::path& initial, const wchar_t* title,
                                                 const wchar_t* typeName, const wchar_t* pattern) {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::vector<std::filesystem::path> picked;
        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT);
            dialog->SetTitle(title);
            const COMDLG_FILTERSPEC filters[] = {{typeName, pattern}, {L"所有文件", L"*.*"}};
            dialog->SetFileTypes(2, filters);
            std::error_code error;
            IShellItem* start = nullptr;
            if (!initial.empty() && std::filesystem::is_directory(initial, error) &&
                SUCCEEDED(SHCreateItemFromParsingName(initial.wstring().c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                dialog->SetFolder(start);
                start->Release();
            }
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->Show((HWND) window)) && SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; i++) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR name = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                            picked.emplace_back(name);
                            CoTaskMemFree(name);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
            dialog->Release();
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return picked;
    }

    namespace {
        std::optional<std::filesystem::path> saveDialog(const std::filesystem::path& suggested, const wchar_t* title,
                                                        void* owner, const COMDLG_FILTERSPEC* filters, UINT filterCount,
                                                        const wchar_t* extension) {
            HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            std::optional<std::filesystem::path> picked;
            IFileSaveDialog* dialog = nullptr;
            if (SUCCEEDED(
                        CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                DWORD options = 0;
                dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_NODEREFERENCELINKS);
                dialog->SetTitle(title);
                dialog->SetFileTypes(filterCount, filters);
                dialog->SetDefaultExtension(extension);
                dialog->SetFileName(suggested.filename().wstring().c_str());
                std::error_code error;
                std::filesystem::path folder = suggested.parent_path();
                IShellItem* start = nullptr;
                if (!folder.empty() && std::filesystem::is_directory(folder, error) &&
                    SUCCEEDED(SHCreateItemFromParsingName(folder.wstring().c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                    dialog->SetFolder(start);
                    start->Release();
                }
                if (SUCCEEDED(dialog->Show((HWND) (owner != nullptr ? owner : window)))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item))) {
                        PWSTR name = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                            picked = std::filesystem::path(name);
                            CoTaskMemFree(name);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
            return picked;
        }
    }

    std::optional<std::filesystem::path> pickSaveFile(const std::filesystem::path& suggested, const wchar_t* title,
                                                      void* owner) {
        const COMDLG_FILTERSPEC filters[] = {{L"日志文件 (*.log)", L"*.log"}, {L"文本文件 (*.txt)", L"*.txt"}};
        return saveDialog(suggested, title, owner, filters, 2, L"log");
    }

    std::optional<std::filesystem::path> pickShortcut(const std::filesystem::path& suggested) {
        const COMDLG_FILTERSPEC filters[] = {{L"快捷方式 (*.lnk)", L"*.lnk"}};
        return saveDialog(suggested, L"快捷方式保存位置", nullptr, filters, 1, L"lnk");
    }

    std::optional<std::filesystem::path> pickIcon(const std::filesystem::path& initial) {
        const COMDLG_FILTERSPEC filters[] = {{L"图标或图片", L"*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.gif"},
                                             {L"所有文件", L"*.*"}};
        return pickFile(initial, L"选择快捷方式图标", filters, 2);
    }

    std::optional<std::filesystem::path> pickSavePack(const std::filesystem::path& suggested) {
        const COMDLG_FILTERSPEC filters[] = {{L"Modrinth 整合包 (*.mrpack)", L"*.mrpack"}};
        return saveDialog(suggested, L"导出整合包", nullptr, filters, 1, L"mrpack");
    }

    std::optional<std::filesystem::path> pickImage(const std::filesystem::path& initial) {
        const COMDLG_FILTERSPEC filters[] = {{L"图片", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"},
                                             {L"所有文件", L"*.*"}};
        return pickFile(initial, L"选择背景图片", filters, 2);
    }

    std::optional<std::string> loadValue(const std::string& name) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, STORE_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        std::wstring valueName = wide(name);
        DWORD type = 0;
        DWORD size = 0;
        std::optional<std::string> out;
        if (RegQueryValueExW(key, valueName.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS) {
            std::string bytes(size, '\0');
            if (RegQueryValueExW(key, valueName.c_str(), nullptr, &type, (BYTE*) bytes.data(), &size) ==
                ERROR_SUCCESS) {
                bytes.resize(size);
                if (type == REG_SZ || type == REG_EXPAND_SZ) {
                    std::wstring text((const wchar_t*) bytes.data(), bytes.size() / sizeof(wchar_t));
                    while (!text.empty() && text.back() == L'\0') {
                        text.pop_back();
                    }
                    out = utf8(text);
                } else {
                    out = std::move(bytes);
                }
            }
        }
        RegCloseKey(key);
        return out;
    }

    bool storeValue(const std::string& name, const std::string& data, bool text) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, STORE_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
            ERROR_SUCCESS) {
            return false;
        }
        std::wstring valueName = wide(name);
        LSTATUS status;
        if (text) {
            std::wstring value = wide(data);
            status = RegSetValueExW(key, valueName.c_str(), 0, REG_SZ, (const BYTE*) value.c_str(),
                                    (DWORD) ((value.size() + 1) * sizeof(wchar_t)));
        } else {
            status = RegSetValueExW(key, valueName.c_str(), 0, REG_BINARY, (const BYTE*) data.data(),
                                    (DWORD) data.size());
        }
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    std::string display(const std::filesystem::path& path) {
        std::string text = b_launcher_base::path_to_utf8(path);
        std::replace(text.begin(), text.end(), '/', '\\');
        return text;
    }

    uint32_t totalMemoryMb() {
        MEMORYSTATUSEX status = {};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status)) {
            return 8192;
        }
        return (uint32_t) (status.ullTotalPhys / (1024ull * 1024ull));
    }

    Memory memory() {
        MEMORYSTATUSEX status = {};
        status.dwLength = sizeof(status);
        Memory out;
        if (!GlobalMemoryStatusEx(&status)) {
            out.totalMb = 8192;
            out.availableMb = 4096;
            return out;
        }
        out.totalMb = (uint32_t) (status.ullTotalPhys / (1024ull * 1024ull));
        out.availableMb = (uint32_t) (status.ullAvailPhys / (1024ull * 1024ull));
        return out;
    }

    std::string runCommand(const std::string& command, const Environment& environment,
                           const std::filesystem::path& directory, const std::function<bool()>& cancelled) {
        std::string error;
        HANDLE process = spawn(command, environment, directory, error);
        if (process == nullptr) {
            return error;
        }
        while (WaitForSingleObject(process, 100) == WAIT_TIMEOUT) {
            if (cancelled && cancelled()) {
                TerminateProcess(process, 1);
                break;
            }
        }
        CloseHandle(process);
        return {};
    }

    std::string startCommand(const std::string& command, const Environment& environment,
                             const std::filesystem::path& directory) {
        std::string error;
        HANDLE process = spawn(command, environment, directory, error);
        if (process != nullptr) {
            CloseHandle(process);
        }
        return error;
    }

    bool setPriority(int pid, int level) {
        static const DWORD CLASSES[] = {IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
                                        ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS};
        if (pid <= 0 || level < 0 || level > 4) {
            return false;
        }
        HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, (DWORD) pid);
        if (process == nullptr) {
            return false;
        }
        bool done = SetPriorityClass(process, CLASSES[level]) != 0;
        CloseHandle(process);
        return done;
    }

    void minimizeWindow() {
        if (window != nullptr) {
            ShowWindow((HWND) window, SW_MINIMIZE);
        }
    }

    void hideWindow() {
        if (window != nullptr) {
            ShowWindow((HWND) window, SW_HIDE);
        }
    }

    void showWindow() {
        if (window == nullptr) {
            return;
        }
        HWND hwnd = (HWND) window;
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
    }

    void closeWindow() {
        if (window != nullptr) {
            PostMessageW((HWND) window, WM_CLOSE, 0, 0);
        }
    }
}

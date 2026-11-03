#include "app/system/DiskScan.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Catalog.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/strings.hpp>
#include <core/java/java_detect.hpp>

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <unordered_map>

namespace DiskScan {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr uint64_t INDEX_MASK = 0x0000FFFFFFFFFFFFull;
        constexpr DWORD BUFFER_BYTES = 1u << 20;
        constexpr int MAX_DEPTH = 512;
        const wchar_t* const HELPER_FLAG = L"--scan-disks";

        struct Node {
            uint64_t parent = 0;
            uint32_t offset = 0;
            uint16_t length = 0;
        };

        struct Volume {
            wchar_t letter = 0;
            HANDLE handle = INVALID_HANDLE_VALUE;
            uint64_t root = 0;
            uint64_t records = 1;
            USN_JOURNAL_DATA_V0 journal = {};
        };

        using Report = std::function<void(float fraction, wchar_t drive)>;

        struct Found {
            int volumes = 0;
            std::string error;
            std::vector<std::filesystem::path> javas;
            std::vector<std::filesystem::path> folders;
        };

        AsyncTasks tasks;
        Status current;
        std::atomic<int> stage{0};
        std::atomic<int> reading{0};
        std::atomic<float> fraction{0.0f};
        std::atomic<size_t> javaDone{0};
        std::atomic<size_t> javaTotal{0};
        std::vector<Drive> driveList;
        bool driveLoaded = false;
        bool driveLoading = false;
        std::chrono::steady_clock::time_point driveAt;
        std::set<wchar_t> deselected;

        bool ntfs(const wchar_t* root, UINT type, std::wstring* label) {
            wchar_t name[MAX_PATH + 1] = {};
            wchar_t system[MAX_PATH + 1] = {};
            if ((type != DRIVE_FIXED && type != DRIVE_REMOVABLE) ||
                !GetVolumeInformationW(root, name, (DWORD) std::size(name), nullptr, nullptr, nullptr, system,
                                       (DWORD) std::size(system)) ||
                std::wstring(system) != L"NTFS") {
                return false;
            }
            if (label != nullptr) {
                *label = name;
            }
            return true;
        }

        std::vector<Drive> listDrives() {
            std::vector<Drive> out;
            wchar_t roots[512] = {};
            DWORD length = GetLogicalDriveStringsW((DWORD) std::size(roots) - 1, roots);
            for (const wchar_t* root = roots; length > 0 && *root != L'\0'; root += std::wcslen(root) + 1) {
                UINT type = GetDriveTypeW(root);
                std::wstring label;
                if (!ntfs(root, type, &label)) {
                    continue;
                }
                Drive drive;
                drive.letter = (wchar_t) std::towupper(root[0]);
                drive.label = Platform::narrow(label);
                drive.removable = type == DRIVE_REMOVABLE;
                ULARGE_INTEGER total = {};
                if (GetDiskFreeSpaceExW(root, nullptr, &total, nullptr)) {
                    drive.size = total.QuadPart;
                }
                out.push_back(std::move(drive));
            }
            return out;
        }

        bool same(const wchar_t* name, size_t length, const wchar_t* expected) {
            size_t size = std::wcslen(expected);
            if (length != size) {
                return false;
            }
            for (size_t i = 0; i < size; i++) {
                if (std::towlower(name[i]) != std::towlower(expected[i])) {
                    return false;
                }
            }
            return true;
        }

        bool ignored(std::wstring path) {
            std::transform(path.begin(), path.end(), path.begin(),
                           [](wchar_t c) { return (wchar_t) std::towlower(c); });
            return path.find(L"\\$recycle.bin\\") != std::wstring::npos ||
                    path.find(L"\\system volume information\\") != std::wstring::npos;
        }

        bool rootOf(const std::wstring& drive, uint64_t& root) {
            HANDLE handle = CreateFileW(drive.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                return false;
            }
            BY_HANDLE_FILE_INFORMATION info = {};
            bool ok = GetFileInformationByHandle(handle, &info) != FALSE;
            CloseHandle(handle);
            root = ((uint64_t) info.nFileIndexHigh << 32 | info.nFileIndexLow) & INDEX_MASK;
            return ok;
        }

        bool journalOf(HANDLE volume, USN_JOURNAL_DATA_V0& journal) {
            DWORD bytes = 0;
            if (DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal, sizeof(journal), &bytes,
                                nullptr)) {
                return true;
            }
            if (GetLastError() != ERROR_JOURNAL_NOT_ACTIVE) {
                return false;
            }
            CREATE_USN_JOURNAL_DATA create = {32ull << 20, 4ull << 20};
            return DeviceIoControl(volume, FSCTL_CREATE_USN_JOURNAL, &create, sizeof(create), nullptr, 0, &bytes,
                                   nullptr) &&
                    DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal, sizeof(journal), &bytes,
                                    nullptr);
        }

        bool openVolume(wchar_t letter, Volume& out) {
            out.letter = letter;
            if (!rootOf(std::wstring(1, letter) + L":\\", out.root)) {
                return false;
            }
            std::wstring device = L"\\\\.\\" + std::wstring(1, letter) + L":";
            out.handle = CreateFileW(device.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
            if (out.handle == INVALID_HANDLE_VALUE) {
                return false;
            }
            if (!journalOf(out.handle, out.journal)) {
                CloseHandle(out.handle);
                out.handle = INVALID_HANDLE_VALUE;
                return false;
            }
            NTFS_VOLUME_DATA_BUFFER data = {};
            DWORD bytes = 0;
            if (DeviceIoControl(out.handle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &data, sizeof(data), &bytes,
                                nullptr) &&
                data.BytesPerFileRecordSegment > 0) {
                out.records = std::max<uint64_t>(
                        1, (uint64_t) data.MftValidDataLength.QuadPart / data.BytesPerFileRecordSegment);
            }
            return true;
        }

        void scanVolume(const Volume& target, Found& found, const std::function<void(uint64_t)>& advance) {
            std::wstring drive = std::wstring(1, target.letter) + L":\\";
            uint64_t root = target.root;
            HANDLE volume = target.handle;
            std::unordered_map<uint64_t, Node> dirs;
            dirs.reserve(1u << 18);
            std::wstring names;
            std::vector<uint64_t> javaParents;
            std::vector<uint64_t> games;
            std::vector<uint64_t> buffer(BUFFER_BYTES / sizeof(uint64_t));
            const unsigned char* data = (const unsigned char*) buffer.data();
            MFT_ENUM_DATA_V0 range = {0, 0, target.journal.NextUsn};
            DWORD bytes = 0;
            while (DeviceIoControl(volume, FSCTL_ENUM_USN_DATA, &range, sizeof(range), buffer.data(), BUFFER_BYTES,
                                   &bytes, nullptr) &&
                   bytes > sizeof(USN)) {
                size_t at = sizeof(USN);
                while (at + offsetof(USN_RECORD_V2, FileName) <= bytes) {
                    const auto* record = (const USN_RECORD_V2*) (data + at);
                    if (record->RecordLength == 0 || at + record->RecordLength > bytes) {
                        break;
                    }
                    if (record->MajorVersion == 2 &&
                        (size_t) record->FileNameOffset + record->FileNameLength <= record->RecordLength) {
                        const wchar_t* name = (const wchar_t*) (data + at + record->FileNameOffset);
                        size_t length = record->FileNameLength / sizeof(wchar_t);
                        uint64_t self = record->FileReferenceNumber & INDEX_MASK;
                        uint64_t parent = record->ParentFileReferenceNumber & INDEX_MASK;
                        if ((record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                            dirs[self] = Node{parent, (uint32_t) names.size(), (uint16_t) length};
                            names.append(name, length);
                            if (same(name, length, L".minecraft")) {
                                games.push_back(self);
                            }
                        } else if (same(name, length, L"java.exe")) {
                            javaParents.push_back(parent);
                        }
                    }
                    at += record->RecordLength;
                }
                range.StartFileReferenceNumber = *(const DWORDLONG*) data;
                advance(range.StartFileReferenceNumber & INDEX_MASK);
            }

            auto pathOf = [&](uint64_t ref) -> std::wstring {
                std::vector<const Node*> chain;
                for (int depth = 0; ref != root; depth++) {
                    auto found = dirs.find(ref);
                    if (depth > MAX_DEPTH || found == dirs.end() || found->second.parent == ref) {
                        return std::wstring();
                    }
                    chain.push_back(&found->second);
                    ref = found->second.parent;
                }
                std::wstring path = drive;
                for (auto part = chain.rbegin(); part != chain.rend(); ++part) {
                    if (path.back() != L'\\') {
                        path += L'\\';
                    }
                    path.append(names, (*part)->offset, (*part)->length);
                }
                return path;
            };
            for (uint64_t parent : javaParents) {
                auto bin = dirs.find(parent);
                if (bin == dirs.end() || !same(names.data() + bin->second.offset, bin->second.length, L"bin")) {
                    continue;
                }
                std::wstring home = pathOf(bin->second.parent);
                if (!home.empty() && !ignored(home + L"\\")) {
                    found.javas.emplace_back(home);
                }
            }
            for (uint64_t game : games) {
                std::wstring path = pathOf(game);
                if (!path.empty() && !ignored(path + L"\\")) {
                    found.folders.emplace_back(path);
                }
            }
        }

        Found scanAll(const std::wstring& letters, const Report& report) {
            Found found;
            std::vector<Volume> volumes;
            wchar_t drives[512] = {};
            DWORD length = GetLogicalDriveStringsW((DWORD) std::size(drives) - 1, drives);
            for (const wchar_t* drive = drives; length > 0 && *drive != L'\0'; drive += std::wcslen(drive) + 1) {
                if (letters.find((wchar_t) std::towupper(drive[0])) == std::wstring::npos ||
                    !ntfs(drive, GetDriveTypeW(drive), nullptr)) {
                    continue;
                }
                Volume volume;
                if (openVolume(drive[0], volume)) {
                    volumes.push_back(volume);
                }
            }
            uint64_t total = 0;
            for (const Volume& volume : volumes) {
                total += volume.records;
            }
            total = std::max<uint64_t>(1, total);
            uint64_t before = 0;
            for (const Volume& volume : volumes) {
                report((float) ((double) before / (double) total), volume.letter);
                scanVolume(volume, found, [&](uint64_t next) {
                    uint64_t done = before + std::min(next, volume.records);
                    report((float) ((double) done / (double) total), volume.letter);
                });
                CloseHandle(volume.handle);
                before += volume.records;
                found.volumes++;
            }
            report(1.0f, 0);
            if (found.volumes == 0) {
                found.error = "没有可以读取的 NTFS 磁盘";
            }
            return found;
        }

        std::filesystem::path progressFile(const std::filesystem::path& output) {
            std::filesystem::path file = output;
            file += L".progress";
            return file;
        }

        void readProgress(const std::filesystem::path& file) {
            std::ifstream in(file);
            float value = 0.0f;
            int letter = 0;
            if (in >> value >> letter) {
                fraction = std::max(fraction.load(), std::clamp(value, 0.0f, 1.0f));
                reading = letter;
            }
        }

        bool elevated() {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                return false;
            }
            TOKEN_ELEVATION elevation = {};
            DWORD size = 0;
            bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated != 0;
            CloseHandle(token);
            return ok;
        }

        std::string runElevated(const std::filesystem::path& output, const std::wstring& letters,
                                blb::cancellation_token token) {
            wchar_t exe[MAX_PATH * 4] = {};
            DWORD length = GetModuleFileNameW(nullptr, exe, (DWORD) std::size(exe));
            if (length == 0 || length >= std::size(exe)) {
                return "找不到启动器程序";
            }
            std::wstring parameters = std::wstring(HELPER_FLAG) + L" \"" + output.wstring() + L"\" " + letters;
            std::filesystem::path progress = progressFile(output);
            HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            SHELLEXECUTEINFOW info = {};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.hwnd = (HWND) Platform::window;
            info.lpVerb = L"runas";
            info.lpFile = exe;
            info.lpParameters = parameters.c_str();
            info.nShow = SW_HIDE;
            bool launched = ShellExecuteExW(&info) != FALSE;
            DWORD failure = launched ? 0 : GetLastError();
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
            if (!launched || info.hProcess == nullptr) {
                return failure == ERROR_CANCELLED ? "已取消管理员授权" : "无法以管理员身份运行搜索";
            }
            stage = 1;
            while (WaitForSingleObject(info.hProcess, 100) == WAIT_TIMEOUT) {
                if (token.is_cancelled()) {
                    CloseHandle(info.hProcess);
                    return "已取消";
                }
                readProgress(progress);
            }
            DWORD code = 1;
            GetExitCodeProcess(info.hProcess, &code);
            CloseHandle(info.hProcess);
            return code == 0 ? std::string() : "搜索没有完成";
        }

        Found readOutput(const std::filesystem::path& file) {
            Found found;
            std::ifstream in(file, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
            if (doc.is_discarded() || !doc.is_object()) {
                found.error = "读取搜索结果失败";
                return found;
            }
            found.volumes = doc.value("volumes", 0);
            found.error = doc.value("error", std::string());
            for (const char* key : {"java", "minecraft"}) {
                auto list = doc.find(key);
                if (list == doc.end() || !list->is_array()) {
                    continue;
                }
                for (const nlohmann::json& item : *list) {
                    if (item.is_string()) {
                        (std::string(key) == "java" ? found.javas : found.folders)
                                .push_back(blb::path_from_utf8(item.get<std::string>()));
                    }
                }
            }
            return found;
        }

        void finish(const Found& found, const std::vector<blb::java::java_installation>& javas) {
            if (!found.error.empty()) {
                current.state = State::Failed;
                current.error = found.error;
                Notices::add(Notices::Kind::Failed, "全盘搜索：" + found.error, 4000);
                return;
            }
            current.state = State::Done;
            current.volumes = found.volumes;
            if (current.kind == Kind::Java) {
                current.javas = Catalog::addFound(javas);
                Notices::add(Notices::Kind::Done,
                             current.javas > 0 ? "全盘搜索完成，新增 " + std::to_string(current.javas) + " 个 Java"
                                               : std::string("全盘搜索完成，没有发现新的 Java"),
                             4000);
                return;
            }
            for (const std::filesystem::path& folder : found.folders) {
                std::error_code error;
                if (std::filesystem::is_directory(folder / "versions", error)) {
                    current.folders.push_back(folder);
                }
            }
            Notices::add(Notices::Kind::Done,
                         "全盘搜索完成，找到 " + std::to_string(current.folders.size()) + " 个游戏目录", 4000);
        }
    }

    void start(Kind kind) {
        if (current.state == State::Running) {
            return;
        }
        current = Status();
        current.state = State::Running;
        current.kind = kind;
        current.waiting = true;
        stage = 0;
        reading = 0;
        fraction = 0.0f;
        javaDone = 0;
        javaTotal = 0;
        std::filesystem::path output = std::filesystem::temp_directory_path() /
                ("b-launcher-disks-" + std::to_string(GetCurrentProcessId()) + ".json");
        std::vector<std::filesystem::path> excluded = Catalog::javaScanOptions().excluded;
        std::wstring letters;
        for (const Drive& drive : driveList) {
            if (driveSelected(drive.letter)) {
                letters += drive.letter;
            }
        }
        tasks.run(
                [output, excluded, kind, letters](blb::cancellation_token token) -> Continuation {
                    Found found;
                    if (elevated()) {
                        stage = 1;
                        found = scanAll(letters, [](float value, wchar_t drive) {
                            fraction = value;
                            reading = (int) drive;
                        });
                    } else {
                        std::error_code ignore;
                        std::filesystem::remove(output, ignore);
                        std::filesystem::remove(progressFile(output), ignore);
                        found.error = runElevated(output, letters, token);
                        if (found.error.empty()) {
                            found = readOutput(output);
                        }
                        std::filesystem::remove(output, ignore);
                        std::filesystem::remove(progressFile(output), ignore);
                    }
                    if (token.is_cancelled()) {
                        return nullptr;
                    }
                    std::vector<blb::java::java_installation> javas;
                    if (kind == Kind::Java && found.error.empty() && !found.javas.empty()) {
                        blb::java::scan_options options;
                        options.system_locations = false;
                        options.extra_homes = found.javas;
                        options.excluded = excluded;
                        options.progress = [](size_t done, size_t total) {
                            javaDone = done;
                            javaTotal = total;
                        };
                        stage = 2;
                        javas = blb::java::scan_java(options, token);
                    }
                    return [found = std::move(found), javas = std::move(javas)] { finish(found, javas); };
                },
                [](const std::string& error) -> Continuation {
                    return [error] {
                        current.state = State::Failed;
                        current.error = error;
                    };
                });
    }

    const Status& status() {
        return current;
    }

    const std::vector<Drive>& drives() {
        auto now = std::chrono::steady_clock::now();
        if (!driveLoading && (!driveLoaded || now - driveAt >= std::chrono::seconds(5))) {
            driveLoading = true;
            tasks.run(
                    [](blb::cancellation_token) -> Continuation {
                        std::vector<Drive> found = listDrives();
                        return [found = std::move(found)] {
                            driveList = found;
                            driveLoaded = true;
                            driveLoading = false;
                            driveAt = std::chrono::steady_clock::now();
                        };
                    },
                    [](const std::string&) -> Continuation {
                        return [] {
                            driveLoaded = true;
                            driveLoading = false;
                            driveAt = std::chrono::steady_clock::now();
                        };
                    });
        }
        return driveList;
    }

    bool drivesReady() {
        return driveLoaded;
    }

    bool driveSelected(wchar_t letter) {
        return deselected.count(letter) == 0;
    }

    void selectDrive(wchar_t letter, bool on) {
        if (on) {
            deselected.erase(letter);
        } else {
            deselected.insert(letter);
        }
    }

    bool anySelected() {
        return std::any_of(driveList.begin(), driveList.end(),
                           [](const Drive& drive) { return driveSelected(drive.letter); });
    }

    std::optional<Helper> helperArgument() {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (args == nullptr) {
            return std::nullopt;
        }
        std::optional<Helper> helper;
        for (int i = 1; i + 1 < count; i++) {
            if (std::wcscmp(args[i], HELPER_FLAG) == 0) {
                helper = Helper{args[i + 1], i + 2 < count ? args[i + 2] : L""};
            }
        }
        LocalFree(args);
        return helper;
    }

    int runHelper(const Helper& helper) {
        const std::wstring& output = helper.output;
        std::filesystem::path progress = progressFile(output);
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        Found found = scanAll(helper.drives, [&](float value, wchar_t drive) {
            auto now = std::chrono::steady_clock::now();
            if (value < 1.0f && now - last < std::chrono::milliseconds(100)) {
                return;
            }
            last = now;
            std::ofstream out(progress, std::ios::trunc);
            out << value << ' ' << (int) drive;
        });
        nlohmann::json doc;
        doc["volumes"] = found.volumes;
        doc["error"] = found.error;
        nlohmann::json javas = nlohmann::json::array();
        for (const std::filesystem::path& path : found.javas) {
            javas.push_back(blb::path_to_utf8(path));
        }
        nlohmann::json folders = nlohmann::json::array();
        for (const std::filesystem::path& path : found.folders) {
            folders.push_back(blb::path_to_utf8(path));
        }
        doc["java"] = std::move(javas);
        doc["minecraft"] = std::move(folders);
        std::ofstream out(std::filesystem::path(output), std::ios::binary | std::ios::trunc);
        out << doc.dump();
        return out.good() ? 0 : 1;
    }

    void poll() {
        tasks.poll();
        if (current.state != State::Running) {
            return;
        }
        int now = stage.load();
        current.waiting = now == 0;
        if (now == 0) {
            current.step = "等待管理员授权";
            current.progress = 0.0f;
        } else if (now == 1) {
            int letter = reading.load();
            current.step =
                    letter > 0 ? "正在读取 " + std::string(1, (char) letter) + ": 盘" : std::string("正在读取文件表");
            current.progress = (current.kind == Kind::Java ? 0.9f : 1.0f) * fraction.load();
        } else {
            size_t total = javaTotal.load();
            current.step = "正在识别 Java";
            current.progress = 0.9f + 0.1f * (total > 0 ? (float) javaDone.load() / (float) total : 0.0f);
        }
    }

    void shutdown() {
        tasks.stop();
    }
}

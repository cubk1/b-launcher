#include "app/system/Customization.h"
#include "app/system/Config.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

namespace Customization {

    namespace blb = b_launcher_base;

    namespace {
        constexpr char MAGIC[8] = {'B', 'L', 'B', 'P', 'A', 'C', 'K', '1'};
        constexpr size_t TRAILER = 8 + 8 + 8 + 32;

        struct Setting {
            nlohmann::json value;
            bool locked = false;
        };

        struct State {
            bool active = false;
            std::filesystem::path origin;
            std::string bytes;
            std::unique_ptr<blb::zip_reader> zip;
            std::vector<std::string> names;
            std::vector<std::string> problems;
            std::string by;
            std::string version;
            std::string theme;
            bool themeLocked = false;
            std::string windowTitle;
            std::string brandTitle;
            std::string logo;
            std::string icon;
            std::string homepageType;
            std::string homepageValue;
            bool homepageLocked = false;
            std::string announcement;
            std::vector<Link> links;
            std::vector<Server> servers;
            std::string autoJoin;
            std::vector<Login> logins;
            std::string modpack;
            std::string modpackInstance;
            bool singleInstance = false;
            std::vector<std::string> hidden;
            bool featuresLocked = false;
            std::map<std::string, Setting> settings;
            bool updatesDisabled = false;
        };

        State state;

        std::filesystem::path selfPath() {
#if defined(_WIN32)
            wchar_t buffer[MAX_PATH * 4];
            DWORD length = GetModuleFileNameW(nullptr, buffer, (DWORD) (sizeof(buffer) / sizeof(buffer[0])));
            return length == 0 ? std::filesystem::path() : std::filesystem::path(std::wstring(buffer, length));
#elif defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
                return {};
            }
            return std::filesystem::path(buffer.c_str());
#else
            std::error_code error;
            return std::filesystem::read_symlink("/proc/self/exe", error);
#endif
        }

        std::uint64_t readU64(const unsigned char* data) {
            std::uint64_t value = 0;
            for (int i = 7; i >= 0; i--) {
                value = (value << 8) | data[i];
            }
            return value;
        }

        void writeU64(std::string& out, std::uint64_t value) {
            for (int i = 0; i < 8; i++) {
                out.push_back((char) ((value >> (i * 8)) & 0xFF));
            }
        }

        bool trailerOf(const std::filesystem::path& file, std::uint64_t& offset, std::uint64_t& length,
                       std::string* digest) {
            std::ifstream in(file, std::ios::binary | std::ios::ate);
            if (!in) {
                return false;
            }
            std::uint64_t size = (std::uint64_t) in.tellg();
            if (size < TRAILER) {
                return false;
            }
            unsigned char tail[TRAILER];
            in.seekg((std::streamoff)(size - TRAILER));
            in.read((char*) tail, TRAILER);
            if (!in || std::memcmp(tail, MAGIC, 8) != 0) {
                return false;
            }
            offset = readU64(tail + 8);
            length = readU64(tail + 16);
            if (offset + length + TRAILER != size || length == 0) {
                return false;
            }
            if (digest != nullptr) {
                digest->assign((const char*) tail + 24, 32);
            }
            return true;
        }

        std::optional<std::string> appended(const std::filesystem::path& exe) {
            std::uint64_t offset = 0;
            std::uint64_t length = 0;
            std::string digest;
            if (!trailerOf(exe, offset, length, &digest) || length > ((std::uint64_t) 256 << 20)) {
                return std::nullopt;
            }
            std::ifstream in(exe, std::ios::binary);
            in.seekg((std::streamoff) offset);
            std::string bytes((size_t) length, '\0');
            in.read(bytes.data(), (std::streamsize) length);
            if (!in) {
                return std::nullopt;
            }
            blb::sha256_digest sum = blb::sha256::digest(bytes.data(), bytes.size());
            if (std::memcmp(sum.data(), digest.data(), 32) != 0) {
                state.problems.push_back("定制包校验失败");
                return std::nullopt;
            }
            return bytes;
        }

        std::string text(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        bool flag(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_boolean() && it->get<bool>();
        }

        void parse(const nlohmann::json& doc) {
            state.by = text(doc, "by");
            state.version = text(doc, "version");
            auto theme = doc.find("theme");
            if (theme != doc.end() && theme->is_string()) {
                state.theme = theme->get<std::string>();
            } else if (theme != doc.end() && theme->is_object()) {
                state.theme = text(*theme, "id");
                state.themeLocked = flag(*theme, "lock");
            }
            auto branding = doc.find("branding");
            if (branding != doc.end() && branding->is_object()) {
                state.windowTitle = text(*branding, "windowTitle");
                state.brandTitle = text(*branding, "brandTitle");
                state.logo = text(*branding, "logo");
                state.icon = text(*branding, "icon");
            }
            auto home = doc.find("homepage");
            if (home != doc.end() && home->is_object()) {
                state.homepageType = text(*home, "type");
                state.homepageValue = text(*home, "value");
                state.homepageLocked = flag(*home, "lock");
                state.announcement = text(*home, "announcement");
                auto links = home->find("links");
                if (links != home->end() && links->is_array()) {
                    for (const nlohmann::json& link : *links) {
                        if (link.is_object() && !text(link, "url").empty()) {
                            state.links.push_back({text(link, "label"), text(link, "url")});
                        }
                    }
                }
            }
            auto servers = doc.find("servers");
            if (servers != doc.end() && servers->is_array()) {
                for (const nlohmann::json& server : *servers) {
                    if (server.is_object() && !text(server, "address").empty()) {
                        state.servers.push_back({text(server, "name"), text(server, "address"), text(server, "icon")});
                    }
                }
            }
            state.autoJoin = text(doc, "autoJoin");
            auto logins = doc.find("logins");
            if (logins != doc.end() && logins->is_array()) {
                for (const nlohmann::json& login : *logins) {
                    if (login.is_string()) {
                        state.logins.push_back({login.get<std::string>(), std::string()});
                    } else if (login.is_object() && !text(login, "type").empty()) {
                        state.logins.push_back({text(login, "type"), text(login, "server")});
                    }
                }
            }
            auto modpack = doc.find("modpack");
            if (modpack != doc.end() && modpack->is_object()) {
                state.modpack = text(*modpack, "source");
                state.modpackInstance = text(*modpack, "instance");
                state.singleInstance = flag(*modpack, "single");
            }
            auto features = doc.find("features");
            if (features != doc.end() && features->is_object()) {
                auto hidden = features->find("hidden");
                if (hidden != features->end() && hidden->is_array()) {
                    for (const nlohmann::json& item : *hidden) {
                        if (item.is_string()) {
                            state.hidden.push_back(item.get<std::string>());
                        }
                    }
                }
                state.featuresLocked = flag(*features, "lock");
            }
            auto settings = doc.find("settings");
            if (settings != doc.end() && settings->is_object()) {
                for (auto entry = settings->begin(); entry != settings->end(); ++entry) {
                    Setting setting;
                    if (entry.value().is_object() && entry.value().contains("value")) {
                        setting.value = entry.value()["value"];
                        setting.locked = flag(entry.value(), "lock");
                    } else {
                        setting.value = entry.value();
                    }
                    state.settings[entry.key()] = std::move(setting);
                }
            }
            auto updates = doc.find("updates");
            state.updatesDisabled = updates != doc.end() && updates->is_boolean() && !updates->get<bool>();
        }

        bool open(std::string bytes, const std::filesystem::path& origin) {
            auto zip = blb::zip_reader::open_memory(std::move(bytes));
            if (!zip) {
                state.problems.push_back("无法打开定制包");
                return false;
            }
            auto config = zip->read("customize.json");
            if (!config) {
                state.problems.push_back("定制包里没有 customize.json");
                return false;
            }
            nlohmann::json doc = nlohmann::json::parse(*config, nullptr, false, true);
            if (!doc.is_object()) {
                state.problems.push_back("customize.json 不是有效的 JSON");
                return false;
            }
            auto entries = zip->entries();
            if (entries) {
                for (const blb::zip_entry& entry : *entries) {
                    if (!entry.directory && blb::fs::safe_relative(entry.name)) {
                        state.names.push_back(entry.name);
                    }
                }
            }
            state.zip = std::make_unique<blb::zip_reader>(std::move(*zip));
            state.origin = origin;
            state.active = true;
            parse(doc);
            return true;
        }

        std::filesystem::path besideFile(const std::filesystem::path& exe) {
#if defined(__APPLE__)
            std::filesystem::path folder = exe.parent_path();
            if (folder.filename() == "MacOS") {
                return folder.parent_path() / "Resources" / "customize.blbpack";
            }
#endif
            return exe.parent_path() / "customize.blbpack";
        }

        bool collect(const std::filesystem::path& source, std::string& bytes, std::string& error) {
            std::error_code ec;
            if (std::filesystem::is_directory(source, ec)) {
                std::filesystem::path temp =
                        std::filesystem::temp_directory_path(ec) / ("blbpack-" + blb::random_bytes_hex(6) + ".zip");
                auto writer = blb::zip_writer::create(temp);
                if (!writer) {
                    error = "无法创建临时文件";
                    return false;
                }
                for (std::filesystem::recursive_directory_iterator it(source, ec), end; !ec && it != end;
                     it.increment(ec)) {
                    if (!it->is_regular_file(ec)) {
                        continue;
                    }
                    std::string name = blb::path_to_utf8(std::filesystem::relative(it->path(), source, ec));
                    std::replace(name.begin(), name.end(), '\\', '/');
                    if (!writer->add_file(name, it->path())) {
                        error = "无法打包 " + name;
                        return false;
                    }
                }
                if (!writer->close()) {
                    error = "无法写入定制包";
                    return false;
                }
                auto data = blb::fs::read_file(temp);
                std::filesystem::remove(temp, ec);
                if (!data) {
                    error = "无法读取定制包";
                    return false;
                }
                bytes = std::move(*data);
            } else {
                auto data = blb::fs::read_file(source);
                if (!data) {
                    error = "无法读取 " + blb::path_to_utf8(source);
                    return false;
                }
                bytes = std::move(*data);
            }
            auto zip = blb::zip_reader::open_memory(bytes);
            if (!zip || !zip->contains("customize.json")) {
                error = "定制包里没有 customize.json";
                return false;
            }
            return true;
        }

#if defined(_WIN32)
#pragma pack(push, 2)
        struct IconEntry {
            BYTE width;
            BYTE height;
            BYTE colors;
            BYTE reserved;
            WORD planes;
            WORD bits;
            DWORD size;
            DWORD offset;
        };
        struct GroupEntry {
            BYTE width;
            BYTE height;
            BYTE colors;
            BYTE reserved;
            WORD planes;
            WORD bits;
            DWORD size;
            WORD id;
        };
#pragma pack(pop)

        struct GroupName {
            std::wstring text;
            WORD id = 0;
            WORD language = 0;
            bool found = false;
        };

        bool replaceIcon(const std::filesystem::path& exe, const std::string& ico, std::string& error) {
            if (ico.size() < 6 + sizeof(IconEntry)) {
                error = "图标文件无效";
                return false;
            }
            WORD count = *(const WORD*) (ico.data() + 4);
            if (count == 0 || ico.size() < 6 + count * sizeof(IconEntry)) {
                error = "图标文件无效";
                return false;
            }
            GroupName group;
            HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
            if (module != nullptr) {
                EnumResourceNamesW(
                        module, MAKEINTRESOURCEW(14),
                        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
                            auto* out = (GroupName*) param;
                            if (IS_INTRESOURCE(name)) {
                                out->id = (WORD) (ULONG_PTR) name;
                            } else {
                                out->text = name;
                            }
                            out->found = true;
                            return FALSE;
                        },
                        (LONG_PTR) &group);
                if (group.found) {
                    LPCWSTR name = group.text.empty() ? MAKEINTRESOURCEW(group.id) : group.text.c_str();
                    EnumResourceLanguagesW(
                            module, MAKEINTRESOURCEW(14), name,
                            [](HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR param) -> BOOL {
                                ((GroupName*) param)->language = language;
                                return FALSE;
                            },
                            (LONG_PTR) &group);
                }
                FreeLibrary(module);
            }
            if (!group.found) {
                group.id = 1;
                group.language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
            }
            HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
            if (update == nullptr) {
                error = "无法修改程序图标";
                return false;
            }
            const IconEntry* entries = (const IconEntry*) (ico.data() + 6);
            std::string directory(6 + count * sizeof(GroupEntry), '\0');
            std::memcpy(directory.data(), ico.data(), 6);
            for (WORD i = 0; i < count; i++) {
                const IconEntry& entry = entries[i];
                if ((size_t) entry.offset + entry.size > ico.size()) {
                    EndUpdateResourceW(update, TRUE);
                    error = "图标文件无效";
                    return false;
                }
                WORD id = (WORD) (200 + i);
                UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), group.language,
                                (LPVOID) (ico.data() + entry.offset), entry.size);
                GroupEntry out = {entry.width,  entry.height, entry.colors, entry.reserved,
                                  entry.planes, entry.bits,   entry.size,   id};
                std::memcpy(directory.data() + 6 + i * sizeof(GroupEntry), &out, sizeof(out));
            }
            LPCWSTR name = group.text.empty() ? MAKEINTRESOURCEW(group.id) : group.text.c_str();
            UpdateResourceW(update, MAKEINTRESOURCEW(14), name, group.language, directory.data(),
                            (DWORD) directory.size());
            if (!EndUpdateResourceW(update, FALSE)) {
                error = "无法修改程序图标";
                return false;
            }
            return true;
        }
#endif
    }

    void load() {
        if (state.active || !state.problems.empty()) {
            return;
        }
        std::filesystem::path exe = selfPath();
        if (exe.empty()) {
            return;
        }
        if (std::optional<std::string> bytes = appended(exe)) {
            if (open(std::move(*bytes), exe)) {
                return;
            }
        }
        std::filesystem::path beside = besideFile(exe);
        std::error_code ec;
        if (std::filesystem::is_regular_file(beside, ec)) {
            auto bytes = blb::fs::read_file(beside);
            if (bytes) {
                open(std::move(*bytes), beside);
            }
        }
    }

    void apply() {
        if (!state.active) {
            return;
        }
        nlohmann::json& doc = Config::data();
        bool changed = false;
        auto put = [&](const std::string& key, const nlohmann::json& value, bool force) {
            if ((force || !doc.contains(key)) && doc[key] != value) {
                doc[key] = value;
                changed = true;
            }
        };
        for (const auto& [key, setting] : state.settings) {
            put(key, setting.value, setting.locked);
        }
        if (!state.theme.empty()) {
            put("theme", state.theme, state.themeLocked);
        }
        std::error_code ec;
        std::filesystem::path folder = blb::launcher_data_directory() / "customize";
        auto extract = [&](const std::string& name, const std::string& target) -> std::string {
            std::optional<std::string> data = read(name);
            if (!data) {
                state.problems.push_back("定制包里缺少 " + name);
                return {};
            }
            std::filesystem::create_directories(folder, ec);
            std::filesystem::path file = folder / target;
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(data->data(), (std::streamsize) data->size());
            return out ? blb::path_to_utf8(file) : std::string();
        };
        if (!state.logo.empty()) {
            std::string ext = std::filesystem::path(state.logo).extension().string();
            std::string file = extract(state.logo, "logo" + ext);
            if (!file.empty()) {
                put("logoPath", file, false);
            }
        }
        if (state.homepageType == "file" && !state.homepageValue.empty()) {
            std::string file = extract(state.homepageValue, "home.xaml");
            if (!file.empty()) {
                put("homepage", "file", state.homepageLocked);
                put("homepageFile", file, state.homepageLocked);
            }
        } else if (state.homepageType == "url" && !state.homepageValue.empty()) {
            put("homepage", "url", state.homepageLocked);
            put("homepageUrl", state.homepageValue, state.homepageLocked);
        } else if (state.homepageType == "preset" && !state.homepageValue.empty()) {
            put("homepage", "preset", state.homepageLocked);
            put("homepagePreset", state.homepageValue, state.homepageLocked);
        } else if (state.homepageType == "announcement") {
            put("homepage", "announcement", state.homepageLocked);
        }
        if (changed) {
            Config::save();
        }
    }

    std::optional<int> command() {
        std::vector<std::string> args;
#if defined(_WIN32)
        int count = 0;
        LPWSTR* list = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 0; list != nullptr && i < count; i++) {
            int size = WideCharToMultiByte(CP_UTF8, 0, list[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(size > 0 ? (size_t) size - 1 : 0, '\0');
            WideCharToMultiByte(CP_UTF8, 0, list[i], -1, arg.data(), size, nullptr, nullptr);
            args.push_back(arg);
        }
        if (list != nullptr) {
            LocalFree(list);
        }
#elif defined(__APPLE__)
        int count = *_NSGetArgc();
        char** list = *_NSGetArgv();
        for (int i = 0; i < count; i++) {
            args.push_back(list[i]);
        }
#else
        std::ifstream in("/proc/self/cmdline", std::ios::binary);
        std::string arg;
        while (std::getline(in, arg, '\0')) {
            args.push_back(arg);
        }
#endif
        auto at = std::find(args.begin(), args.end(), "--make-pack");
        if (at == args.end()) {
            return std::nullopt;
        }
        std::string source = at + 1 != args.end() ? *(at + 1) : std::string();
        auto out = std::find(args.begin(), args.end(), "--out");
        std::string target = out != args.end() && out + 1 != args.end() ? *(out + 1) : std::string();
        std::string error;
        int code = 1;
        if (source.empty() || target.empty()) {
            error = "用法：--make-pack <目录或压缩包> --out <新程序>";
        } else {
            code = makePack(blb::path_from_utf8(source), blb::path_from_utf8(target), error);
        }
        std::string message = code == 0 ? "已生成定制版：" + target : "生成定制版失败：" + error;
        bool quiet = std::find(args.begin(), args.end(), "--quiet") != args.end();
#if defined(_WIN32)
        int size = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
        std::wstring wide((size_t) std::max(size, 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide.data(), size);
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            HANDLE console = GetStdHandle(code == 0 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
            std::wstring line = L"\n" + std::wstring(wide.c_str()) + L"\n";
            DWORD written = 0;
            WriteConsoleW(console, line.c_str(), (DWORD) line.size(), &written, nullptr);
            FreeConsole();
        } else if (!quiet) {
            MessageBoxW(nullptr, wide.c_str(), L"B Launcher", code == 0 ? MB_ICONINFORMATION : MB_ICONERROR);
        }
#else
        std::fprintf(code == 0 ? stdout : stderr, "%s\n", message.c_str());
#endif
        return code;
    }

    bool active() {
        return state.active;
    }

    const std::string& by() {
        return state.by;
    }

    const std::string& version() {
        return state.version;
    }

    const std::filesystem::path& origin() {
        return state.origin;
    }

    std::optional<std::string> read(const std::string& path) {
        if (!state.active || !blb::fs::safe_relative(path)) {
            return std::nullopt;
        }
        auto data = state.zip->read(path);
        if (!data) {
            return std::nullopt;
        }
        return *data;
    }

    std::vector<std::string> files(const std::string& prefix) {
        std::vector<std::string> out;
        for (const std::string& name : state.names) {
            if (name.rfind(prefix, 0) == 0) {
                out.push_back(name);
            }
        }
        return out;
    }

    const std::vector<std::string>& problems() {
        return state.problems;
    }

    std::string theme() {
        return state.theme;
    }

    bool themeLocked() {
        return state.active && state.themeLocked && !state.theme.empty();
    }

    std::string windowTitle() {
        return state.windowTitle;
    }

    std::string brandTitle() {
        return state.brandTitle;
    }

    std::string logo() {
        return state.logo;
    }

    std::string icon() {
        return state.icon;
    }

    std::string homepageType() {
        return state.homepageType;
    }

    std::string homepageValue() {
        return state.homepageValue;
    }

    bool homepageLocked() {
        return state.active && state.homepageLocked;
    }

    std::string announcement() {
        return state.announcement;
    }

    const std::vector<Link>& links() {
        return state.links;
    }

    const std::vector<Server>& servers() {
        return state.servers;
    }

    std::string autoJoin() {
        return state.autoJoin;
    }

    const std::vector<Login>& logins() {
        return state.logins;
    }

    bool loginAllowed(const std::string& type, const std::string& server) {
        if (!state.active || state.logins.empty()) {
            return true;
        }
        for (const Login& login : state.logins) {
            if (login.type != type) {
                continue;
            }
            if (login.server.empty() || server.empty()) {
                return true;
            }
            std::string a = login.server;
            std::string b = server;
            while (!a.empty() && a.back() == '/') {
                a.pop_back();
            }
            while (!b.empty() && b.back() == '/') {
                b.pop_back();
            }
            if (a == b) {
                return true;
            }
        }
        return false;
    }

    std::string modpack() {
        return state.modpack;
    }

    std::string modpackInstance() {
        return state.modpackInstance;
    }

    bool singleInstance() {
        return state.active && state.singleInstance;
    }

    std::vector<std::string> hiddenFeatures() {
        return state.hidden;
    }

    bool featuresLocked() {
        return state.active && state.featuresLocked;
    }

    bool preset(const std::string& key, nlohmann::json& value) {
        auto found = state.settings.find(key);
        if (found == state.settings.end()) {
            return false;
        }
        value = found->second.value;
        return true;
    }

    bool locked(const std::string& key) {
        auto found = state.settings.find(key);
        return found != state.settings.end() && found->second.locked;
    }

    bool updatesDisabled() {
        return state.active && state.updatesDisabled;
    }

    int makePack(const std::filesystem::path& source, const std::filesystem::path& output, std::string& error) {
        std::filesystem::path exe = selfPath();
        auto program = blb::fs::read_file(exe);
        if (!program) {
            error = "无法读取启动器程序";
            return 1;
        }
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
        if (trailerOf(exe, offset, length, nullptr)) {
            program->resize((size_t) offset);
        }
        std::string pack;
        if (!collect(source, pack, error)) {
            return 1;
        }
        {
            std::ofstream out(output, std::ios::binary | std::ios::trunc);
            out.write(program->data(), (std::streamsize) program->size());
            if (!out) {
                error = "无法写入 " + blb::path_to_utf8(output);
                return 1;
            }
        }
#if defined(_WIN32)
        auto zip = blb::zip_reader::open_memory(pack);
        nlohmann::json doc;
        if (zip) {
            auto config = zip->read("customize.json");
            doc = config ? nlohmann::json::parse(*config, nullptr, false, true) : nlohmann::json();
        }
        auto branding = doc.is_object() ? doc.find("branding") : doc.end();
        std::string iconPath = doc.is_object() && branding != doc.end() && branding->is_object()
                ? text(*branding, "icon")
                : std::string();
        bool packed = program->find("UPX!") != std::string::npos && program->find("UPX!") < 4096;
        if (!iconPath.empty() && zip && !packed) {
            auto ico = zip->read(iconPath);
            std::string iconError;
            if (ico && !replaceIcon(output, *ico, iconError)) {
                error = iconError;
                return 1;
            }
        }
#endif
        blb::sha256_digest sum = blb::sha256::digest(pack.data(), pack.size());
        auto size = std::filesystem::file_size(output);
        std::string trailer(MAGIC, MAGIC + 8);
        writeU64(trailer, (std::uint64_t) size);
        writeU64(trailer, (std::uint64_t) pack.size());
        trailer.append((const char*) sum.data(), sum.size());
        std::ofstream out(output, std::ios::binary | std::ios::app);
        out.write(pack.data(), (std::streamsize) pack.size());
        out.write(trailer.data(), (std::streamsize) trailer.size());
        if (!out) {
            error = "无法写入 " + blb::path_to_utf8(output);
            return 1;
        }
        return 0;
    }
}

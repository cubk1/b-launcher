#include "ui/imgui/screens/theme/ThemeImport.h"

#include "app/system/Notices.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/theme/ThemeAssets.h"
#include "ui/imgui/screens/theme/ThemePreview.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeShaders.h"
#include "ui/imgui/screens/theme/ThemeStore.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ThemeImport {

    namespace blb = b_launcher_base;

    namespace {
        constexpr size_t MAX_FILES = 512;
        constexpr std::uint64_t MAX_TOTAL = (std::uint64_t) 64 << 20;
        constexpr std::uint64_t MAX_TEXT = (std::uint64_t) 1 << 20;
        constexpr std::uint64_t MAX_SOUND = (std::uint64_t) 1 << 20;
        const char* const ALLOWED[] = {".json", ".js",  ".png",  ".jpg", ".jpeg", ".ttf",
                                       ".otf",  ".ttc", ".glsl", ".txt", ".wav"};
        const char* const TEXTS[] = {".json", ".js", ".glsl", ".txt"};
        const char* const SOUNDS[] = {".wav"};
        const char* const REPLACE_DIALOG = "theme#replace";

        struct Staged {
            std::string id;
            std::string name;
        };

        struct Result {
            std::filesystem::path staging;
            std::vector<Staged> themes;
            std::string error;
        };

        std::thread worker;
        std::mutex lock;
        std::shared_ptr<Result> finished;
        bool working = false;

        std::filesystem::path staging;
        std::vector<Staged> conflicts;

        std::string watchedId;
        std::filesystem::file_time_type watchedStamp;
        bool watchChanged = false;
        std::chrono::steady_clock::time_point nextWatch;
        std::chrono::steady_clock::time_point changedAt;

        std::string lower(std::string text) {
            for (char& c : text) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return text;
        }

        bool endsWith(const std::string& name, const char* const* list, size_t count) {
            std::string low = lower(name);
            for (size_t i = 0; i < count; i++) {
                if (low.ends_with(list[i])) {
                    return true;
                }
            }
            return false;
        }

        std::string sanitize(const std::string& raw) {
            std::string out;
            for (char c : lower(raw)) {
                bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                out += ok ? c : '-';
            }
            while (!out.empty() && (out[0] == '.' || out[0] == '_' || out[0] == '-')) {
                out.erase(out.begin());
            }
            if (out.size() > 48) {
                out.resize(48);
            }
            return out.empty() ? std::string("theme") : out;
        }

        std::string unique(std::string id, const std::set<std::string>& taken) {
            if (ThemeStore::builtIn(id)) {
                id += "-user";
            }
            std::string next = id;
            for (int n = 2; taken.count(next) != 0; n++) {
                next = id + "-" + std::to_string(n);
            }
            return next;
        }

        std::vector<std::string> roots(const std::vector<blb::zip_entry>& files) {
            std::vector<std::string> out;
            for (const blb::zip_entry& entry : files) {
                if (entry.name == "theme.json") {
                    return {""};
                }
            }
            for (const blb::zip_entry& entry : files) {
                const std::string& name = entry.name;
                if (name.ends_with("/theme.json") && std::count(name.begin(), name.end(), '/') == 1) {
                    out.push_back(name.substr(0, name.size() - 10));
                }
            }
            return out;
        }

        Result work(const std::filesystem::path& zip) {
            Result result;
            auto reader = blb::zip_reader::open(zip);
            if (!reader) {
                result.error = "无法打开这个压缩包";
                return result;
            }
            auto entries = reader->entries();
            if (!entries) {
                result.error = "无法读取这个压缩包";
                return result;
            }
            std::vector<blb::zip_entry> files;
            std::uint64_t total = 0;
            for (const blb::zip_entry& entry : *entries) {
                if (entry.directory) {
                    continue;
                }
                if (!blb::fs::safe_relative(entry.name)) {
                    result.error = "压缩包里有不安全的路径";
                    return result;
                }
                total += entry.size;
                files.push_back(entry);
            }
            if (files.size() > MAX_FILES) {
                result.error = "压缩包里的文件超过 512 个";
                return result;
            }
            if (total > MAX_TOTAL) {
                result.error = "解压后超过 64 MB";
                return result;
            }
            std::vector<std::string> prefixes = roots(files);
            if (prefixes.empty()) {
                result.error = "压缩包里没有 theme.json";
                return result;
            }
            std::set<std::string> taken;
            result.staging = ThemeStore::folder() / (".import-" + blb::random_bytes_hex(6));
            for (const std::string& prefix : prefixes) {
                std::string id =
                        sanitize(prefix.empty() ? blb::path_to_utf8(zip.stem()) : prefix.substr(0, prefix.size() - 1));
                id = unique(id, taken);
                taken.insert(id);
                std::filesystem::path root = result.staging / id;
                std::string name = id;
                for (const blb::zip_entry& entry : files) {
                    if (entry.name.rfind(prefix, 0) != 0) {
                        continue;
                    }
                    std::string relative = entry.name.substr(prefix.size());
                    if (!endsWith(relative, ALLOWED, std::size(ALLOWED))) {
                        continue;
                    }
                    if ((endsWith(relative, TEXTS, std::size(TEXTS)) && entry.size > MAX_TEXT) ||
                        (endsWith(relative, SOUNDS, std::size(SOUNDS)) && entry.size > MAX_SOUND)) {
                        result.error = relative + " 超过 1 MB";
                        return result;
                    }
                    std::filesystem::path target = root / blb::path_from_utf8(relative);
                    if (!blb::fs::is_inside(root, target)) {
                        result.error = "压缩包里有不安全的路径";
                        return result;
                    }
                    std::error_code ec;
                    std::filesystem::create_directories(target.parent_path(), ec);
                    if (!reader->extract(entry.name, target)) {
                        result.error = "无法解压 " + relative;
                        return result;
                    }
                    if (relative == "theme.json") {
                        auto text = blb::fs::read_file(target);
                        nlohmann::json doc =
                                text ? nlohmann::json::parse(*text, nullptr, false, true) : nlohmann::json();
                        auto label = doc.is_object() ? doc.find("name") : doc.end();
                        if (doc.is_object() && label != doc.end() && label->is_string()) {
                            name = label->get<std::string>();
                        }
                    }
                }
                result.themes.push_back({id, name});
            }
            return result;
        }

        void clearStaging() {
            if (!staging.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(staging, ec);
                staging.clear();
            }
        }

        void refreshed(bool current) {
            ThemeStore::rescan();
            ThemePreview::forget();
            if (current) {
                ThemeShaders::reset();
                ThemeAssets::reset();
                ThemeRuntime::reload();
            }
        }

        void install(const Staged& theme, bool replace) {
            std::error_code ec;
            std::filesystem::path target = ThemeStore::folder() / theme.id;
            if (replace) {
                std::filesystem::remove_all(target, ec);
            }
            std::filesystem::rename(staging / theme.id, target, ec);
            if (ec) {
                Notices::add(Notices::Kind::Failed, "无法导入主题 " + theme.name, 4000);
                return;
            }
            refreshed(replace && ThemeRuntime::current() == theme.id);
            Notices::add(Notices::Kind::Done, "已导入主题 " + theme.name, 3000);
        }

        void askNext();

        void replaceBody(float x, float y, float w, float h) {
            if (conflicts.empty()) {
                return;
            }
            const Staged& theme = conflicts.front();
            UiRender::textY("替换主题", x + 20.0f, y + 16.0f, 24.0f, UiTheme::textPrimary(), UiFont::BOLD,
                            UiWidgets::TEXT_TITLE);
            std::string line = UiFont::fitEnd("已经有一个 id 为 " + theme.id + " 的主题，要替换它吗？", UiFont::MEDIUM,
                                              UiWidgets::TEXT_CONTROL, w - 40.0f);
            UiRender::textY(line, x + 20.0f, y + 50.0f, 22.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float buttonY = y + h - 20.0f - Page::FIELD_H;
            if (Page::button("theme#replace#skip", "跳过", x + w - 20.0f - 180.0f, buttonY, 84.0f, false, false)) {
                std::error_code ec;
                std::filesystem::remove_all(staging / theme.id, ec);
                conflicts.erase(conflicts.begin());
                Dialog::close();
                askNext();
                return;
            }
            if (Page::button("theme#replace#ok", "替换", x + w - 20.0f - 88.0f, buttonY, 88.0f, true, false)) {
                Staged chosen = theme;
                conflicts.erase(conflicts.begin());
                install(chosen, true);
                Dialog::close();
                askNext();
            }
        }

        void askNext() {
            if (conflicts.empty()) {
                clearStaging();
                return;
            }
            Dialog::open(REPLACE_DIALOG, 400.0f, 140.0f, replaceBody, [] {
                if (!conflicts.empty() && !Dialog::isOpen(REPLACE_DIALOG)) {
                    for (const Staged& left : conflicts) {
                        std::error_code ec;
                        std::filesystem::remove_all(staging / left.id, ec);
                    }
                    conflicts.clear();
                    clearStaging();
                }
            });
        }

        std::filesystem::file_time_type newest(const std::filesystem::path& folder) {
            std::filesystem::file_time_type latest{};
            std::error_code ec;
            int seen = 0;
            for (std::filesystem::recursive_directory_iterator it(folder, ec), end; !ec && it != end && seen < 512;
                 it.increment(ec), seen++) {
                std::filesystem::file_time_type stamp = it->last_write_time(ec);
                if (!ec && stamp > latest) {
                    latest = stamp;
                }
            }
            return latest;
        }

        void watch() {
            const std::string& id = ThemeRuntime::current();
            if (id.empty() || ThemeStore::builtIn(id) || ThemeStore::find(id) == nullptr) {
                watchedId.clear();
                return;
            }
            auto now = std::chrono::steady_clock::now();
            if (now < nextWatch) {
                return;
            }
            nextWatch = now + std::chrono::milliseconds(500);
            const ThemeStore::Entry* entry = ThemeStore::find(id);
            std::error_code ec;
            std::filesystem::file_time_type latest = entry != nullptr && !entry->zip.empty()
                    ? std::filesystem::last_write_time(entry->zip, ec)
                    : newest(ThemeStore::folder() / id);
            if (id != watchedId) {
                watchedId = id;
                watchedStamp = latest;
                watchChanged = false;
                return;
            }
            if (latest != watchedStamp) {
                watchedStamp = latest;
                watchChanged = true;
                changedAt = now;
                return;
            }
            if (watchChanged && now - changedAt >= std::chrono::milliseconds(300)) {
                watchChanged = false;
                refreshed(true);
            }
        }
    }

    bool looksLikeTheme(const std::filesystem::path& zip) {
        auto reader = blb::zip_reader::open(zip);
        if (!reader) {
            return false;
        }
        auto entries = reader->entries();
        return entries && !roots(*entries).empty();
    }

    void start(const std::filesystem::path& zip) {
        if (working) {
            Notices::add(Notices::Kind::Failed, "正在导入主题，稍后再试", 3000);
            return;
        }
        working = true;
        if (worker.joinable()) {
            worker.join();
        }
        worker = std::thread([zip] {
            auto result = std::make_shared<Result>(work(zip));
            std::lock_guard<std::mutex> guard(lock);
            finished = result;
        });
    }

    bool busy() {
        return working;
    }

    void poll() {
        std::shared_ptr<Result> result;
        {
            std::lock_guard<std::mutex> guard(lock);
            result.swap(finished);
        }
        if (result) {
            working = false;
            if (worker.joinable()) {
                worker.join();
            }
            if (!result->error.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(result->staging, ec);
                Notices::add(Notices::Kind::Failed, "导入主题失败：" + result->error, 5000);
            } else {
                clearStaging();
                staging = result->staging;
                conflicts.clear();
                for (const Staged& theme : result->themes) {
                    std::error_code ec;
                    if (std::filesystem::exists(ThemeStore::folder() / theme.id, ec)) {
                        conflicts.push_back(theme);
                    } else {
                        install(theme, false);
                    }
                }
                askNext();
            }
        }
        watch();
    }

    void shutdown() {
        if (worker.joinable()) {
            worker.join();
        }
        std::lock_guard<std::mutex> guard(lock);
        if (finished) {
            std::error_code ec;
            std::filesystem::remove_all(finished->staging, ec);
            finished.reset();
        }
        clearStaging();
    }
}

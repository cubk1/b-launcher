#include "app/instances/Instances.h"
#include "app/instances/Installer.h"
#include "app/system/Config.h"
#include "app/system/FolderWatch.h"
#include "app/system/Platform.h"
#include "app/system/Notices.h"

#include <core/base/hash.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Instances {

    namespace {
        using Continuation = std::function<void()>;
        using Job = std::function<Continuation()>;
        using Clock = std::chrono::steady_clock;

        constexpr double SAVE_DELAY = 0.5;
        // Instances changed outside the launcher are read again once the change has been over for this long.
        constexpr double SETTLE = 1.0;
        const char* const FOLDERS = "gameFolders";
        const char* const ORDER = "instanceOrder";

        std::thread worker;
        std::mutex lock;
        std::condition_variable wake;
        std::deque<Job> jobs;
        std::deque<Continuation> finished;
        bool stopping = false;

        std::vector<Source> roots;
        blb::instance::isolation_mode mode = blb::instance::isolation_mode::modded;
        std::vector<Entry> entries;
        std::map<std::string, std::vector<std::string>> arranged;
        std::string selection;
        bool scanned = false;
        int running = 0;
        std::string activityText;
        // What runs on each instance being copied, renamed or removed.
        std::unordered_map<std::string, std::string> operating;
        std::unordered_map<std::string, Clock::time_point> pendingSaves;
        int scansInFlight = 0;

        // What changed in the game folders since they were last read: the versions touched, by game folder, or
        // everything when there is no telling which.
        struct Changes {
            std::map<std::filesystem::path, std::set<std::string>> versions;
            bool everything = false;
        };
        // The game folders that exist, watched for instances changed outside the launcher. The changes are filled on
        // the watcher's thread, and changedAt is when the last came, on the steady clock, 0 once they are taken.
        FolderWatch watch;
        std::mutex changesLock;
        Changes changes;
        std::atomic<Clock::rep> changedAt = 0;

        struct Scan {
            std::vector<Entry> entries;
            std::unordered_map<std::string, bool> exists;
        };

        blb::instance::instance_manager managerFor(const std::filesystem::path& root) {
            return blb::instance::instance_manager(blb::meta::game_layout(root), mode);
        }

        void place(Entry& entry, const blb::instance::instance_manager& with) {
            entry.isolated = with.is_isolated(entry.info);
            entry.gameDirectory = with.game_directory(entry.info);
        }

        bool sameName(const std::wstring& a, const std::wstring& b) {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                              [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
        }

        // What a change in a game folder touches: the version whose folder comes or goes, or whose json or settings
        // beside the json change, or everything (an empty name) when the versions folder itself moves or when there is
        // no telling. What the game writes while it runs touches nothing.
        std::optional<std::wstring> touched(const std::wstring& path, bool moved) {
            if (path.empty()) {
                return std::wstring();
            }
            std::vector<std::wstring> parts;
            for (size_t start = 0;;) {
                size_t slash = path.find(L'\\', start);
                parts.push_back(path.substr(start, slash - start));
                if (slash == std::wstring::npos) {
                    break;
                }
                start = slash + 1;
            }
            if (!sameName(parts[0], L"versions") || (parts.size() <= 2 && !moved)) {
                return std::nullopt;
            }
            if (parts.size() <= 2) {
                return parts.size() == 1 ? std::wstring() : parts[1];
            }
            const std::wstring settings(blb::instance::settings_file.begin(), blb::instance::settings_file.end());
            if (parts.size() == 3 && (sameName(parts[2], parts[1] + L".json") || sameName(parts[2], settings))) {
                return parts[1];
            }
            return std::nullopt;
        }

        // Watches the game folders that exist now, if they are not the ones watched already.
        void follow() {
            std::vector<std::filesystem::path> folders;
            for (const Source& source : roots) {
                if (source.exists) {
                    folders.push_back(source.root);
                }
            }
            if (folders == watch.folders()) {
                return;
            }
            watch.start(folders, [](const std::filesystem::path& folder, const std::wstring& path, bool moved) {
                std::optional<std::wstring> version = touched(path, moved);
                if (!version) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> guard(changesLock);
                    if (version->empty()) {
                        changes.everything = true;
                    } else {
                        changes.versions[folder].insert(blb::path_to_utf8(std::filesystem::path(*version)));
                    }
                }
                changedAt = Clock::now().time_since_epoch().count();
            });
        }

        Scan scan(const std::vector<Source>& from, blb::instance::isolation_mode isolation) {
            Scan out;
            // It reads what changed so far.
            {
                std::lock_guard<std::mutex> guard(changesLock);
                changes = Changes();
            }
            for (const Source& source : from) {
                std::error_code error;
                bool exists = std::filesystem::is_directory(source.root, error);
                out.exists[source.key] = exists;
                if (!exists) {
                    continue;
                }
                blb::instance::instance_manager with(blb::meta::game_layout(source.root), isolation);
                for (blb::instance::instance& info : with.list()) {
                    Entry entry;
                    entry.source = source.key;
                    entry.key = keyOf(source.key, info.id);
                    entry.info = std::move(info);
                    place(entry, with);
                    out.entries.push_back(std::move(entry));
                }
            }
            return out;
        }

        size_t rankOf(const std::string& source) {
            return (size_t) (std::find_if(roots.begin(), roots.end(),
                                          [&](const Source& root) { return root.key == source; }) -
                             roots.begin());
        }

        size_t positionOf(const Entry& entry) {
            auto found = arranged.find(entry.source);
            if (found == arranged.end()) {
                return SIZE_MAX;
            }
            auto at = std::find(found->second.begin(), found->second.end(), entry.info.id);
            return at == found->second.end() ? SIZE_MAX : (size_t) (at - found->second.begin());
        }

        void sortEntries() {
            std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                size_t rankA = rankOf(a.source);
                size_t rankB = rankOf(b.source);
                return rankA != rankB ? rankA < rankB : positionOf(a) < positionOf(b);
            });
        }

        void saveOrder() {
            Config::data()[ORDER] = arranged;
            if (!Config::save()) {
                notify("无法保存启动器配置", true);
            }
        }

        void apply(Scan& result) {
            std::vector<Entry> next;
            for (Entry& entry : result.entries) {
                if (std::none_of(roots.begin(), roots.end(),
                                 [&](const Source& source) { return source.key == entry.source; })) {
                    continue;
                }
                if (pendingSaves.count(entry.key) != 0) {
                    for (const Entry& old : entries) {
                        if (old.key == entry.key) {
                            entry.info.settings = old.info.settings;
                            entry.isolated = old.isolated;
                            entry.gameDirectory = old.gameDirectory;
                        }
                    }
                }
                next.push_back(std::move(entry));
            }
            entries = std::move(next);
            sortEntries();
            for (Source& source : roots) {
                auto found = result.exists.find(source.key);
                if (found != result.exists.end()) {
                    source.exists = found->second;
                }
            }
            scanned = true;
            follow();
        }

        void runWorker() {
            for (;;) {
                Job job;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [] { return stopping || !jobs.empty(); });
                    if (jobs.empty()) {
                        return;
                    }
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                Continuation next;
                try {
                    next = job();
                } catch (const std::exception& error) {
                    std::string text = error.what();
                    next = [text] { notify("操作失败：" + text, true); };
                }
                std::lock_guard<std::mutex> guard(lock);
                finished.push_back(std::move(next));
            }
        }

        // Tells of the instances read that the list does not have yet.
        void announce(const std::vector<Entry>& read) {
            std::vector<std::string> found;
            for (const Entry& entry : read) {
                if (std::none_of(entries.begin(), entries.end(),
                                 [&](const Entry& old) { return old.key == entry.key; }) &&
                    std::any_of(roots.begin(), roots.end(),
                                [&](const Source& source) { return source.key == entry.source; })) {
                    found.push_back(entry.info.id);
                }
            }
            if (found.size() == 1) {
                notify("发现新实例 " + found.front() + "，已加入列表", false);
            } else if (found.size() > 1) {
                notify("发现 " + std::to_string(found.size()) + " 个新实例，已加入列表", false);
            }
        }

        void submit(Job job) {
            {
                std::lock_guard<std::mutex> guard(lock);
                jobs.push_back(std::move(job));
            }
            wake.notify_one();
        }

        void loadConfig() {
            const nlohmann::json& doc = Config::data();
            if (auto it = doc.find("selectedInstance"); it != doc.end() && it->is_string()) {
                selection = it->get<std::string>();
            }
            if (auto it = doc.find("isolation"); it != doc.end() && it->is_string()) {
                if (auto parsed = blb::instance::parse_isolation(it->get<std::string>())) {
                    mode = *parsed;
                }
            }
            if (auto it = doc.find(ORDER); it != doc.end() && it->is_object()) {
                for (auto group = it->begin(); group != it->end(); ++group) {
                    if (!group.value().is_array()) {
                        continue;
                    }
                    std::vector<std::string>& ids = arranged[group.key()];
                    for (const nlohmann::json& id : group.value()) {
                        if (id.is_string()) {
                            ids.push_back(id.get<std::string>());
                        }
                    }
                }
            }
        }

        void saveConfig() {
            nlohmann::json& doc = Config::data();
            doc.erase("gameDirectory");
            doc["isolation"] = std::string(blb::instance::isolation_name(mode));
            doc["selectedInstance"] = selection;
            if (!Config::save()) {
                notify("无法保存启动器配置", true);
            }
        }

        std::string folderKey(const std::filesystem::path& root) {
            std::string text = blb::to_lower(blb::path_to_utf8(root.lexically_normal()));
            while (!text.empty() && (text.back() == '\\' || text.back() == '/')) {
                text.pop_back();
            }
            return "dir-" + blb::to_hex(blb::sha1::digest(text)).substr(0, 10);
        }

        std::string folderLabel(const std::filesystem::path& root) {
            std::filesystem::path clean = root.lexically_normal();
            if (!clean.has_filename()) {
                clean = clean.parent_path();
            }
            std::string name = blb::path_to_utf8(clean.filename());
            if (blb::iequals(name, ".minecraft")) {
                std::string parent = blb::path_to_utf8(clean.parent_path().filename());
                if (!parent.empty()) {
                    return parent;
                }
            }
            return name.empty() ? Platform::display(clean) : name;
        }

        bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
            std::error_code error;
            return std::filesystem::equivalent(a, b, error) || folderKey(a) == folderKey(b);
        }

        void saveFolders() {
            nlohmann::json list = nlohmann::json::array();
            for (const Source& source : roots) {
                if (source.custom) {
                    list.push_back(blb::path_to_utf8(source.root));
                }
            }
            Config::data()[FOLDERS] = list;
            if (!Config::save()) {
                notify("无法保存启动器配置", true);
            }
        }

        void loadFolders() {
            const nlohmann::json& doc = Config::data();
            auto list = doc.find(FOLDERS);
            if (list == doc.end() || !list->is_array()) {
                return;
            }
            for (const nlohmann::json& item : *list) {
                if (!item.is_string()) {
                    continue;
                }
                std::filesystem::path root = blb::path_from_utf8(item.get<std::string>());
                if (std::none_of(roots.begin(), roots.end(),
                                 [&](const Source& source) { return sameFolder(source.root, root); })) {
                    roots.push_back(Source{folderKey(root), folderLabel(root), root, false, true});
                }
            }
        }

        Source* sourceMutable(const std::string& key) {
            for (Source& source : roots) {
                if (source.key == key) {
                    return &source;
                }
            }
            return nullptr;
        }

        Entry* findMutable(const std::string& key) {
            for (Entry& entry : entries) {
                if (entry.key == key) {
                    return &entry;
                }
            }
            return nullptr;
        }

        void writeSettings(const std::string& key) {
            Entry* entry = findMutable(key);
            const Source* from = entry != nullptr ? sourceMutable(entry->source) : nullptr;
            if (from == nullptr) {
                return;
            }
            auto saved = managerFor(from->root).save_settings(entry->info.id, entry->info.settings);
            if (!saved) {
                notify("保存 " + entry->info.id + " 的设置失败：" + saved.err().message, true);
            }
        }

        void flushSaves() {
            for (const auto& pending : pendingSaves) {
                writeSettings(pending.first);
            }
            pendingSaves.clear();
        }

        std::string lowered(std::string text) {
            for (char& c : text) {
                c = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
            }
            return text;
        }

        // The instances to read again, by game folder, after their versions changed: those versions, the instances
        // made of them, and those that did not resolve, which may have lacked one of them.
        std::map<std::string, std::set<std::string>>
        affected(const std::map<std::filesystem::path, std::set<std::string>>& versions) {
            std::map<std::string, std::set<std::string>> out;
            for (const auto& [folder, changed] : versions) {
                auto source = std::find_if(roots.begin(), roots.end(),
                                           [&](const Source& root) { return root.root == folder; });
                if (source == roots.end()) {
                    continue;
                }
                std::set<std::string>& ids = out[source->key];
                std::set<std::string> names;
                for (const std::string& id : changed) {
                    ids.insert(id);
                    names.insert(lowered(id));
                }
                for (const Entry& entry : entries) {
                    if (entry.source == source->key &&
                        (!entry.info.valid ||
                         std::any_of(entry.info.chain.begin(), entry.info.chain.end(),
                                     [&](const std::string& id) { return names.count(lowered(id)) != 0; }))) {
                        ids.insert(entry.info.id);
                    }
                }
            }
            return out;
        }

        // Instances read again one by one: those read, and those found gone.
        struct Update {
            std::vector<Entry> read;
            std::vector<std::string> gone;
        };

        Update reread(const std::vector<Source>& from, const std::map<std::string, std::set<std::string>>& ids,
                      blb::instance::isolation_mode isolation) {
            Update out;
            for (const Source& source : from) {
                auto wanted = ids.find(source.key);
                if (wanted == ids.end()) {
                    continue;
                }
                blb::instance::instance_manager with(blb::meta::game_layout(source.root), isolation);
                for (const std::string& id : wanted->second) {
                    auto info = with.get(id);
                    if (!info) {
                        out.gone.push_back(keyOf(source.key, id));
                        continue;
                    }
                    Entry entry;
                    entry.source = source.key;
                    entry.key = keyOf(source.key, id);
                    entry.info = std::move(*info);
                    place(entry, with);
                    out.read.push_back(std::move(entry));
                }
            }
            return out;
        }

        // Takes in instances read again, keeping the settings still to be saved, in the order of a scan: by game
        // folder, then by name.
        void merge(Update& update) {
            for (Entry& entry : update.read) {
                Entry* old = findMutable(entry.key);
                if (old == nullptr) {
                    if (sourceMutable(entry.source) != nullptr) {
                        entries.push_back(std::move(entry));
                    }
                    continue;
                }
                if (pendingSaves.count(entry.key) != 0) {
                    entry.info.settings = old->info.settings;
                    entry.isolated = old->isolated;
                    entry.gameDirectory = old->gameDirectory;
                }
                *old = std::move(entry);
            }
            for (const std::string& key : update.gone) {
                std::erase_if(entries, [&](const Entry& entry) { return entry.key == key; });
            }
            std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                size_t rankA = rankOf(a.source);
                size_t rankB = rankOf(b.source);
                return rankA != rankB ? rankA < rankB : a.info.id < b.info.id;
            });
            sortEntries();
        }

        void operate(const std::string& key, const std::string& activity, const std::string& success,
                     const std::string& failure,
                     std::function<blb::result<void>(const blb::instance::instance_manager&, const std::string& id)>
                             operation,
                     Done done) {
            const Entry* entry = findMutable(key);
            const Source* from = entry != nullptr ? sourceMutable(entry->source) : nullptr;
            if (from == nullptr) {
                if (done) {
                    done("找不到这个实例");
                }
                return;
            }
            flushSaves();
            blb::instance::instance_manager with = managerFor(from->root);
            std::string id = entry->info.id;
            std::vector<Source> all = roots;
            blb::instance::isolation_mode isolation = mode;
            running++;
            activityText = activity + " " + id;
            operating[key] = activity;
            std::string notice = Notices::live(Notices::Kind::Info, activityText);
            submit([key, with, id, all, isolation, success, failure, notice, operation, done]() -> Continuation {
                blb::result<void> outcome = operation(with, id);
                std::string error = outcome ? std::string() : outcome.err().message;
                Scan fresh = scan(all, isolation);
                return [key, success, failure, notice, done, error, fresh]() mutable {
                    if (--running == 0) {
                        activityText.clear();
                    }
                    operating.erase(key);
                    apply(fresh);
                    if (error.empty()) {
                        Notices::finish(notice, Notices::Kind::Done, success, 2500);
                    } else {
                        Notices::finish(notice, Notices::Kind::Failed, failure + error, 4000);
                    }
                    if (done) {
                        done(error);
                    }
                };
            });
        }

        std::string sourceOf(const std::string& key) {
            size_t slash = key.find('/');
            return slash == std::string::npos ? std::string() : key.substr(0, slash);
        }

        void rearrange(const std::string& key, const std::function<void(std::vector<std::string>&, size_t)>& edit) {
            size_t slash = key.find('/');
            auto found = arranged.find(sourceOf(key));
            if (slash == std::string::npos || found == arranged.end()) {
                return;
            }
            std::vector<std::string>& ids = found->second;
            auto at = std::find(ids.begin(), ids.end(), key.substr(slash + 1));
            if (at == ids.end()) {
                return;
            }
            edit(ids, (size_t) (at - ids.begin()));
            sortEntries();
            saveOrder();
        }
    }

    void init() {
        loadConfig();
        std::filesystem::path appData = Platform::defaultMinecraft();
        std::filesystem::path local = Platform::exeDirectory() / L".minecraft";
        std::error_code error;
        if (appData.empty() || !std::filesystem::equivalent(appData, local, error)) {
            roots.push_back(Source{"local", "当前目录", local, false});
        }
        if (!appData.empty()) {
            roots.push_back(Source{"appdata", "APPDATA", appData, false});
        }
        loadFolders();
        worker = std::thread(runWorker);
        refresh();
    }

    void poll() {
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            if (next) {
                next();
            }
        }
        Clock::time_point now = Clock::now();
        for (auto it = pendingSaves.begin(); it != pendingSaves.end();) {
            if (std::chrono::duration<double>(now - it->second).count() >= SAVE_DELAY) {
                writeSettings(it->first);
                it = pendingSaves.erase(it);
            } else {
                ++it;
            }
        }
        // While the launcher changes instances itself, it reads them again when it is done anyway.
        Clock::rep at = changedAt;
        if (at != 0 && scanned && scansInFlight == 0 && running == 0 && !Installer::running() &&
            std::chrono::duration<double>(now - Clock::time_point(Clock::duration(at))).count() >= SETTLE &&
            changedAt.compare_exchange_strong(at, 0)) {
            Changes taken;
            {
                std::lock_guard<std::mutex> guard(changesLock);
                taken = std::move(changes);
                changes = Changes();
            }
            flushSaves();
            std::vector<Source> all = roots;
            blb::instance::isolation_mode isolation = mode;
            if (taken.everything) {
                scansInFlight++;
                submit([all, isolation]() -> Continuation {
                    Scan fresh = scan(all, isolation);
                    return [fresh]() mutable {
                        scansInFlight--;
                        if (running == 0 && !Installer::running()) {
                            announce(fresh.entries);
                        }
                        apply(fresh);
                    };
                });
            } else if (!taken.versions.empty()) {
                // Only the instances the changes touch are read again.
                std::map<std::string, std::set<std::string>> ids = affected(taken.versions);
                scansInFlight++;
                submit([all, isolation, ids]() -> Continuation {
                    Update update = reread(all, ids, isolation);
                    return [update]() mutable {
                        scansInFlight--;
                        if (running == 0 && !Installer::running()) {
                            announce(update.read);
                        }
                        merge(update);
                    };
                });
            }
        }
    }

    void shutdown() {
        watch.stop();
        flushSaves();
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
        }
        wake.notify_one();
        if (worker.joinable()) {
            worker.join();
        }
    }

    bool loading() {
        return !scanned;
    }

    const std::string& activity() {
        return activityText;
    }

    std::string activityOf(const std::string& key) {
        auto found = operating.find(key);
        return found != operating.end() ? found->second : std::string();
    }

    const std::vector<Source>& sources() {
        return roots;
    }

    const Source* source(const std::string& key) {
        return sourceMutable(key);
    }

    const std::vector<Entry>& list() {
        return entries;
    }

    const Entry* find(const std::string& key) {
        return findMutable(key);
    }

    std::string keyOf(const std::string& source, const std::string& id) {
        return source + "/" + id;
    }

    const std::string& selected() {
        return selection;
    }

    void select(const std::string& key) {
        if (key == selection) {
            return;
        }
        selection = key;
        saveConfig();
    }

    void arrange(const std::string& source, const std::vector<std::string>& keys) {
        std::vector<std::string> ids;
        for (const std::string& key : keys) {
            const Entry* entry = findMutable(key);
            if (entry != nullptr && entry->source == source) {
                ids.push_back(entry->info.id);
            }
        }
        arranged[source] = ids;
        sortEntries();
        saveOrder();
    }

    blb::instance::isolation_mode isolation() {
        return mode;
    }

    void setIsolation(blb::instance::isolation_mode next) {
        if (next == mode) {
            return;
        }
        mode = next;
        for (Entry& entry : entries) {
            if (const Source* from = sourceMutable(entry.source)) {
                place(entry, managerFor(from->root));
            }
        }
        saveConfig();
    }

    void refresh() {
        refresh(nullptr);
    }

    void refresh(std::function<void()> done) {
        flushSaves();
        std::vector<Source> all = roots;
        blb::instance::isolation_mode isolation = mode;
        scansInFlight++;
        submit([all, isolation, done]() -> Continuation {
            Scan fresh = scan(all, isolation);
            return [fresh, done]() mutable {
                scansInFlight--;
                apply(fresh);
                if (done) {
                    done();
                }
            };
        });
    }

    void createFolder(const std::string& key) {
        const Source* target = sourceMutable(key);
        if (target == nullptr) {
            return;
        }
        std::error_code error;
        std::filesystem::create_directories(target->root / L"versions", error);
        if (error) {
            notify("无法创建 " + Platform::display(target->root) + "：" + error.message(), true);
            return;
        }
        notify("已创建 " + Platform::display(target->root), false);
        refresh();
    }

    std::string addFolder(const std::filesystem::path& picked) {
        std::error_code error;
        std::filesystem::path root = picked;
        if (!std::filesystem::is_directory(root / L"versions", error) &&
            std::filesystem::is_directory(root / L".minecraft", error)) {
            root /= L".minecraft";
        }
        if (!std::filesystem::is_directory(root, error)) {
            return "找不到这个文件夹";
        }
        for (const Source& source : roots) {
            if (sameFolder(source.root, root)) {
                return "这个游戏目录已经在列表里（" + source.label + "）";
            }
        }
        roots.push_back(Source{folderKey(root), folderLabel(root), root, true, true});
        saveFolders();
        notify("已添加游戏目录 " + roots.back().label, false);
        refresh();
        return {};
    }

    void removeFolder(const std::string& key) {
        auto found = std::find_if(roots.begin(), roots.end(),
                                  [&](const Source& source) { return source.key == key && source.custom; });
        if (found == roots.end()) {
            return;
        }
        std::string label = found->label;
        flushSaves();
        roots.erase(found);
        if (arranged.erase(key) != 0) {
            saveOrder();
        }
        entries.erase(
                std::remove_if(entries.begin(), entries.end(), [&](const Entry& entry) { return entry.source == key; }),
                entries.end());
        saveFolders();
        notify("已移除游戏目录 " + label + "，文件夹里的文件不会被删除", false);
        refresh();
    }

    void editSettings(const std::string& key, const std::function<void(blb::instance::instance_settings&)>& edit) {
        Entry* entry = findMutable(key);
        const Source* from = entry != nullptr ? sourceMutable(entry->source) : nullptr;
        if (from == nullptr) {
            return;
        }
        edit(entry->info.settings);
        place(*entry, managerFor(from->root));
        pendingSaves[key] = Clock::now();
    }

    void rename(const std::string& key, const std::string& to, Done done) {
        std::string renamed = keyOf(sourceOf(key), to);
        operate(
                key, "正在重命名", "已重命名为 " + to, "重命名失败：",
                [to](const blb::instance::instance_manager& with, const std::string& id) {
                    return with.rename(id, to);
                },
                [key, renamed, to, done](const std::string& error) {
                    if (error.empty() && selection == key) {
                        select(renamed);
                    }
                    if (error.empty()) {
                        rearrange(key, [to](std::vector<std::string>& ids, size_t at) { ids[at] = to; });
                    }
                    if (done) {
                        done(error);
                    }
                });
    }

    void duplicate(const std::string& key, const std::string& to, Done done) {
        operate(
                key, "正在复制", "已复制为 " + to, "复制失败：",
                [to](const blb::instance::instance_manager& with, const std::string& id) {
                    return with.duplicate(id, to, true);
                },
                [key, to, done](const std::string& error) {
                    if (error.empty()) {
                        rearrange(key, [to](std::vector<std::string>& ids, size_t at) {
                            ids.insert(ids.begin() + (long long) at + 1, to);
                        });
                    }
                    if (done) {
                        done(error);
                    }
                });
    }

    void remove(const std::string& key, Done done) {
        pendingSaves.erase(key);
        const Entry* entry = findMutable(key);
        std::string name = entry != nullptr ? entry->info.id : key;
        operate(
                key, "正在删除", "已删除 " + name, "删除失败：",
                [](const blb::instance::instance_manager& with, const std::string& id) { return with.remove(id); },
                [key, done](const std::string& error) {
                    if (error.empty()) {
                        rearrange(key, [](std::vector<std::string>& ids, size_t at) {
                            ids.erase(ids.begin() + (long long) at);
                        });
                    }
                    if (done) {
                        done(error);
                    }
                });
    }

    std::string checkId(const std::string& source, const std::string& id) {
        if (id.empty()) {
            return "名称不能为空";
        }
        if (id.size() > 128) {
            return "名称太长";
        }
        if (id == "." || id == "..") {
            return "名称无效";
        }
        for (char c : id) {
            if ((unsigned char) c < 0x20 || std::strchr("/\\:*?\"<>|", c) != nullptr) {
                return "名称不能包含 / \\ : * ? \" < > |";
            }
        }
        if (id.back() == '.' || id.back() == ' ') {
            return "名称不能以点或空格结尾";
        }
        const Source* target = sourceMutable(source);
        std::error_code error;
        if (target != nullptr && std::filesystem::exists(blb::meta::game_layout(target->root).version_dir(id), error)) {
            return "已有同名实例";
        }
        return std::string();
    }

    std::string checkInstallId(const std::string& source, const std::string& id) {
        std::string problem = checkId(source, id);
        if (problem != "已有同名实例") {
            return problem;
        }
        const Source* target = sourceMutable(source);
        std::error_code error;
        bool real = target != nullptr &&
                std::filesystem::exists(blb::meta::game_layout(target->root).version_json(id), error);
        return real ? problem : std::string();
    }

    std::string freeId(const std::string& source, const std::string& base) {
        if (checkId(source, base).empty()) {
            return base;
        }
        for (int n = 2; n < 1000; n++) {
            std::string candidate = base + "-" + std::to_string(n);
            if (checkId(source, candidate).empty()) {
                return candidate;
            }
        }
        return base;
    }

    void notify(const std::string& text, bool error) {
        Notices::add(error ? Notices::Kind::Failed : Notices::Kind::Done, text, error ? 4000 : 2500);
    }
}

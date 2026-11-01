#include "app/instances/Instances.h"
#include "app/instances/Installer.h"
#include "app/system/Config.h"
#include "app/system/FolderWatch.h"
#include "app/system/Platform.h"
#include "app/system/Notices.h"

#include <core/base/hash.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/instance/loader_editor.hpp>

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
#include <numeric>
#include <thread>
#include <unordered_map>

namespace Instances {

    namespace {
        using Continuation = std::function<void()>;
        using Task = std::function<Continuation()>;
        using Failure = std::function<void(const std::string&)>;
        struct Job {
            Task work;
            Failure failed;
        };
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
        struct PendingSave {
            Clock::time_point attempted;
            std::filesystem::path root;
            std::string id;
            blb::instance::instance_settings settings;
            uint64_t revision = 0;
            bool failed = false;
        };
        std::unordered_map<std::string, PendingSave> pendingSaves;
        uint64_t settingsRevision = 0;
        int scansInFlight = 0;
        bool checkingRoots = false;
        Clock::time_point nextRootCheck;

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
            uint64_t revision = 0;
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
            if (folders == watch.folders() && !watch.needsRestart()) {
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

        Scan scan(const std::vector<Source>& from, blb::instance::isolation_mode isolation, uint64_t revision) {
            Scan out;
            out.revision = revision;
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

        void sortEntries() {
            std::unordered_map<std::string, size_t> ranks;
            for (size_t i = 0; i < roots.size(); i++) {
                ranks.emplace(roots[i].key, i);
            }
            std::unordered_map<std::string, std::unordered_map<std::string, size_t>> positions;
            for (const auto& [source, ids] : arranged) {
                std::unordered_map<std::string, size_t>& at = positions[source];
                for (size_t i = 0; i < ids.size(); i++) {
                    at.emplace(ids[i], i);
                }
            }
            std::vector<std::pair<size_t, size_t>> keys(entries.size());
            for (size_t i = 0; i < entries.size(); i++) {
                auto rank = ranks.find(entries[i].source);
                size_t position = SIZE_MAX;
                if (auto group = positions.find(entries[i].source); group != positions.end()) {
                    if (auto found = group->second.find(entries[i].info.id); found != group->second.end()) {
                        position = found->second;
                    }
                }
                keys[i] = {rank != ranks.end() ? rank->second : roots.size(), position};
            }
            std::vector<size_t> order(entries.size());
            std::iota(order.begin(), order.end(), (size_t) 0);
            std::stable_sort(order.begin(), order.end(), [&keys](size_t a, size_t b) { return keys[a] < keys[b]; });
            std::vector<Entry> sorted;
            sorted.reserve(entries.size());
            for (size_t i : order) {
                sorted.push_back(std::move(entries[i]));
            }
            for (size_t i = 0; i < sorted.size(); i++) {
                entries[i] = std::move(sorted[i]);
            }
        }

        void saveOrder() {
            Config::data()[ORDER] = arranged;
            if (!Config::save()) {
                notify("无法保存启动器配置", true);
            }
        }

        void preserveSettings(Entry& entry, const Entry* old, uint64_t revision) {
            if (auto pending = pendingSaves.find(entry.key); pending != pendingSaves.end()) {
                entry.info.settings = pending->second.settings;
                entry.settingsRevision = pending->second.revision;
                return;
            }
            if (old == nullptr) {
                return;
            }
            // A save removes the dirty marker, but does not make older reads
            // current. Retain edits made after this read was requested too.
            if (old->settingsRevision > revision) {
                entry.info.settings = old->info.settings;
            }
            entry.settingsRevision = old->settingsRevision;
        }

        void apply(Scan& result) {
            std::unordered_map<std::string, const Entry*> previous;
            for (const Entry& entry : entries) {
                previous.emplace(entry.key, &entry);
            }
            std::vector<Entry> next;
            for (Entry& entry : result.entries) {
                auto source = std::find_if(roots.begin(), roots.end(),
                        [&](const Source& source) { return source.key == entry.source; });
                if (source == roots.end()) {
                    continue;
                }
                auto old = previous.find(entry.key);
                preserveSettings(entry, old != previous.end() ? old->second : nullptr, result.revision);
                place(entry, managerFor(source->root));
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
                    next = job.work();
                } catch (const std::exception& error) {
                    std::string text = error.what();
                    next = [text, failed = std::move(job.failed)] {
                        if (failed) {
                            failed(text);
                        } else {
                            notify("操作失败：" + text, true);
                        }
                    };
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

        void submit(Task task, Failure failed = {}) {
            {
                std::lock_guard<std::mutex> guard(lock);
                jobs.push_back(Job{std::move(task), std::move(failed)});
            }
            wake.notify_one();
        }

        void scanFailed(const std::string& error) {
            --scansInFlight;
            scanned = true;
            notify("读取实例失败：" + error, true);
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

        bool writeSettings(const std::string& key, PendingSave& pending) {
            pending.attempted = Clock::now();
            Entry* entry = findMutable(key);
            // Keep the snapshot until a temporarily missing instance is read
            // again, so a scan cannot lose it between disk write and apply.
            if (entry == nullptr) return false;
            auto saved = managerFor(pending.root).save_settings(pending.id, pending.settings);
            if (!saved) {
                if (!pending.failed) {
                    notify("保存 " + pending.id + " 的设置失败，将自动重试：" + saved.err().message, true);
                }
                pending.failed = true;
            } else {
                // A deferred save invalidates reads requested while it failed,
                // even when the edit itself predates those reads.
                entry->settingsRevision = ++settingsRevision;
            }
            return bool(saved);
        }

        void flushSaves() {
            for (auto it = pendingSaves.begin(); it != pendingSaves.end();) {
                if (writeSettings(it->first, it->second)) {
                    it = pendingSaves.erase(it);
                } else {
                    ++it;
                }
            }
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
            uint64_t revision = 0;
        };

        Update reread(const std::vector<Source>& from, const std::map<std::string, std::set<std::string>>& ids,
                      blb::instance::isolation_mode isolation, uint64_t revision) {
            Update out;
            out.revision = revision;
            for (const Source& source : from) {
                auto wanted = ids.find(source.key);
                if (wanted == ids.end()) {
                    continue;
                }
                blb::instance::instance_manager with(blb::meta::game_layout(source.root), isolation);
                const auto folders = blb::fs::list_directory(with.layout().versions());
                std::set<std::string> read;
                for (const std::string& id : wanted->second) {
                    std::string actual = id;
                    if (!folders.contains(id)) {
                        // A case-only rename can leave both spellings in the watch events.
                        // Use the directory's spelling without folding distinct case-sensitive folders.
                        auto alias = std::find_if(folders.begin(), folders.end(), [&](const auto& folder) {
                            return folder.second.is_directory &&
                                   blb::fs::same_file(with.layout().version_dir(id),
                                                      with.layout().version_dir(folder.first));
                        });
                        if (alias != folders.end()) {
                            actual = alias->first;
                            out.gone.push_back(keyOf(source.key, id));
                        }
                    }
                    if (!read.insert(actual).second) continue;
                    auto info = with.get(actual);
                    if (!info) {
                        out.gone.push_back(keyOf(source.key, actual));
                        continue;
                    }
                    Entry entry;
                    entry.source = source.key;
                    entry.key = keyOf(source.key, actual);
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
                const Source* source = sourceMutable(entry.source);
                if (source == nullptr) {
                    continue;
                }
                Entry* old = findMutable(entry.key);
                preserveSettings(entry, old, update.revision);
                place(entry, managerFor(source->root));
                if (old == nullptr) {
                    entries.push_back(std::move(entry));
                    continue;
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
            if (pendingSaves.contains(key)) {
                std::string error = "实例设置尚未保存，请稍后重试";
                notify(error, true);
                if (done) done(error);
                return;
            }
            blb::instance::instance_manager with = managerFor(from->root);
            std::string id = entry->info.id;
            std::vector<Source> all = roots;
            blb::instance::isolation_mode isolation = mode;
            uint64_t revision = settingsRevision;
            running++;
            activityText = activity + " " + id;
            operating[key] = activity;
            std::string notice = Notices::live(Notices::Kind::Info, activityText);
            submit([key, with, id, all, isolation, revision, success, failure, notice, operation, done]() -> Continuation {
                blb::result<void> outcome = operation(with, id);
                std::string error = outcome ? std::string() : outcome.err().message;
                Scan fresh = scan(all, isolation, revision);
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
            }, [key, failure, notice, done](const std::string& error) {
                if (--running == 0) {
                    activityText.clear();
                }
                operating.erase(key);
                Notices::finish(notice, Notices::Kind::Failed, failure + error, 4000);
                if (done) {
                    done(error);
                }
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
            double delay = it->second.failed ? 2.0 : SAVE_DELAY;
            if (std::chrono::duration<double>(now - it->second.attempted).count() >= delay) {
                if (writeSettings(it->first, it->second)) {
                    it = pendingSaves.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it;
            }
        }
        // A missing root cannot notify us when it comes back. Probe only these
        // paths on the worker; read instance JSON only after a root reappears.
        if (scanned && scansInFlight == 0 && running == 0 && !Installer::running() &&
            !checkingRoots && now >= nextRootCheck) {
            nextRootCheck = now + std::chrono::seconds(2);
            std::vector<Source> missing;
            for (const Source& source : roots) {
                if (!source.exists) {
                    missing.push_back(source);
                }
            }
            if (!missing.empty()) {
                checkingRoots = true;
                submit([missing = std::move(missing)]() -> Continuation {
                    std::vector<Source> returned;
                    for (const Source& source : missing) {
                        std::error_code error;
                        if (std::filesystem::is_directory(source.root, error)) {
                            returned.push_back(source);
                        }
                    }
                    return [returned = std::move(returned)] {
                        checkingRoots = false;
                        for (const Source& source : returned) {
                            const Source* current = sourceMutable(source.key);
                            if (current != nullptr && current->root == source.root && !current->exists) {
                                refresh();
                                break;
                            }
                        }
                    };
                }, [](const std::string& error) {
                    checkingRoots = false;
                    notify("检查游戏目录失败：" + error, true);
                });
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
            uint64_t revision = settingsRevision;
            if (taken.everything) {
                scansInFlight++;
                submit([all, isolation, revision]() -> Continuation {
                    Scan fresh = scan(all, isolation, revision);
                    return [fresh]() mutable {
                        scansInFlight--;
                        if (running == 0 && !Installer::running()) {
                            announce(fresh.entries);
                        }
                        apply(fresh);
                    };
                }, scanFailed);
            } else if (!taken.versions.empty()) {
                // Only the instances the changes touch are read again.
                std::map<std::string, std::set<std::string>> ids = affected(taken.versions);
                scansInFlight++;
                submit([all, isolation, revision, ids]() -> Continuation {
                    Update update = reread(all, ids, isolation, revision);
                    return [update]() mutable {
                        scansInFlight--;
                        if (running == 0 && !Installer::running()) {
                            announce(update.read);
                        }
                        merge(update);
                    };
                }, scanFailed);
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
        std::unordered_map<std::string, const Entry*> byKey;
        for (const Entry& entry : entries) {
            byKey.emplace(entry.key, &entry);
        }
        std::vector<std::string> ids;
        for (const std::string& key : keys) {
            auto found = byKey.find(key);
            if (found != byKey.end() && found->second->source == source) {
                ids.push_back(found->second->info.id);
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
        uint64_t revision = settingsRevision;
        scansInFlight++;
        submit([all, isolation, revision, done]() -> Continuation {
            Scan fresh = scan(all, isolation, revision);
            return [fresh, done]() mutable {
                scansInFlight--;
                apply(fresh);
                if (done) {
                    done();
                }
            };
        }, [done](const std::string& error) {
            scanFailed(error);
            if (done) {
                done();
            }
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

    void readLoaders(const std::string& key,
                     std::function<void(blb::result<std::vector<blb::install::loader_info>>)> done) {
        const Entry* entry = find(key);
        const Source* from = entry != nullptr ? source(entry->source) : nullptr;
        if (from == nullptr) {
            done(blb::make_error(blb::errc::not_found, "找不到这个实例"));
            return;
        }
        flushSaves();
        if (pendingSaves.contains(key)) {
            done(blb::make_error(blb::errc::io, "实例设置尚未保存，请稍后重试"));
            return;
        }
        auto with = managerFor(from->root);
        std::string id = entry->info.id;
        submit([with, id, done]() -> Continuation {
            auto result = blb::instance::installed_loaders(with, id);
            return [result = std::move(result), done] { done(result); };
        }, [done](const std::string& message) { done(blb::make_error(blb::errc::io, message)); });
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
        if (std::any_of(pendingSaves.begin(), pendingSaves.end(), [&](const auto& item) {
                return item.second.root == found->root;
            })) {
            notify("这个目录还有未保存的实例设置，暂时不能移除", true);
            return;
        }
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
        entry->settingsRevision = ++settingsRevision;
        place(*entry, managerFor(from->root));
        bool failed = pendingSaves.contains(key) && pendingSaves.at(key).failed;
        pendingSaves[key] = PendingSave{Clock::now(), from->root, entry->info.id,
                                      entry->info.settings, entry->settingsRevision, failed};
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
        if (id.size() > 128) {
            return "名称太长";
        }
        switch (blb::fs::check_component(id)) {
            case blb::fs::component_error::empty: return "名称不能为空";
            case blb::fs::component_error::dot: return "名称无效";
            case blb::fs::component_error::invalid_character: return "名称不能包含 / \\ : * ? \" < > |";
            case blb::fs::component_error::trailing_dot_or_space: return "名称不能以点或空格结尾";
            case blb::fs::component_error::none: break;
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

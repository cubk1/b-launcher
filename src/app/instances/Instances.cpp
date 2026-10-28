#include "app/instances/Instances.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"

#include "ui/panel/Notifications.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Instances {

    namespace {
        using Continuation = std::function<void()>;
        using Job = std::function<Continuation()>;
        using Clock = std::chrono::steady_clock;

        constexpr double SAVE_DELAY = 0.5;

        std::thread worker;
        std::mutex lock;
        std::condition_variable wake;
        std::deque<Job> jobs;
        std::deque<Continuation> finished;
        bool stopping = false;

        std::vector<Source> roots;
        blb::instance::isolation_mode mode = blb::instance::isolation_mode::modded;
        std::vector<Entry> entries;
        std::string selection;
        bool scanned = false;
        int running = 0;
        std::string activityText;
        std::unordered_map<std::string, Clock::time_point> pendingSaves;

        struct Scan {
            std::vector<Entry> entries;
            std::vector<bool> exists;
        };

        blb::instance::instance_manager managerFor(const std::filesystem::path& root) {
            return blb::instance::instance_manager(blb::meta::game_layout(root), mode);
        }

        void place(Entry& entry, const blb::instance::instance_manager& with) {
            entry.isolated = with.is_isolated(entry.info);
            entry.gameDirectory = with.game_directory(entry.info);
        }

        Scan scan(const std::vector<Source>& from, blb::instance::isolation_mode isolation) {
            Scan out;
            for (const Source& source : from) {
                std::error_code error;
                bool exists = std::filesystem::is_directory(source.root, error);
                out.exists.push_back(exists);
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

        void apply(Scan& result) {
            entries = std::move(result.entries);
            for (size_t i = 0; i < roots.size() && i < result.exists.size(); i++) {
                roots[i].exists = result.exists[i];
            }
            scanned = true;
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
            std::string notice = Notifications::live(Notifications::INFO, activityText);
            submit([with, id, all, isolation, success, failure, notice, operation, done]() -> Continuation {
                blb::result<void> outcome = operation(with, id);
                std::string error = outcome ? std::string() : outcome.err().message;
                Scan fresh = scan(all, isolation);
                return [success, failure, notice, done, error, fresh]() mutable {
                    if (--running == 0) {
                        activityText.clear();
                    }
                    apply(fresh);
                    if (error.empty()) {
                        Notifications::finish(notice, Notifications::DONE, success, 2500);
                    } else {
                        Notifications::finish(notice, Notifications::FAILED, failure + error, 4000);
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
    }

    void shutdown() {
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
        submit([all, isolation, done]() -> Continuation {
            Scan fresh = scan(all, isolation);
            return [fresh, done]() mutable {
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
                [key, renamed, done](const std::string& error) {
                    if (error.empty() && selection == key) {
                        select(renamed);
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
                std::move(done));
    }

    void remove(const std::string& key, Done done) {
        pendingSaves.erase(key);
        const Entry* entry = findMutable(key);
        std::string name = entry != nullptr ? entry->info.id : key;
        operate(
                key, "正在删除", "已删除 " + name, "删除失败：",
                [](const blb::instance::instance_manager& with, const std::string& id) { return with.remove(id); },
                std::move(done));
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
        Notifications::add(error ? Notifications::FAILED : Notifications::DONE, text, error ? 4000 : 2500);
    }
}

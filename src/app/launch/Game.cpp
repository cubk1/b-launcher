#include "app/launch/Game.h"
#include "app/accounts/Accounts.h"
#include "app/launch/GameLog.h"
#include "app/instances/Instances.h"
#include "app/launch/LaunchDefaults.h"
#include "app/launch/Patches.h"
#include "app/system/Platform.h"
#include "app/system/Interface.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/launcher/launcher.hpp>
#include <core/network/mirror_provider.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace Game {

    namespace blb = b_launcher_base;

    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr std::chrono::milliseconds WATCH_INTERVAL{250};

        const char* const NODES[] = {"auth", "integrity", "java", "natives", "command"};
        const char* const LABELS[] = {"验证账户", "检查游戏文件", "准备 Java", "准备本地库", "运行启动前命令"};
        constexpr int NODE_COUNT = 5;

        struct Live {
            int id = 0;
            std::string key;
            std::string name;
            std::string accountKey;
            std::string accountName;
            int log = 0;
            State state = State::Preparing;
            std::thread preparer;
            std::shared_ptr<blb::cancellation_source> cancelling;
            std::atomic<float> fraction{-1.0f};

            std::mutex lock;
            bool prepared = false;
            std::optional<blb::process> started;
            std::string failure;
            bool cancelled = false;
            std::string javaUsed;
            std::optional<blb::auth::account> refreshedAccount;
            bool accountExpired = false;

            std::mutex stepLock;
            std::vector<Step> steps;

            blb::process game;
            bool killed = false;
            Clock::time_point nextWatch;
            Clock::time_point since;

            std::string visibility;
            std::string postExit;
            Platform::Environment variables;
            std::filesystem::path commandDirectory;
        };

        std::vector<std::unique_ptr<Live>> lives;
        std::map<std::string, int> lastLog;
        int nextId = 1;
        bool hidden = false;
        bool quiet = false;

        void resetSteps(Live& live, bool command) {
            std::lock_guard<std::mutex> guard(live.stepLock);
            live.steps.clear();
            for (int i = 0; i < NODE_COUNT - (command ? 0 : 1); i++) {
                live.steps.push_back(Step{LABELS[i], Step::Status::Waiting, std::string()});
            }
        }

        int priorityLevel(const std::string& priority) {
            if (priority == "low") {
                return 0;
            }
            if (priority == "below-normal") {
                return 1;
            }
            if (priority == "above-normal") {
                return 3;
            }
            if (priority == "high") {
                return 4;
            }
            return 2;
        }

        Platform::Environment instanceVariables(const Instances::Entry& entry) {
            Platform::Environment out = {
                    {"INST_NAME", entry.info.id},
                    {"INST_ID", entry.info.id},
                    {"INST_DIR", Platform::display(entry.info.directory)},
                    {"INST_MC_DIR", Platform::display(entry.gameDirectory)},
            };
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                const char* name = nullptr;
                switch (loader.type) {
                    case blb::install::loader_type::forge: name = "INST_FORGE"; break;
                    case blb::install::loader_type::neoforge: name = "INST_NEOFORGE"; break;
                    case blb::install::loader_type::cleanroom: name = "INST_CLEANROOM"; break;
                    case blb::install::loader_type::liteloader: name = "INST_LITELOADER"; break;
                    case blb::install::loader_type::optifine: name = "INST_OPTIFINE"; break;
                    case blb::install::loader_type::fabric:
                    case blb::install::loader_type::legacy_fabric: name = "INST_FABRIC"; break;
                    case blb::install::loader_type::quilt: name = "INST_QUILT"; break;
                    default: break;
                }
                if (name != nullptr && std::none_of(out.begin(), out.end(), [name](const auto& variable) {
                        return variable.first == name;
                    })) {
                    out.emplace_back(name, "1");
                }
            }
            return out;
        }

        int stepOfTask(const std::string& task) {
            size_t colon = task.find(':');
            std::string what = colon == std::string::npos ? task : task.substr(colon + 1);
            if (what.rfind("java", 0) == 0) {
                return 2;
            }
            if (what == "natives") {
                return 3;
            }
            return 1;
        }

        std::string megabytes(uint64_t bytes) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", bytes / (1024.0 * 1024.0));
            return text;
        }

        class Progress final : public blb::progress_sink {
        public:
            explicit Progress(Live& live) : live(live) {}

            void on_stage(std::string_view task, std::string_view stage) override {
                std::lock_guard<std::mutex> guard(live.stepLock);
                for (int i = 0; i < NODE_COUNT && i < (int) live.steps.size(); i++) {
                    if (task != NODES[i]) {
                        continue;
                    }
                    Step& step = live.steps[i];
                    step.status = stage == "start" ? Step::Status::Running
                            : stage == "done"      ? Step::Status::Done
                                                   : Step::Status::Failed;
                    if (step.status != Step::Status::Running) {
                        step.detail.clear();
                    }
                }
            }

            void on_progress(const blb::progress_event& event) override {
                std::string task(event.task);
                bool download = task.rfind("download:", 0) == 0;
                std::string detail;
                if (!event.finished) {
                    if (download && event.total_bytes > 0) {
                        detail = "下载 " + megabytes(event.done_bytes) + "/" + megabytes(event.total_bytes) + " MB";
                        if (event.bytes_per_second > 0) {
                            detail += "  " + megabytes((uint64_t) event.bytes_per_second) + " MB/s";
                        }
                    } else if (event.total_items > 0) {
                        detail = (download ? "下载 " : "校验 ") + std::to_string(event.done_items) + "/" +
                                std::to_string(event.total_items);
                    }
                }
                {
                    std::lock_guard<std::mutex> guard(live.stepLock);
                    int index = stepOfTask(task);
                    if (index < (int) live.steps.size() && live.steps[index].status != Step::Status::Done) {
                        live.steps[index].detail = detail;
                    }
                }
                if (!download) {
                    return;
                }
                std::lock_guard<std::mutex> guard(tasks);
                if (event.total_bytes > 0 && !event.finished) {
                    sizes[task] = {event.done_bytes, event.total_bytes};
                } else {
                    sizes.erase(task);
                }
                uint64_t done = 0;
                uint64_t total = 0;
                for (const auto& size : sizes) {
                    done += size.second.first;
                    total += size.second.second;
                }
                live.fraction.store(total > 0 ? (float) ((double) std::min(done, total) / (double) total) : -1.0f);
            }

        private:
            Live& live;
            std::mutex tasks;
            std::map<std::string, std::pair<uint64_t, uint64_t>> sizes;
        };

        void join(Live& live) {
            if (live.preparer.joinable()) {
                live.preparer.join();
            }
        }

        void takeAccount(Live& live) {
            std::optional<blb::auth::account> fresh;
            bool expired;
            {
                std::lock_guard<std::mutex> guard(live.lock);
                fresh = std::move(live.refreshedAccount);
                live.refreshedAccount.reset();
                expired = live.accountExpired;
                live.accountExpired = false;
            }
            if (fresh) {
                Accounts::refreshed(*fresh);
            }
            if (expired) {
                Accounts::markExpired(live.accountKey);
            }
        }

        void finishPreparing(Live& live) {
            join(live);
            std::optional<blb::process> game;
            std::string error;
            bool wasCancelled;
            {
                std::lock_guard<std::mutex> guard(live.lock);
                game = std::move(live.started);
                live.started.reset();
                error = live.failure;
                wasCancelled = live.cancelled;
                live.prepared = false;
                if (!live.javaUsed.empty()) {
                    live.variables.emplace_back("INST_JAVA", live.javaUsed);
                }
            }
            takeAccount(live);
            if (game && game->valid()) {
                live.game = std::move(*game);
                live.killed = false;
                live.state = State::Running;
                live.since = Clock::now();
                live.nextWatch = Clock::now() + WATCH_INTERVAL;
                Notices::add(Notices::Kind::Done, "已启动 " + live.name, 2500);
                if (quiet) {
                    return;
                }
                if (live.visibility == "minimize") {
                    Platform::minimizeWindow();
                } else if (live.visibility == "hide") {
                    Platform::hideWindow();
                    hidden = true;
                } else if (live.visibility == "close") {
                    Platform::closeWindow();
                }
                return;
            }
            live.state = State::Idle;
            if (wasCancelled) {
                GameLog::note(live.log, "已取消启动");
                Notices::add(Notices::Kind::Info, "已取消启动", 2000);
            } else {
                std::string reason = error.empty() ? std::string("游戏没有启动") : error;
                GameLog::note(live.log, "启动失败：" + reason);
                Notices::add(Notices::Kind::Failed, "启动失败：" + reason, 5000);
            }
        }

        void watch(Live& live) {
            if (Clock::now() < live.nextWatch) {
                return;
            }
            live.nextWatch = Clock::now() + WATCH_INTERVAL;
            if (live.game.running()) {
                return;
            }
            auto exit = live.game.wait(std::chrono::milliseconds(0));
            int code = exit ? *exit : 0;
            live.state = State::Idle;
            live.game = blb::process();
            if (!live.postExit.empty()) {
                std::string problem = Platform::startCommand(live.postExit, live.variables, live.commandDirectory);
                if (!problem.empty()) {
                    Notices::add(Notices::Kind::Failed, "退出后命令：" + problem, 5000);
                }
            }
            GameLog::note(live.log,
                          live.killed ? std::string("游戏已被结束") : "游戏已退出，退出代码 " + std::to_string(code));
            if (live.killed) {
                Notices::add(Notices::Kind::Info, "已结束 " + live.name, 2000);
            } else if (code != 0) {
                Notices::add(Notices::Kind::Failed, live.name + " 异常退出（代码 " + std::to_string(code) + "）", 5000);
                GameLog::show(live.log);
                Interface::showLog();
            }
        }

        void kill(Live& live) {
            if (live.state != State::Running) {
                return;
            }
            live.killed = true;
            auto result = live.game.kill();
            if (!result) {
                live.killed = false;
                Notices::add(Notices::Kind::Failed, "无法结束游戏：" + result.err().message, 4000);
            }
            live.nextWatch = Clock::now();
        }

        Session snapshot(const Live& live) {
            Session out;
            out.id = live.id;
            out.instance = live.key;
            out.name = live.name;
            out.account = live.accountName;
            out.accountKey = live.accountKey;
            out.state = live.state;
            out.log = live.log;
            if (live.state == State::Running) {
                out.uptime = std::chrono::duration<double>(Clock::now() - live.since).count();
            }
            return out;
        }

        Live* preparingOf(const std::string& key) {
            for (const std::unique_ptr<Live>& live : lives) {
                if (live->key == key && live->state == State::Preparing) {
                    return live.get();
                }
            }
            return nullptr;
        }

        int logFor(const std::string& key) {
            auto found = lastLog.find(key);
            if (found == lastLog.end()) {
                return 0;
            }
            for (const std::unique_ptr<Live>& live : lives) {
                if (live->log == found->second) {
                    return 0;
                }
            }
            return found->second;
        }
    }

    State state(const std::string& key) {
        State out = State::Idle;
        for (const std::unique_ptr<Live>& live : lives) {
            if (live->key != key) {
                continue;
            }
            if (live->state == State::Preparing) {
                return State::Preparing;
            }
            if (live->state == State::Running) {
                out = State::Running;
            }
        }
        return out;
    }

    float progress(const std::string& key) {
        Live* live = preparingOf(key);
        return live != nullptr ? live->fraction.load() : -1.0f;
    }

    std::vector<Step> steps(const std::string& key) {
        Live* live = preparingOf(key);
        if (live == nullptr) {
            return {};
        }
        std::lock_guard<std::mutex> guard(live->stepLock);
        return live->steps;
    }

    std::vector<Session> sessions() {
        std::vector<Session> out;
        for (const std::unique_ptr<Live>& live : lives) {
            if (live->state != State::Idle) {
                out.push_back(snapshot(*live));
            }
        }
        return out;
    }

    std::vector<Session> sessionsOf(const std::string& key) {
        std::vector<Session> out;
        for (const std::unique_ptr<Live>& live : lives) {
            if (live->key == key && live->state != State::Idle) {
                out.push_back(snapshot(*live));
            }
        }
        return out;
    }

    bool preparing() {
        return std::any_of(lives.begin(), lives.end(),
                           [](const std::unique_ptr<Live>& live) { return live->state == State::Preparing; });
    }

    bool running() {
        return std::any_of(lives.begin(), lives.end(),
                           [](const std::unique_ptr<Live>& live) { return live->state == State::Running; });
    }

    bool active() {
        return std::any_of(lives.begin(), lives.end(),
                           [](const std::unique_ptr<Live>& live) { return live->state != State::Idle; });
    }

    bool inUse(const std::string& accountKey) {
        return std::any_of(lives.begin(), lives.end(), [&](const std::unique_ptr<Live>& live) {
            return live->state != State::Idle && live->accountKey == accountKey;
        });
    }

    std::string accountOf(const std::string& key) {
        const Instances::Entry* entry = Instances::find(key);
        if (entry == nullptr) {
            return std::string();
        }
        const blb::instance::instance_settings& settings = entry->info.settings;
        const blb::auth::account* account = Accounts::pick(settings.login, settings.auth_server, settings.account);
        return account != nullptr ? Accounts::keyOf(*account) : std::string();
    }

    void setAccount(const std::string& key, const std::string& accountKey) {
        const Instances::Entry* entry = Instances::find(key);
        if (entry == nullptr || entry->info.settings.account == accountKey) {
            return;
        }
        Instances::editSettings(
                key, [accountKey](blb::instance::instance_settings& settings) { settings.account = accountKey; });
    }

    int launch(const std::string& target, bool test, const std::string& who) {
        const Instances::Entry* entry = Instances::find(target);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (source == nullptr || preparingOf(target) != nullptr) {
            return 0;
        }
        if (!entry->info.valid) {
            Instances::notify("无法启动 " + entry->info.id + "：" + entry->info.problem, true);
            return 0;
        }
        const std::string& login = entry->info.settings.login;
        const std::string& authServer = entry->info.settings.auth_server;
        const blb::auth::account* account = who.empty() ? nullptr : Accounts::find(who);
        if (account == nullptr || !Accounts::suits(*account, login, authServer)) {
            account = Accounts::pick(login, authServer, entry->info.settings.account);
        }
        if (account == nullptr) {
            Instances::notify(Accounts::requirement(login, authServer), true);
            Interface::needAccount(login, authServer);
            return 0;
        }
        const blb::auth::account* chosen = Accounts::current();
        bool substituted = chosen != nullptr && Accounts::keyOf(*chosen) != Accounts::keyOf(*account);
        size_t alongside = sessionsOf(target).size();
        bool shared = inUse(Accounts::keyOf(*account));
        blb::instance::instance_manager manager(blb::meta::game_layout(source->root), Instances::isolation());
        blb::launch::launch_settings settings = manager.launch_settings_for(entry->info);
        std::filesystem::path root = source->root;
        const blb::instance::instance_settings& options = entry->info.settings;
        std::vector<std::string> preLaunch;
        for (const std::string& command : {LaunchDefaults::preLaunch(), options.pre_launch}) {
            if (!command.empty()) {
                preLaunch.push_back(command);
            }
        }
        std::vector<std::string> jvm = LaunchDefaults::split(LaunchDefaults::jvmArgs());
        std::filesystem::path patchJar;
        Patches::Plan patchPlan;
        if (Patches::enabled(options.patch)) {
            patchJar = Patches::jarIn(blb::meta::game_layout(root).cache());
            patchPlan = Patches::plan();
        }
        jvm.insert(jvm.end(), settings.jvm_args.begin(), settings.jvm_args.end());
        settings.jvm_args = jvm;
        std::vector<std::string> game = LaunchDefaults::split(LaunchDefaults::gameArgs());
        game.insert(game.end(), settings.game_args.begin(), settings.game_args.end());
        settings.game_args = game;
        settings.gc = LaunchDefaults::gc();
        LaunchDefaults::Language language = LaunchDefaults::language();
        std::string minecraft = entry->info.minecraft_version;
        int priority = priorityLevel(options.process_priority);
        auto made = std::make_unique<Live>();
        Live& live = *made;
        live.id = nextId++;
        live.key = target;
        live.name = entry->info.id;
        live.accountKey = Accounts::keyOf(*account);
        live.accountName = account->name;
        live.visibility = options.launcher_visibility;
        live.postExit = options.post_exit;
        live.variables = instanceVariables(*entry);
        live.commandDirectory = entry->gameDirectory;
        settings.environment.insert(settings.environment.begin(), live.variables.begin(), live.variables.end());

        int log = GameLog::begin(live.name + "（" + account->name + "）", logFor(target));
        live.log = log;
        lastLog[target] = log;
        GameLog::note(log,
                      "正在启动 " + live.name + "（Minecraft " + entry->info.minecraft_version + "），账户 " +
                              account->name + "（" + Accounts::kindOf(*account) + "）");
        if (!patchJar.empty()) {
            GameLog::note(log, "已启用启动器补丁");
        }
        if (substituted && entry->info.settings.account.empty()) {
            GameLog::note(log, "这个实例指定了登录方式，没有使用当前账户");
        }
        if (alongside > 0) {
            GameLog::note(log, "这个实例已有 " + std::to_string(alongside) + " 个在运行");
            Notices::add(Notices::Kind::Info, live.name + " 已在运行", 4000);
        }
        const std::optional<bool>& showLogs = entry->info.settings.show_logs;
        if (test || (!quiet && (showLogs ? *showLogs : LaunchDefaults::showLogs()))) {
            Interface::showLog();
        }
        resetSteps(live, !preLaunch.empty());
        live.cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = live.cancelling->token();
        blb::auth::account player = *account;
        std::shared_ptr<blb::auth::microsoft_authenticator> authenticator = Accounts::authenticator(player);
        Platform::Environment commandVariables = live.variables;
        std::filesystem::path directory = live.commandDirectory;
        Live* self = &live;
        lives.push_back(std::move(made));
        self->preparer = std::thread([self, log, root, settings, player, authenticator, token, preLaunch, priority,
                                      commandVariables, directory, language, minecraft, patchJar, patchPlan]() mutable {
            std::optional<blb::process> game_;
            std::string error;
            bool wasCancelled = false;
            try {
                if (!patchJar.empty()) {
                    std::string problem = Patches::extract(patchJar);
                    if (!problem.empty()) {
                        throw std::runtime_error("无法准备启动器补丁：" + problem);
                    }
                    std::string warning = Patches::resolve(patchPlan, token);
                    if (!warning.empty()) {
                        GameLog::note(log, warning);
                    }
                    std::vector<std::string> agent = Patches::arguments(patchJar, patchPlan);
                    settings.jvm_args.insert(settings.jvm_args.begin(), agent.begin(), agent.end());
                }
                blb::install::install_options options;
                options.mirror = blb::net::make_bmclapi_provider();
                auto installer = std::make_shared<blb::install::game_installer>(blb::meta::game_layout(root), options);
                blb::launch::game_launcher launcher(installer);
                if (authenticator) {
                    launcher.set_authenticator(authenticator);
                }
                Progress sink(*self);
                blb::launch::launch_callbacks callbacks;
                callbacks.progress = &sink;
                callbacks.on_output = [log](std::string_view text, blb::output_stream stream) {
                    GameLog::feed(log, text, stream == blb::output_stream::err);
                };
                callbacks.on_account_refreshed = [self](const blb::auth::account& account) {
                    std::lock_guard<std::mutex> guard(self->lock);
                    self->refreshedAccount = account;
                };
                auto ready = launcher.prepare(settings, player, callbacks, token);
                if (!ready) {
                    wasCancelled = ready.err().code == blb::errc::cancelled || token.is_cancelled();
                    error = Accounts::describe(ready.err());
                    if (ready.err().code == blb::errc::auth_expired) {
                        std::lock_guard<std::mutex> guard(self->lock);
                        self->accountExpired = true;
                    }
                } else {
                    std::string java = blb::path_to_utf8(ready->java.executable);
                    GameLog::note(log, "Java：" + java + "（" + ready->java.version + "）");
                    ready->command.environment.insert(ready->command.environment.begin(), {"INST_JAVA", java});
                    {
                        std::lock_guard<std::mutex> guard(self->lock);
                        self->javaUsed = java;
                    }
                    LaunchDefaults::applyLanguage(language, directory, minecraft);
                    if (!preLaunch.empty()) {
                        commandVariables.emplace_back("INST_JAVA", java);
                        sink.on_stage("command", "start");
                        for (const std::string& command : preLaunch) {
                            error = Platform::runCommand(command, commandVariables, directory,
                                                         [token] { return token.is_cancelled(); });
                            if (!error.empty() || token.is_cancelled()) {
                                break;
                            }
                        }
                        sink.on_stage("command", error.empty() && !token.is_cancelled() ? "done" : "failed");
                        if (!error.empty()) {
                            error = "启动前命令：" + error;
                        }
                    }
                    if (token.is_cancelled()) {
                        wasCancelled = true;
                    } else if (error.empty()) {
                        auto result = launcher.start(*ready, callbacks);
                        if (result) {
                            game_ = std::move(*result);
                            if (priority != 2) {
                                Platform::setPriority(game_->pid(), priority);
                            }
                        } else {
                            error = result.err().message;
                        }
                    }
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            std::lock_guard<std::mutex> guard(self->lock);
            self->started = std::move(game_);
            self->failure = error;
            self->cancelled = wasCancelled;
            self->prepared = true;
        });
        return self->id;
    }

    void setQuiet(bool on) {
        quiet = on;
    }

    void cancel(const std::string& key) {
        if (Live* live = preparingOf(key); live != nullptr && live->cancelling) {
            live->cancelling->cancel();
        }
    }

    void stop(const std::string& key) {
        for (const std::unique_ptr<Live>& live : lives) {
            if (live->key == key) {
                kill(*live);
            }
        }
    }

    void stop(int session) {
        for (const std::unique_ptr<Live>& live : lives) {
            if (live->id == session) {
                kill(*live);
            }
        }
    }

    void poll() {
        for (size_t i = 0; i < lives.size(); i++) {
            Live& live = *lives[i];
            if (live.state == State::Preparing) {
                bool ready;
                {
                    std::lock_guard<std::mutex> guard(live.lock);
                    ready = live.prepared;
                }
                if (ready) {
                    finishPreparing(live);
                }
            } else if (live.state == State::Running) {
                watch(live);
            }
        }
        std::erase_if(lives, [](const std::unique_ptr<Live>& live) { return live->state == State::Idle; });
        if (hidden && !running()) {
            hidden = false;
            Platform::showWindow();
        }
    }

    void shutdown() {
        for (const std::unique_ptr<Live>& live : lives) {
            if (live->state == State::Preparing && live->cancelling) {
                live->cancelling->cancel();
            }
        }
        for (const std::unique_ptr<Live>& live : lives) {
            join(*live);
            takeAccount(*live);
            if (live->state == State::Running) {
                live->game.detach();
            }
        }
    }
}

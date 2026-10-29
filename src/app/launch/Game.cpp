#include "app/launch/Game.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/AccountsScreen.h"
#include "app/launch/GameLog.h"
#include "app/instances/Instances.h"
#include "app/launch/LaunchDefaults.h"
#include "app/launch/LogWindow.h"
#include "app/shell/MainWindow.h"
#include "app/launch/Patches.h"
#include "app/system/Platform.h"
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

        State current = State::Idle;
        std::string key;
        std::string name;
        std::thread preparer;
        std::shared_ptr<blb::cancellation_source> cancelling;
        std::atomic<float> fraction{-1.0f};

        std::mutex lock;
        bool prepared = false;
        std::optional<blb::process> started;
        std::string failure;
        bool cancelled = false;

        std::string javaUsed;
        std::string accountKey;
        std::optional<blb::auth::account> refreshedAccount;
        bool accountExpired = false;

        blb::process game;
        bool killed = false;
        Clock::time_point nextWatch;

        std::string visibility;
        std::string postExit;
        Platform::Environment variables;
        std::filesystem::path commandDirectory;
        bool hidden = false;
        bool quiet = false;

        const char* const NODES[] = {"auth", "integrity", "java", "natives", "command"};
        const char* const LABELS[] = {"验证账户", "检查游戏文件", "准备 Java", "准备本地库", "运行启动前命令"};
        constexpr int NODE_COUNT = 5;

        std::mutex stepLock;
        std::vector<Step> stepList;

        void resetSteps(bool command) {
            std::lock_guard<std::mutex> guard(stepLock);
            stepList.clear();
            for (int i = 0; i < NODE_COUNT - (command ? 0 : 1); i++) {
                stepList.push_back(Step{LABELS[i], Step::Status::Waiting, std::string()});
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
            void on_stage(std::string_view task, std::string_view stage) override {
                std::lock_guard<std::mutex> guard(stepLock);
                for (int i = 0; i < NODE_COUNT && i < (int) stepList.size(); i++) {
                    if (task != NODES[i]) {
                        continue;
                    }
                    Step& step = stepList[i];
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
                    std::lock_guard<std::mutex> guard(stepLock);
                    int index = stepOfTask(task);
                    if (index < (int) stepList.size() && stepList[index].status != Step::Status::Done) {
                        stepList[index].detail = detail;
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
                fraction.store(total > 0 ? (float) ((double) std::min(done, total) / (double) total) : -1.0f);
            }

        private:
            std::mutex tasks;
            std::map<std::string, std::pair<uint64_t, uint64_t>> sizes;
        };

        void join() {
            if (preparer.joinable()) {
                preparer.join();
            }
        }

        void finishPreparing() {
            join();
            std::optional<blb::process> game_;
            std::string error;
            bool wasCancelled;
            std::optional<blb::auth::account> fresh;
            bool expired;
            {
                std::lock_guard<std::mutex> guard(lock);
                game_ = std::move(started);
                started.reset();
                error = failure;
                wasCancelled = cancelled;
                prepared = false;
                if (!javaUsed.empty()) {
                    variables.emplace_back("INST_JAVA", javaUsed);
                }
                fresh = std::move(refreshedAccount);
                refreshedAccount.reset();
                expired = accountExpired;
            }
            if (fresh) {
                Accounts::refreshed(*fresh);
            }
            if (expired) {
                Accounts::markExpired(accountKey);
            }
            if (game_ && game_->valid()) {
                game = std::move(*game_);
                killed = false;
                current = State::Running;
                nextWatch = Clock::now() + WATCH_INTERVAL;
                Notices::add(Notices::Kind::Done, "已启动 " + name, 2500);
                if (quiet) {
                    return;
                }
                if (visibility == "minimize") {
                    Platform::minimizeWindow();
                } else if (visibility == "hide") {
                    Platform::hideWindow();
                    hidden = true;
                } else if (visibility == "close") {
                    Platform::closeWindow();
                }
                return;
            }
            current = State::Idle;
            key.clear();
            if (wasCancelled) {
                GameLog::note("已取消启动");
                Notices::add(Notices::Kind::Info, "已取消启动", 2000);
            } else {
                GameLog::note("启动失败：" + (error.empty() ? std::string("游戏没有启动") : error));
                Notices::add(Notices::Kind::Failed,
                             "启动失败：" + (error.empty() ? std::string("游戏没有启动") : error), 5000);
            }
        }

        void watchGame() {
            if (Clock::now() < nextWatch) {
                return;
            }
            nextWatch = Clock::now() + WATCH_INTERVAL;
            if (game.running()) {
                return;
            }
            auto exit = game.wait(std::chrono::milliseconds(0));
            int code = exit ? *exit : 0;
            current = State::Idle;
            game = blb::process();
            if (hidden) {
                hidden = false;
                Platform::showWindow();
            }
            if (!postExit.empty()) {
                std::string problem = Platform::startCommand(postExit, variables, commandDirectory);
                if (!problem.empty()) {
                    Notices::add(Notices::Kind::Failed, "退出后命令：" + problem, 5000);
                }
            }
            GameLog::note(killed ? std::string("游戏已被结束") : "游戏已退出，退出代码 " + std::to_string(code));
            if (killed) {
                Notices::add(Notices::Kind::Info, "已结束 " + name, 2000);
            } else if (code != 0) {
                Notices::add(Notices::Kind::Failed, name + " 异常退出（代码 " + std::to_string(code) + "）", 5000);
                LogWindow::open();
            }
            key.clear();
        }
    }

    State state() {
        return current;
    }

    float progress() {
        return current == State::Preparing ? fraction.load() : -1.0f;
    }

    const std::string& instance() {
        return key;
    }

    std::vector<Step> steps() {
        if (current != State::Preparing) {
            return {};
        }
        std::lock_guard<std::mutex> guard(stepLock);
        return stepList;
    }

    void launch(const std::string& target, bool test) {
        if (current != State::Idle) {
            return;
        }
        const Instances::Entry* entry = Instances::find(target);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (source == nullptr) {
            return;
        }
        if (!entry->info.valid) {
            Instances::notify("无法启动 " + entry->info.id + "：" + entry->info.problem, true);
            return;
        }
        const std::string& login = entry->info.settings.login;
        const std::string& authServer = entry->info.settings.auth_server;
        const blb::auth::account* account = Accounts::pick(login, authServer);
        if (account == nullptr) {
            Instances::notify(Accounts::requirement(login, authServer), true);
            MainWindow::showAccounts();
            if (login == "yggdrasil") {
                AccountsScreen::openServerLogin(blb::auth::account_type::yggdrasil, authServer, std::string());
            } else if (login == "nide8") {
                AccountsScreen::openServerLogin(blb::auth::account_type::nide8, authServer, std::string());
            }
            return;
        }
        const blb::auth::account* chosen = Accounts::current();
        bool substituted = chosen != nullptr && Accounts::keyOf(*chosen) != Accounts::keyOf(*account);
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
        visibility = options.launcher_visibility;
        postExit = options.post_exit;
        variables = instanceVariables(*entry);
        commandDirectory = entry->gameDirectory;
        settings.environment.insert(settings.environment.begin(), variables.begin(), variables.end());

        join();
        key = target;
        name = entry->info.id;
        GameLog::begin(name);
        GameLog::note("正在启动 " + name + "（Minecraft " + entry->info.minecraft_version + "）");
        if (!patchJar.empty()) {
            GameLog::note("已启用启动器补丁");
        }
        if (substituted) {
            GameLog::note("这个实例指定了登录方式，使用账户 " + account->name + "（" + Accounts::kindOf(*account) +
                          "）");
        }
        const std::optional<bool>& showLogs = entry->info.settings.show_logs;
        if (test || (!quiet && (showLogs ? *showLogs : LaunchDefaults::showLogs()))) {
            LogWindow::open();
        }
        current = State::Preparing;
        fraction.store(-1.0f);
        resetSteps(!preLaunch.empty());
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        {
            std::lock_guard<std::mutex> guard(lock);
            prepared = false;
            started.reset();
            failure.clear();
            cancelled = false;
            javaUsed.clear();
            refreshedAccount.reset();
            accountExpired = false;
        }
        blb::auth::account player = *account;
        accountKey = Accounts::keyOf(player);
        std::shared_ptr<blb::auth::microsoft_authenticator> authenticator = Accounts::authenticator(player);
        Platform::Environment commandVariables = variables;
        std::filesystem::path directory = commandDirectory;
        preparer = std::thread([root, settings, player, authenticator, token, preLaunch, priority, commandVariables,
                                directory, language, minecraft, patchJar, patchPlan]() mutable {
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
                        GameLog::note(warning);
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
                Progress sink;
                blb::launch::launch_callbacks callbacks;
                callbacks.progress = &sink;
                callbacks.on_output = [](std::string_view text, blb::output_stream stream) {
                    GameLog::feed(text, stream == blb::output_stream::err);
                };
                callbacks.on_account_refreshed = [](const blb::auth::account& account) {
                    std::lock_guard<std::mutex> guard(lock);
                    refreshedAccount = account;
                };
                auto ready = launcher.prepare(settings, player, callbacks, token);
                if (!ready) {
                    wasCancelled = ready.err().code == blb::errc::cancelled || token.is_cancelled();
                    error = Accounts::describe(ready.err());
                    if (ready.err().code == blb::errc::auth_expired) {
                        std::lock_guard<std::mutex> guard(lock);
                        accountExpired = true;
                    }
                } else {
                    std::string java = blb::path_to_utf8(ready->java.executable);
                    GameLog::note("Java：" + java + "（" + ready->java.version + "）");
                    ready->command.environment.insert(ready->command.environment.begin(), {"INST_JAVA", java});
                    {
                        std::lock_guard<std::mutex> guard(lock);
                        javaUsed = java;
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
            std::lock_guard<std::mutex> guard(lock);
            started = std::move(game_);
            failure = error;
            cancelled = wasCancelled;
            prepared = true;
        });
    }

    void setQuiet(bool on) {
        quiet = on;
    }

    void cancel() {
        if (current == State::Preparing && cancelling) {
            cancelling->cancel();
        }
    }

    void stop() {
        if (current != State::Running) {
            return;
        }
        killed = true;
        auto result = game.kill();
        if (!result) {
            killed = false;
            Notices::add(Notices::Kind::Failed, "无法结束游戏：" + result.err().message, 4000);
        }
        nextWatch = Clock::now();
    }

    void poll() {
        if (current == State::Preparing) {
            bool ready;
            {
                std::lock_guard<std::mutex> guard(lock);
                ready = prepared;
            }
            if (ready) {
                finishPreparing();
            }
        } else if (current == State::Running) {
            watchGame();
        }
    }

    void shutdown() {
        if (current == State::Preparing && cancelling) {
            cancelling->cancel();
        }
        join();
        std::optional<blb::auth::account> fresh;
        {
            std::lock_guard<std::mutex> guard(lock);
            fresh = std::move(refreshedAccount);
            refreshedAccount.reset();
        }
        if (fresh) {
            Accounts::refreshed(*fresh);
        }
        if (current == State::Running) {
            game.detach();
        }
    }
}

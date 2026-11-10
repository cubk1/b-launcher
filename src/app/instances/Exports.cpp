#include "app/instances/Exports.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/DownloadSources.h"
#include "app/system/AsyncTasks.h"
#include "app/accounts/Accounts.h"
#include "app/instances/Instances.h"
#include "app/system/Config.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>

#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace Exports {

    namespace blb = b_launcher_base;

    namespace {
        std::map<std::string, Options> remembered;
        FileTree::Tree files;

        AsyncTasks tasks;
        std::shared_ptr<blb::cancellation_source> cancelling;
        bool busy = false;
        std::string busyKey;
        std::mutex progressLock;
        std::string progress;

        std::string fileName(const Options& options) {
            std::string base = options.name.empty() ? std::string("modpack") : options.name;
            if (!options.version.empty()) {
                base += "-" + options.version;
            }
            return blb::fs::filter_filename(base) + ".mrpack";
        }

        struct Finished {
            std::filesystem::path file;
            std::string error;
            std::vector<std::string> notes;
            bool cancelled = false;
        };

        FileTree::Rules migrate(const std::filesystem::path& root, const Options& o) {
            FileTree::Rules rules;
            std::error_code ec;
            auto pick = [&](const char* name) {
                if (std::filesystem::exists(root / name, ec)) {
                    rules[name] = true;
                }
            };
            if (o.game_settings) {
                for (const char* name :
                     {"options.txt", "optionsof.txt", "optionsshaders.txt", "optionsviveprofiles.txt"}) {
                    pick(name);
                }
            }
            if (o.personal_data) {
                pick("hotbar.nbt");
                pick("command_history.txt");
            }
            if (o.mods) {
                pick("mods");
            }
            if (o.mods || o.disabled_mods) {
                for (const auto& [name, st] : blb::fs::list_directory(root / "mods")) {
                    bool disabled = blb::to_lower(name).ends_with(".disabled");
                    bool wanted = !st.is_directory && (disabled ? o.disabled_mods : o.mods);
                    if (wanted != o.mods) {
                        rules["mods/" + name] = wanted;
                    }
                }
            }
            if (o.mod_settings) {
                pick("config");
                pick("defaultconfigs");
            }
            if (o.saves) {
                pick("saves");
            }
            if (o.servers) {
                pick("servers.dat");
            }
            if (o.resource_packs) {
                pick("resourcepacks");
            }
            if (o.shader_packs) {
                pick("shaderpacks");
            }
            return rules;
        }

        void complete(const Finished& finished) {
            busy = false;
            busyKey.clear();
            if (finished.cancelled) {
                return;
            }
            if (!finished.error.empty()) {
                Notices::add(Notices::Kind::Failed, "导出失败：" + finished.error, 5000);
                return;
            }
            if (finished.notes.empty()) {
                Notices::add(Notices::Kind::Done, "已导出 " + blb::path_to_utf8(finished.file.filename()), 4000);
            } else {
                Notices::add(Notices::Kind::Info, "已导出，但存在兼容性或文件来源问题", 15000);
                for (const auto& note : finished.notes) {
                    // The legacy UI queues failed notices in dialogs; both UIs
                    // otherwise render ordinary notices as a single line.
                    Notices::add(Notices::Kind::Failed, "导出提示：" + note, 15000);
                }
            }
            Platform::reveal(finished.file);
        }
    }

    Options& options(const std::string& instanceKey) {
        auto found = remembered.find(instanceKey);
        if (found != remembered.end()) {
            return found->second;
        }
        Options options;
        if (const Instances::Entry* entry = Instances::find(instanceKey)) {
            options.name = entry->info.id;
        }
        if (const blb::auth::account* account = Accounts::current()) {
            options.author = account->name;
        }
        const nlohmann::json& data = Config::data();
        auto saved = data.find("exportOptions");
        if (saved != data.end() && saved->is_object()) {
            auto mine = saved->find(instanceKey);
            if (mine != saved->end() && mine->is_object()) {
                auto parsed = blb::modpack::parse_export_options(mine->dump());
                if (parsed) {
                    options = *parsed;
                }
            }
        }
        return remembered[instanceKey] = options;
    }

    void save(const std::string& instanceKey) {
        auto found = remembered.find(instanceKey);
        if (found == remembered.end()) {
            return;
        }
        nlohmann::json parsed =
                nlohmann::json::parse(blb::modpack::serialize_export_options(found->second), nullptr, false);
        if (parsed.is_object()) {
            Config::data()["exportOptions"][instanceKey] = parsed;
            Config::save();
        }
    }

    FileTree::Tree& tree(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr) {
            return files;
        }
        files.reset(entry->gameDirectory, entry->info.id);
        Options& chosen = options(instanceKey);
        if (!chosen.pick_files) {
            chosen.picks = migrate(entry->gameDirectory, chosen);
            chosen.pick_files = true;
        }
        return files;
    }

    namespace {
        bool flagged(const char* name, const std::string& instanceKey) {
            const nlohmann::json& data = Config::data();
            auto saved = data.find(name);
            if (saved == data.end() || !saved->is_object()) {
                return false;
            }
            auto mine = saved->find(instanceKey);
            return mine != saved->end() && mine->is_boolean() && mine->get<bool>();
        }

        void flag(const char* name, const std::string& instanceKey, bool on) {
            nlohmann::json& saved = Config::data()[name];
            if (!saved.is_object()) {
                saved = nlohmann::json::object();
            }
            if (on) {
                saved[instanceKey] = true;
            } else {
                saved.erase(instanceKey);
            }
            Config::save();
        }
    }

    bool withLauncher(const std::string& instanceKey) {
        return flagged("exportLauncher", instanceKey);
    }

    void setWithLauncher(const std::string& instanceKey, bool on) {
        flag("exportLauncher", instanceKey, on);
    }

    bool single(const std::string& instanceKey) {
        return flagged("exportSingle", instanceKey);
    }

    void setSingle(const std::string& instanceKey, bool on) {
        flag("exportSingle", instanceKey, on);
    }

    void startBundle(const std::string& instanceKey, LauncherBuilder::ThemeFiles themes) {
        if (std::string issue = LauncherBuilder::problem(); !issue.empty()) {
            Notices::add(Notices::Kind::Failed, "无法附带自定义启动器：" + issue, 5000);
            return;
        }
        Options chosen = options(instanceKey);
        std::shared_ptr<LauncherBuilder::Job> job = LauncherBuilder::prepare(std::move(themes));
        if (!LauncherBuilder::attachModpack(*job, instanceKey, chosen.name)) {
            return;
        }
        job->single = single(instanceKey);
        std::string base = fileName(chosen);
        base = base.substr(0, base.size() - std::string(".mrpack").size());
        std::optional<std::filesystem::path> picked = Platform::pickSaveAs(blb::path_from_utf8(base + ".zip"));
        if (!picked || busy) {
            return;
        }
        std::string folder = blb::fs::filter_filename(job->instanceName, '_');
        job->output = *picked;
        job->zip = true;
        job->folder = folder.empty() ? std::string("modpack") : folder;
        busy = true;
        busyKey = instanceKey;
        {
            std::lock_guard<std::mutex> guard(progressLock);
            progress = "正在准备";
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::filesystem::path output = job->output;
        tasks.run(
                [job, token](blb::cancellation_token) -> AsyncTasks::Continuation {
                    std::string error = LauncherBuilder::run(
                            *job,
                            [](const std::string& line) {
                                std::lock_guard<std::mutex> guard(progressLock);
                                progress = line;
                            },
                            token);
                    Finished finished{job->output, error, {}, token.is_cancelled()};
                    return [finished = std::move(finished)] { complete(finished); };
                },
                [output, token](const std::string& error) -> AsyncTasks::Continuation {
                    Finished finished{output, error, {}, !error.empty() && token.is_cancelled()};
                    return [finished = std::move(finished)] { complete(finished); };
                });
    }

    void start(const std::string& instanceKey, std::optional<LauncherBuilder::ThemeFiles> launcher) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (busy || source == nullptr) {
            return;
        }
        if (launcher) {
            startBundle(instanceKey, std::move(*launcher));
            return;
        }
        // The instances may be read again while the file dialog is open, so what is needed is copied before it.
        std::filesystem::path root = source->root;
        std::string id = entry->info.id;
        std::filesystem::path gameDirectory = entry->gameDirectory;
        Options chosen = options(instanceKey);
        std::optional<std::filesystem::path> picked = Platform::pickSavePack(blb::path_from_utf8(fileName(chosen)));
        if (!picked || busy) {
            return;
        }
        chosen.curseforge.api_key = CurseForge::apiKey();
        std::filesystem::path output = *picked;
        busy = true;
        busyKey = instanceKey;
        {
            std::lock_guard<std::mutex> guard(progressLock);
            progress = "正在准备";
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        tasks.run([root, id, gameDirectory, output, chosen, token](blb::cancellation_token) -> AsyncTasks::Continuation {
            Finished finished;
            finished.file = output;
            blb::function_progress_sink sink([](const blb::progress_event& event) {
                char text[64];
                if (event.task == "export:hash") {
                    std::snprintf(text, sizeof(text), "正在查找文件来源 %llu/%llu",
                                  (unsigned long long) event.done_items, (unsigned long long) event.total_items);
                } else if (event.total_bytes > 0) {
                    std::snprintf(text, sizeof(text), "正在打包 %d%%",
                                  (int) (event.done_bytes * 100 / event.total_bytes));
                } else {
                    std::snprintf(text, sizeof(text), "正在打包");
                }
                std::lock_guard<std::mutex> guard(progressLock);
                progress = text;
            });
            blb::install::install_options installOptions;
            installOptions.mirror = DownloadSources::provider();
            blb::install::game_installer installer(blb::meta::game_layout(root), installOptions);
            auto report = blb::modpack::export_modpack(installer, id, gameDirectory, output, chosen, &sink, token);
            if (!report) {
                finished.error = report.err().message;
                finished.cancelled = report.err().code == blb::errc::cancelled;
            } else {
                finished.notes = std::move(report->warnings);
            }
            finished.cancelled = finished.cancelled || (!finished.error.empty() && token.is_cancelled());
            return [finished = std::move(finished)] { complete(finished); };
        }, [output, token](const std::string& error) -> AsyncTasks::Continuation {
            Finished finished{output, error, {}, !error.empty() && token.is_cancelled()};
            return [finished = std::move(finished)] { complete(finished); };
        });
    }

    void cancel() {
        if (busy && cancelling) {
            cancelling->cancel();
        }
    }

    bool running() {
        return busy;
    }

    const std::string& runningKey() {
        return busyKey;
    }

    std::string status() {
        std::lock_guard<std::mutex> guard(progressLock);
        return progress;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        cancel();
        tasks.stop();
    }
}

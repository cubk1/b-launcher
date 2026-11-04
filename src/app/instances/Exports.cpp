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

    void start(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (busy || source == nullptr) {
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

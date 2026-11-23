#include "app/instances/PackUpdates.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/DownloadSources.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>

#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace PackUpdates {

    namespace blb = b_launcher_base;

    namespace {
        struct Cached {
            int64_t mtime = -1;
            std::optional<Origin> value;
        };

        std::map<std::string, Cached> origins;
        State current;
        std::shared_ptr<blb::modpack::update_plan> planned;
        AsyncTasks tasks;
        std::shared_ptr<blb::cancellation_source> cancelling;
        std::mutex progressLock;
        std::string progressText;

        void setProgress(const std::string& text) {
            std::lock_guard<std::mutex> guard(progressLock);
            progressText = text;
        }

        blb::install::install_options installOptions() {
            blb::install::install_options options;
            options.mirror = DownloadSources::provider();
            return options;
        }

        std::string originName(Addons::Source source) {
            return source == Addons::Source::CurseForge ? "curseforge" : "modrinth";
        }

        Summary summarize(const blb::modpack::update_plan& plan) {
            using blb::modpack::update_action;
            Summary out;
            out.version = plan.manifest.version;
            out.minecraft = plan.manifest.minecraft;
            for (const auto& loader : plan.manifest.loaders) {
                out.loaders.push_back(Installer::loaderName(loader.type) +
                                      (loader.version.empty() ? std::string() : " " + loader.version));
            }
            out.archive = plan.archive_size;
            out.loadersChanged = plan.loaders_changed;
            out.tracked = plan.tracked;
            for (const auto& item : plan.items) {
                if (item.action == update_action::remove) {
                    if (item.modified) {
                        out.kept.push_back(item.path);
                    } else {
                        out.removed++;
                    }
                } else if (item.modified) {
                    out.modified.push_back(item.path);
                    out.modifiedBytes += item.bytes;
                } else {
                    (item.action == update_action::add ? out.added : out.changed)++;
                    out.bytes += item.bytes;
                }
            }
            return out;
        }

        struct Target {
            std::filesystem::path root;
            std::string id;
        };

        std::optional<Target> targetOf(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            const Instances::Source* source = entry != nullptr ? Instances::source(entry->source) : nullptr;
            if (entry == nullptr || source == nullptr) {
                return std::nullopt;
            }
            return Target{source->root, entry->info.id};
        }

        std::string describe(const blb::progress_event& event) {
            char text[96];
            if (event.task == "update:overrides") {
                std::snprintf(text, sizeof(text), "正在更新配置文件 %llu/%llu", (unsigned long long) event.done_items,
                              (unsigned long long) event.total_items);
            } else if (event.task == "modpack-files" && event.total_bytes > 0) {
                std::snprintf(text, sizeof(text), "正在下载模组 %d%%",
                              (int) (event.done_bytes * 100 / event.total_bytes));
            } else if (event.task == "modpack-files") {
                std::snprintf(text, sizeof(text), "正在下载模组");
            } else {
                std::snprintf(text, sizeof(text), "正在安装游戏和加载器");
            }
            return text;
        }

        void finished(const std::string& key, const std::string& version, const std::string& error, bool cancelled) {
            planned.reset();
            if (cancelled) {
                current = State{};
                return;
            }
            if (!error.empty()) {
                current.phase = Phase::Failed;
                current.error = error;
                Notices::add(Notices::Kind::Failed, "更新整合包失败：" + error, 5000);
                return;
            }
            const Instances::Entry* entry = Instances::find(key);
            Notices::add(Notices::Kind::Done,
                         "已将 " + (entry != nullptr ? entry->info.id : key) + " 更新到 " + version, 4000);
            current = State{};
            Instances::refresh();
        }
    }

    const Origin* origin(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr) {
            return nullptr;
        }
        blb::fs::file_stat st = blb::fs::stat(entry->info.directory / blb::modpack::info_file);
        int64_t mtime = st.exists ? st.mtime : 0;
        Cached& cached = origins[instanceKey];
        if (cached.mtime != mtime) {
            cached.mtime = mtime;
            cached.value.reset();
            if (st.exists) {
                auto info = blb::modpack::read_pack_info(entry->info.directory);
                if (info && !info->origin.project.empty() && !info->origin.version.empty()) {
                    Origin found;
                    found.source =
                            info->origin.source == "curseforge" ? Addons::Source::CurseForge : Addons::Source::Modrinth;
                    found.project = info->origin.project;
                    found.version = info->origin.version;
                    found.versionName = info->version;
                    cached.value = std::move(found);
                }
            }
        }
        return cached.value ? &*cached.value : nullptr;
    }

    const State& state() {
        return current;
    }

    std::string progress() {
        std::lock_guard<std::mutex> guard(progressLock);
        return progressText;
    }

    void plan(const std::string& instanceKey, const Addons::Version& version) {
        if (running()) {
            return;
        }
        const Addons::File* file = version.file();
        std::optional<Target> target = targetOf(instanceKey);
        if (file == nullptr || !target) {
            return;
        }
        if (cancelling) {
            cancelling->cancel();
        }
        planned.reset();
        current = State{};
        current.phase = Phase::Planning;
        current.instanceKey = instanceKey;
        current.versionId = version.id;
        setProgress("正在比较文件");
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::vector<std::string> urls = Addons::fileUrls(file->url);
        uint64_t size = file->size > 0 ? (uint64_t) file->size : 0;
        std::string apiKey = CurseForge::apiKey();
        std::string versionId = version.id;
        tasks.run(
                [root = target->root, id = target->id, urls, size, apiKey, token, versionId,
                 instanceKey](blb::cancellation_token) -> AsyncTasks::Continuation {
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions());
                    blb::modpack::curseforge_settings curseforge{apiKey};
                    auto result = blb::modpack::plan_update(installer, id, urls, size, curseforge, token);
                    std::shared_ptr<blb::modpack::update_plan> made;
                    std::string error;
                    if (result) {
                        made = std::make_shared<blb::modpack::update_plan>(std::move(*result));
                    } else {
                        error = result.err().message;
                    }
                    bool cancelled = token.is_cancelled();
                    return [made, error, cancelled, versionId, instanceKey] {
                        if (current.phase != Phase::Planning || current.versionId != versionId ||
                            current.instanceKey != instanceKey) {
                            return;
                        }
                        if (cancelled) {
                            current = State{};
                        } else if (!made) {
                            current.phase = Phase::Failed;
                            current.error = error;
                        } else {
                            planned = made;
                            current.summary = summarize(*made);
                            current.phase = Phase::Planned;
                        }
                    };
                },
                [versionId, instanceKey](const std::string& error) -> AsyncTasks::Continuation {
                    return [error, versionId, instanceKey] {
                        if (current.phase == Phase::Planning && current.versionId == versionId &&
                            current.instanceKey == instanceKey) {
                            current.phase = Phase::Failed;
                            current.error = error;
                        }
                    };
                });
    }

    void apply(bool keepModified) {
        if (current.phase != Phase::Planned || !planned) {
            return;
        }
        std::optional<Target> target = targetOf(current.instanceKey);
        const Origin* from = origin(current.instanceKey);
        if (!target || from == nullptr) {
            return;
        }
        blb::modpack::pack_origin next{originName(from->source), from->project, current.versionId};
        current.phase = Phase::Applying;
        setProgress("正在准备");
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::shared_ptr<blb::modpack::update_plan> plan = planned;
        std::string key = current.instanceKey;
        std::string version = current.summary.version;
        tasks.run(
                [root = target->root, plan, next, keepModified, token, key,
                 version](blb::cancellation_token) -> AsyncTasks::Continuation {
                    blb::function_progress_sink sink(
                            [](const blb::progress_event& event) { setProgress(describe(event)); });
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions());
                    auto done = blb::modpack::apply_update(installer, *plan, next, !keepModified, &sink, token);
                    std::string error = done ? std::string() : done.err().message;
                    bool cancelled = token.is_cancelled() || (!done && done.err().code == blb::errc::cancelled);
                    return [key, version, error, cancelled] { finished(key, version, error, cancelled); };
                },
                [key, version, token](const std::string& error) -> AsyncTasks::Continuation {
                    bool cancelled = token.is_cancelled();
                    return [key, version, error, cancelled] { finished(key, version, error, cancelled); };
                });
    }

    void cancel() {
        if (cancelling) {
            cancelling->cancel();
        }
    }

    void reset() {
        if (current.phase == Phase::Applying) {
            return;
        }
        cancel();
        planned.reset();
        current = State{};
    }

    bool running() {
        return current.phase == Phase::Applying;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}

#include "app/instances/PackUpdates.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/DownloadSources.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Pixels.h"
#include "app/system/Transfers.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace PackUpdates {

    namespace blb = b_launcher_base;

    namespace {
        struct Cached {
            int64_t mtime = -1;
            std::chrono::steady_clock::time_point checked;
            std::optional<Origin> value;
            bool identifiable = false;
            std::unordered_map<std::string, int64_t> files;
        };

        std::set<std::string> recognized;

        std::map<std::string, Cached> origins;
        State current;
        uint64_t serials = 0;
        std::shared_ptr<blb::modpack::update_plan> planned;
        Identification found;
        struct Wanted {
            std::string url;
            std::chrono::steady_clock::time_point since;
        };
        std::map<std::string, Wanted> wantedIcons;
        std::vector<blb::modpack::pack_candidate> choices;
        AsyncTasks tasks;
        std::shared_ptr<blb::cancellation_source> cancelling;
        std::mutex progressLock;
        std::string progressText;
        std::vector<Step> planSteps;
        uint64_t planGeneration = 0;
        uint64_t transfer = 0;

        void setProgress(const std::string& text) {
            std::lock_guard<std::mutex> guard(progressLock);
            progressText = text;
        }

        void enterStep(uint64_t generation, std::string_view stage, const std::string& target) {
            std::lock_guard<std::mutex> guard(progressLock);
            if (generation != planGeneration) {
                return;
            }
            if (planSteps.empty()) {
                if (stage == "origin") {
                    planSteps.push_back({"读取已安装版本的信息"});
                }
                planSteps.push_back({"读取 " + target + " 的信息"});
                planSteps.push_back({"比较本地文件"});
            }
            size_t at = stage == "origin" ? 0 : stage == "index" ? planSteps.size() - 2 : planSteps.size() - 1;
            for (size_t i = 0; i < planSteps.size(); i++) {
                planSteps[i].state = i < at ? StepState::Done : i == at ? StepState::Active : StepState::Pending;
            }
        }

        void countStep(uint64_t generation, uint64_t done, uint64_t total) {
            std::lock_guard<std::mutex> guard(progressLock);
            if (generation != planGeneration || planSteps.empty()) {
                return;
            }
            planSteps.back().done = done;
            planSteps.back().total = total;
        }

        blb::install::install_options installOptions(bool inherit = false) {
            blb::install::install_options options;
            options.mirror = DownloadSources::provider();
            options.inherit_vanilla = inherit;
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
                out.items.push_back({item.path,
                                     item.action == update_action::add              ? Change::Add
                                             : item.action == update_action::change ? Change::Update
                                                                                    : Change::Remove,
                                     item.modified, item.bytes, item.replaces});
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

        std::string stageOf(const blb::progress_event& event) {
            std::string_view task = event.task;
            if (task == "update:overrides") {
                return "正在更新配置文件";
            }
            if (task.ends_with("modpack-files")) {
                return "正在下载模组";
            }
            return "正在安装游戏和加载器";
        }

        void track(const blb::progress_event& event, uint64_t id) {
            std::string_view task = event.task;
            Transfers::stage(id, stageOf(event));
            if (task.starts_with("download:") || task.starts_with("verify:") || task == "update:overrides") {
                Transfers::report(id, event);
            }
        }

        std::string describe(const blb::progress_event& event) {
            char text[96];
            if (event.task == "update:overrides") {
                std::snprintf(text, sizeof(text), "正在更新配置文件 %llu/%llu", (unsigned long long) event.done_items,
                              (unsigned long long) event.total_items);
            } else if (event.task == "download:modpack-files" && event.total_bytes > 0) {
                std::snprintf(text, sizeof(text), "正在下载模组 %d%%",
                              (int) (event.done_bytes * 100 / event.total_bytes));
            } else {
                return stageOf(event);
            }
            return text;
        }

        void finished(const std::string& key, const std::string& version, const std::string& error, bool cancelled) {
            planned.reset();
            Transfers::finish(transfer,
                              cancelled               ? Transfers::State::Cancelled
                                      : error.empty() ? Transfers::State::Done
                                                      : Transfers::State::Failed,
                              error);
            transfer = 0;
            if (cancelled) {
                current = State{};
                return;
            }
            if (!error.empty()) {
                current.phase = Phase::Failed;
                current.error = error;
                Notices::add(Notices::Kind::Failed, "更改整合包版本失败：" + error, 5000);
                return;
            }
            const Instances::Entry* entry = Instances::find(key);
            Notices::add(Notices::Kind::Done,
                         "已将 " + (entry != nullptr ? entry->info.id : key) + " 更改到 " + version, 4000);
            current = State{};
            Instances::refresh();
        }
    }

    namespace {
        Cached& cachedOf(const std::string& instanceKey) {
            static Cached none;
            const Instances::Entry* entry = Instances::find(instanceKey);
            if (entry == nullptr) {
                none = Cached{};
                return none;
            }
            Cached& cached = origins[instanceKey];
            auto now = std::chrono::steady_clock::now();
            if (cached.mtime >= 0 && now - cached.checked < std::chrono::seconds(1)) {
                return cached;
            }
            cached.checked = now;
            blb::fs::file_stat st = blb::fs::stat(entry->info.directory / blb::modpack::info_file);
            int64_t mtime = st.exists ? st.mtime : 0;
            if (cached.mtime == mtime) {
                return cached;
            }
            cached = Cached{};
            cached.mtime = mtime;
            cached.checked = now;
            if (!st.exists) {
                return cached;
            }
            auto info = blb::modpack::read_pack_info(entry->info.directory);
            if (!info) {
                return cached;
            }
            for (const auto& [path, owned] : info->files) {
                cached.files[path] = owned.size;
            }
            if (!info->origin.project.empty() && !info->origin.version.empty()) {
                Origin read;
                read.source =
                        info->origin.source == "curseforge" ? Addons::Source::CurseForge : Addons::Source::Modrinth;
                read.project = info->origin.project;
                read.version = info->origin.version;
                read.versionName = info->version;
                cached.value = std::move(read);
            } else {
                cached.identifiable = (info->format == "modrinth" || info->format == "curseforge") &&
                        !info->name.empty() && !info->version.empty();
            }
            return cached;
        }

        bool certain(const blb::modpack::pack_candidate& candidate) {
            return candidate.exact && candidate.total > 0 && candidate.matched * 10 >= candidate.total * 9;
        }

        Candidate shownOf(const blb::modpack::pack_candidate& candidate) {
            Candidate out;
            out.title = candidate.title;
            out.version = candidate.version_name;
            out.source =
                    candidate.origin.source == "curseforge" ? Addons::Source::CurseForge : Addons::Source::Modrinth;
            out.matched = candidate.matched;
            out.total = candidate.total;
            out.exact = candidate.exact;
            return out;
        }

        void searchFailed(const std::string& instanceKey, const std::string& error) {
            if (found.instanceKey == instanceKey) {
                found.phase = Search::Failed;
                found.error = error;
            }
        }
    }

    const Origin* origin(const std::string& instanceKey) {
        Cached& cached = cachedOf(instanceKey);
        return cached.value ? &*cached.value : nullptr;
    }

    bool identifiable(const std::string& instanceKey) {
        return cachedOf(instanceKey).identifiable;
    }

    bool packFile(const std::string& instanceKey, const std::filesystem::path& file, uint64_t size) {
        const Cached& cached = cachedOf(instanceKey);
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (cached.files.empty() || entry == nullptr) {
            return false;
        }
        std::string relative = blb::path_to_utf8(file.lexically_relative(entry->gameDirectory));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if (relative.ends_with(".disabled")) {
            relative.resize(relative.size() - 9);
        }
        auto found = cached.files.find(relative);
        return found != cached.files.end() && (found->second < 0 || (uint64_t) found->second == size);
    }

    const Identification& identification() {
        return found;
    }

    void identify(const std::string& instanceKey) {
        std::optional<Target> target = targetOf(instanceKey);
        if (!target || found.phase == Search::Running || found.phase == Search::Binding) {
            return;
        }
        found = Identification{};
        found.phase = Search::Running;
        found.instanceKey = instanceKey;
        choices.clear();
        std::string apiKey = CurseForge::apiKey();
        tasks.run(
                [root = target->root, id = target->id, apiKey,
                 instanceKey](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions());
                    blb::modpack::curseforge_settings curseforge{apiKey};
                    auto result = blb::modpack::identify_pack(installer, id, curseforge, token);
                    std::vector<blb::modpack::pack_candidate> list;
                    std::string error;
                    bool bound = false;
                    if (!result) {
                        error = result.err().message;
                    } else {
                        list = std::move(*result);
                        if (!list.empty() && certain(list.front())) {
                            auto bind = blb::modpack::bind_pack(installer, id, list.front(), curseforge, token);
                            if (bind) {
                                bound = true;
                            } else {
                                error = bind.err().message;
                            }
                        }
                    }
                    std::string icon = bound ? list.front().icon_url : std::string();
                    return [list = std::move(list), error, bound, icon, instanceKey]() mutable {
                        if (found.instanceKey != instanceKey) {
                            return;
                        }
                        if (bound) {
                            adoptIcon(instanceKey, icon);
                            found = Identification{};
                            return;
                        }
                        if (!error.empty()) {
                            searchFailed(instanceKey, error);
                            return;
                        }
                        choices = std::move(list);
                        found.candidates.clear();
                        for (const auto& candidate : choices) {
                            found.candidates.push_back(shownOf(candidate));
                        }
                        found.phase = Search::Done;
                    };
                },
                [instanceKey](const std::string& error) -> AsyncTasks::Continuation {
                    return [error, instanceKey] { searchFailed(instanceKey, error); };
                });
    }

    void recognize(const std::string& instanceKey) {
        if (recognized.contains(instanceKey)) {
            return;
        }
        const Cached& cached = cachedOf(instanceKey);
        if (!cached.files.empty()) {
            recognized.insert(instanceKey);
            return;
        }
        if (cached.identifiable) {
            identify(instanceKey);
            if (found.instanceKey == instanceKey) {
                recognized.insert(instanceKey);
            }
            return;
        }
        std::optional<Target> target = targetOf(instanceKey);
        if (!cached.value || !target) {
            return;
        }
        recognized.insert(instanceKey);
        std::string apiKey = CurseForge::apiKey();
        tasks.run(
                [root = target->root, id = target->id,
                 apiKey](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions());
                    blb::modpack::curseforge_settings curseforge{apiKey};
                    (void) blb::modpack::record_pack_files(installer, id, curseforge, token);
                    return [] {};
                },
                [](const std::string&) -> AsyncTasks::Continuation { return [] {}; });
    }

    void choose(size_t index) {
        if (found.phase != Search::Done || index >= choices.size()) {
            return;
        }
        std::optional<Target> target = targetOf(found.instanceKey);
        if (!target) {
            return;
        }
        found.phase = Search::Binding;
        blb::modpack::pack_candidate picked = choices[index];
        std::string apiKey = CurseForge::apiKey();
        std::string instanceKey = found.instanceKey;
        tasks.run(
                [root = target->root, id = target->id, picked, apiKey,
                 instanceKey](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions());
                    blb::modpack::curseforge_settings curseforge{apiKey};
                    auto bind = blb::modpack::bind_pack(installer, id, picked, curseforge, token);
                    std::string error = bind ? std::string() : bind.err().message;
                    return [error, instanceKey, icon = picked.icon_url] {
                        if (found.instanceKey != instanceKey) {
                            return;
                        }
                        if (error.empty()) {
                            adoptIcon(instanceKey, icon);
                            found = Identification{};
                        } else {
                            searchFailed(instanceKey, error);
                        }
                    };
                },
                [instanceKey](const std::string& error) -> AsyncTasks::Continuation {
                    return [error, instanceKey] { searchFailed(instanceKey, error); };
                });
    }

    const State& state() {
        return current;
    }

    std::string progress() {
        std::lock_guard<std::mutex> guard(progressLock);
        return progressText;
    }

    std::vector<Step> steps() {
        std::lock_guard<std::mutex> guard(progressLock);
        return planSteps;
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
        uint64_t generation;
        {
            std::lock_guard<std::mutex> guard(progressLock);
            planSteps.clear();
            generation = ++planGeneration;
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::vector<std::string> urls = Addons::fileUrls(file->url);
        uint64_t size = file->size > 0 ? (uint64_t) file->size : 0;
        std::string apiKey = CurseForge::apiKey();
        std::string versionId = version.id;
        const Origin* from = origin(instanceKey);
        std::string targetName = Addons::sourceName(from != nullptr ? from->source : version.source);
        tasks.run(
                [root = target->root, id = target->id, urls, size, apiKey, token, versionId, instanceKey, generation,
                 targetName](blb::cancellation_token) -> AsyncTasks::Continuation {
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions());
                    blb::modpack::curseforge_settings curseforge{apiKey};
                    blb::function_progress_sink sink(
                            [generation](const blb::progress_event& event) {
                                if (event.task == "update:compare") {
                                    countStep(generation, event.done_items, event.total_items);
                                }
                            },
                            [generation, targetName](std::string_view task, std::string_view stage) {
                                if (task == "update") {
                                    enterStep(generation, stage, targetName);
                                }
                            });
                    auto result = blb::modpack::plan_update(installer, id, urls, size, curseforge, &sink, token);
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
                            current.serial = ++serials;
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

    void apply(const std::set<std::string>& keep) {
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
        const Instances::Entry* entry = Instances::find(key);
        Transfers::Spec spec;
        spec.kind = Transfers::Kind::Modpack;
        spec.title = (entry != nullptr ? entry->info.id : key) + " 更改到 " + version;
        spec.minecraft = current.summary.minecraft;
        spec.instance = key;
        if (entry != nullptr) {
            spec.folder = entry->info.directory;
        }
        spec.cancel = [] { PackUpdates::cancel(); };
        transfer = Transfers::begin(std::move(spec));
        Transfers::stage(transfer, "正在准备");
        tasks.run(
                [root = target->root, plan, next, keep, token, key, inherit = Instances::inheritVanilla(), version,
                 id = transfer](blb::cancellation_token) -> AsyncTasks::Continuation {
                    blb::function_progress_sink sink([id](const blb::progress_event& event) {
                        setProgress(describe(event));
                        track(event, id);
                    });
                    blb::install::game_installer installer(blb::meta::game_layout(root), installOptions(inherit));
                    auto done = blb::modpack::apply_update(installer, *plan, next, keep, &sink, token);
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

    void adoptIcon(const std::string& instanceKey, const std::string& url) {
        if (!url.empty()) {
            wantedIcons[instanceKey] = {url, std::chrono::steady_clock::now()};
        }
    }

    void poll() {
        tasks.poll();
        auto now = std::chrono::steady_clock::now();
        for (auto it = wantedIcons.begin(); it != wantedIcons.end();) {
            const Instances::Entry* entry = Instances::find(it->first);
            std::shared_ptr<const Pixels> pixels = Addons::icon(it->second.url);
            if (entry != nullptr && pixels) {
                (void) blb::fs::write_file_atomic(entry->info.directory / blb::modpack::icon_file,
                                                  Pixels::encodePng(*pixels));
                it = wantedIcons.erase(it);
            } else if (now - it->second.since > std::chrono::minutes(2)) {
                it = wantedIcons.erase(it);
            } else {
                ++it;
            }
        }
    }

    void shutdown() {
        tasks.stop();
    }
}

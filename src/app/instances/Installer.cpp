#include "app/instances/Installer.h"
#include "app/downloads/CurseForge.h"
#include "app/instances/InstancePage.h"
#include "app/instances/Instances.h"
#include "app/shell/MainWindow.h"
#include "app/instances/NewInstanceScreen.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/hash.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/installer/loader.hpp>
#include <core/instance/loader_editor.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>
#include <core/network/download_client.hpp>
#include <core/network/mirror_provider.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace Installer {

    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr std::chrono::milliseconds LINGER{700};

        bool busy = false;
        std::thread worker;
        std::shared_ptr<blb::cancellation_source> cancelling;
        std::atomic<float> fraction{-1.0f};
        std::string sourceKey;
        bool lingering = false;
        Clock::time_point lingerUntil;
        Clock::time_point startedAt;
        std::string finishedKey;
        bool modifying = false;

        std::mutex lock;
        bool done = false;
        std::string installedId;
        std::string failure;
        bool cancelled = false;

        struct Tracked {
            Part part;
            std::string key;
            std::map<std::string, std::pair<uint64_t, uint64_t>> downloads;
            std::map<std::string, double> speeds;
            std::pair<uint64_t, uint64_t> checked{0, 0};
        };

        std::vector<std::string> steps;

        std::string megabytes(double bytes) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", bytes / (1024.0 * 1024.0));
            return text;
        }

        std::string loaderShown(const std::string& name) {
            static const std::map<std::string, std::string> names = {
                    {"forge", "Forge"},
                    {"neoforge", "NeoForge"},
                    {"cleanroom", "Cleanroom"},
                    {"fabric", "Fabric"},
                    {"legacyfabric", "Legacy Fabric"},
                    {"quilt", "Quilt"},
                    {"liteloader", "LiteLoader"},
                    {"optifine", "OptiFine"},
                    {"fabric-api", "Fabric API"},
                    {"legacy-fabric-api", "Legacy Fabric API"},
                    {"quilt-api", "QSL"},
            };
            auto found = names.find(name);
            return found != names.end() ? found->second : name;
        }

        std::string describe(const std::string& step) {
            if (step == "pack") return "正在下载整合包文件";
            if (step == "modpack:version") return "正在安装游戏和加载器";
            if (step == "modpack:resolve") return "正在整理模组列表";
            if (step == "modpack:files") return "正在下载模组文件";
            if (step == "modpack:overrides") return "正在解压整合包配置";
            if (step == "vanilla:json") return "正在获取版本信息";
            if (step == "vanilla:core") return "正在下载游戏本体";
            if (step == "vanilla:assets") return "正在下载游戏资源";
            if (step.rfind("processor", 0) == 0) return "正在运行加载器的安装处理";
            size_t colon = step.find(':');
            if (colon != std::string::npos) {
                std::string kind = step.substr(0, colon);
                std::string loader = loaderShown(step.substr(colon + 1));
                if (kind == "resolve") return "正在获取 " + loader + " 安装信息";
                if (kind == "prepare") return "正在准备 " + loader;
                if (kind == "finalize") return "正在安装 " + loader;
            }
            return "正在安装";
        }

        void describe(Tracked& part) {
            if (part.part.status == Part::Status::Done) {
                part.part.detail = "完成";
                return;
            }
            if (part.part.status == Part::Status::Failed) {
                part.part.detail = "失败";
                return;
            }
            uint64_t got = 0;
            uint64_t total = 0;
            for (const auto& download : part.downloads) {
                got += download.second.first;
                total += download.second.second;
            }
            double speed = 0.0;
            for (const auto& entry : part.speeds) {
                speed += entry.second;
            }
            if (total > 0 && got < total) {
                part.part.detail = megabytes((double) got) + "/" + megabytes((double) total) + " MB";
                if (speed > 0.0) {
                    part.part.detail += "  " + megabytes(speed) + " MB/s";
                }
            } else if (part.checked.second > 0 && part.checked.first < part.checked.second) {
                part.part.detail =
                        "校验 " + std::to_string(part.checked.first) + "/" + std::to_string(part.checked.second);
            } else {
                part.part.detail = part.part.status == Part::Status::Waiting ? "等待中" : "进行中";
            }
        }

        std::mutex partLock;
        std::vector<Tracked> tracked;

        Tracked* partByKey(const std::string& key) {
            for (Tracked& part : tracked) {
                if (part.key == key) {
                    return &part;
                }
            }
            return nullptr;
        }

        Tracked* partOfSubject(const std::string& subject) {
            if (Tracked* exact = partByKey(subject)) {
                return exact;
            }
            if (Tracked* plain = partByKey("game")) {
                return plain;
            }
            if (subject == "game-assets" || subject == "asset-index") {
                return partByKey("assets");
            }
            Tracked* best = nullptr;
            for (Tracked& part : tracked) {
                if (part.key != "core" && part.key != "assets" && subject.rfind(part.key, 0) == 0 &&
                    (best == nullptr || part.key.size() > best->key.size())) {
                    best = &part;
                }
            }
            return best != nullptr ? best : partByKey("core");
        }

        Tracked* partOfStage(const std::string& stage, bool& last) {
            last = false;
            if (stage == "pack") {
                last = true;
                return partByKey("pack");
            }
            if (stage == "modpack:files" || stage == "modpack:overrides") {
                last = stage == "modpack:overrides";
                return partByKey("modpack-files");
            }
            if (stage == "vanilla:json" || stage == "vanilla:core") {
                last = stage == "vanilla:core";
                return partByKey("core");
            }
            if (stage == "vanilla:assets") {
                last = true;
                return partByKey("assets");
            }
            size_t colon = stage.find(':');
            if (colon != std::string::npos) {
                std::string step = stage.substr(0, colon);
                if (step == "resolve" || step == "prepare" || step == "finalize") {
                    last = step == "finalize";
                    return partByKey(stage.substr(colon + 1));
                }
            }
            if (stage.rfind("processor", 0) == 0) {
                for (Tracked& part : tracked) {
                    if (part.key == "forge" || part.key == "neoforge" || part.key == "cleanroom") {
                        return &part;
                    }
                }
            }
            return nullptr;
        }

        void measure(Tracked& part) {
            uint64_t got = 0;
            uint64_t total = 0;
            for (const auto& download : part.downloads) {
                got += download.second.first;
                total += download.second.second;
            }
            if (total > 0) {
                part.part.fraction = (float) ((double) std::min(got, total) / (double) total);
            } else if (part.checked.second > 0) {
                part.part.fraction = (float) ((double) std::min(part.checked.first, part.checked.second) /
                                              (double) part.checked.second);
            } else {
                part.part.fraction = -1.0f;
            }
        }

        class Progress final : public blb::progress_sink {
        public:
            void on_stage(std::string_view task, std::string_view stage) override {
                std::lock_guard<std::mutex> guard(partLock);
                std::string name(task);
                if (stage == "start") {
                    steps.push_back(name);
                } else {
                    steps.erase(std::remove(steps.begin(), steps.end(), name), steps.end());
                }
                bool last = false;
                Tracked* part = partOfStage(std::string(task), last);
                if (part == nullptr || part->part.status == Part::Status::Done) {
                    return;
                }
                if (stage == "failed") {
                    part->part.status = Part::Status::Failed;
                } else if (stage == "done" && last) {
                    part->part.status = Part::Status::Done;
                    part->part.fraction = 1.0f;
                } else {
                    part->part.status = Part::Status::Running;
                }
                describe(*part);
            }

            void on_progress(const blb::progress_event& event) override {
                std::string task(event.task);
                size_t colon = task.find(':');
                std::string kind = colon == std::string::npos ? task : task.substr(0, colon);
                std::string subject = colon == std::string::npos ? task : task.substr(colon + 1);
                {
                    std::lock_guard<std::mutex> guard(partLock);
                    if (Tracked* part = partOfSubject(subject)) {
                        if (part->part.status == Part::Status::Waiting) {
                            part->part.status = Part::Status::Running;
                        }
                        if (kind == "download" && event.total_bytes > 0) {
                            part->downloads[task] = {event.finished ? event.total_bytes : event.done_bytes,
                                                     event.total_bytes};
                            part->speeds[task] = event.finished ? 0.0 : event.bytes_per_second;
                        } else if (kind == "verify" && event.total_items > 0) {
                            part->checked = {event.done_items, event.total_items};
                        }
                        if (part->part.status != Part::Status::Done) {
                            measure(*part);
                            describe(*part);
                        }
                    }
                }
                if (kind != "download") {
                    return;
                }
                std::lock_guard<std::mutex> guard(totals);
                if (event.total_bytes > 0) {
                    sizes[task] = {event.finished ? event.total_bytes : event.done_bytes, event.total_bytes};
                }
                uint64_t got = 0;
                uint64_t total = 0;
                for (const auto& size : sizes) {
                    got += size.second.first;
                    total += size.second.second;
                }
                fraction.store(total > 0 ? (float) ((double) std::min(got, total) / (double) total) : -1.0f);
            }

        private:
            std::mutex totals;
            std::map<std::string, std::pair<uint64_t, uint64_t>> sizes;
        };

        void join() {
            if (worker.joinable()) {
                worker.join();
            }
        }

        std::string duration(Clock::duration elapsed) {
            double seconds = std::chrono::duration<double>(elapsed).count();
            char text[48];
            if (seconds < 60.0) {
                std::snprintf(text, sizeof(text), "%.1f 秒", seconds);
            } else {
                int whole = (int) (seconds + 0.5);
                std::snprintf(text, sizeof(text), "%d 分 %d 秒", whole / 60, whole % 60);
            }
            return text;
        }

        void finishAll(Part::Status status) {
            std::lock_guard<std::mutex> guard(partLock);
            for (Tracked& part : tracked) {
                if (status == Part::Status::Done) {
                    part.part.status = Part::Status::Done;
                    part.part.fraction = 1.0f;
                } else if (part.part.status != Part::Status::Done) {
                    part.part.status = status;
                }
                describe(part);
            }
            steps.clear();
        }
    }

    bool running() {
        return busy;
    }

    float progress() {
        if (!busy) {
            return -1.0f;
        }
        return lingering ? 1.0f : fraction.load();
    }

    std::string activity() {
        std::lock_guard<std::mutex> guard(partLock);
        if (lingering) {
            return "安装完成";
        }
        return steps.empty() ? std::string("正在准备") : describe(steps.back());
    }

    double elapsed() {
        return busy ? std::chrono::duration<double>(Clock::now() - startedAt).count() : 0.0;
    }

    std::vector<Part> parts() {
        std::lock_guard<std::mutex> guard(partLock);
        std::vector<Part> out;
        if (busy) {
            for (const Tracked& part : tracked) {
                out.push_back(part.part);
            }
        }
        return out;
    }

    namespace {
        void begin(const std::string& source) {
            join();
            busy = true;
            modifying = false;
            lingering = false;
            startedAt = Clock::now();
            sourceKey = source;
            fraction.store(-1.0f);
            {
                std::lock_guard<std::mutex> stepGuard(partLock);
                steps.clear();
            }
            std::lock_guard<std::mutex> guard(lock);
            done = false;
            installedId.clear();
            failure.clear();
            cancelled = false;
        }

        std::vector<std::filesystem::path> newFolders(const std::filesystem::path& root,
                                                      const std::vector<std::string>& ids) {
            blb::meta::game_layout layout(root);
            std::vector<std::filesystem::path> out;
            for (const std::string& id : ids) {
                std::error_code error;
                if (!id.empty() && !std::filesystem::exists(layout.version_dir(id), error)) {
                    out.push_back(layout.version_dir(id));
                }
            }
            return out;
        }

        void removeFolders(const std::vector<std::filesystem::path>& folders) {
            for (const std::filesystem::path& folder : folders) {
                std::error_code error;
                std::filesystem::remove_all(folder, error);
            }
        }

        void finished(const std::string& result, const std::string& error, bool wasCancelled) {
            std::lock_guard<std::mutex> guard(lock);
            installedId = result;
            failure = error;
            cancelled = wasCancelled;
            done = true;
        }
    }

    void startModpack(const std::string& source, const Modpack& pack, const std::string& id) {
        const Instances::Source* target = Instances::source(source);
        bool local = !pack.file.empty();
        if (busy || target == nullptr || (pack.urls.empty() && !local)) {
            return;
        }
        begin(source);
        {
            std::lock_guard<std::mutex> guard(partLock);
            tracked.clear();
            if (!local) {
                tracked.push_back(Tracked{Part{"整合包"}, "pack"});
            }
            tracked.push_back(Tracked{Part{"游戏本体"}, "core"});
            tracked.push_back(Tracked{Part{"游戏资源"}, "assets"});
            for (blb::install::loader_type loader : pack.loaders) {
                tracked.push_back(Tracked{Part{InstancePage::loaderLabel(loader)},
                                          std::string(blb::install::loader_name(loader))});
            }
            tracked.push_back(Tracked{Part{"模组文件"}, "modpack-files"});
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::filesystem::path root = target->root;
        std::filesystem::path archive = local
                ? pack.file
                : std::filesystem::temp_directory_path() / blb::path_from_utf8("b-launcher-" + id + ".mrpack");
        std::vector<std::filesystem::path> made = newFolders(root, {id, pack.minecraft});
        blb::net::download_task download;
        download.urls = pack.urls;
        download.target = archive;
        download.size = pack.size;
        if (!pack.sha1.empty()) {
            download.sha1 = blb::parse_sha1(pack.sha1);
        }
        worker = std::thread([root, archive, download, id, token, made, local] {
            std::string result;
            std::string error;
            bool wasCancelled = false;
            try {
                Progress sink;
                blb::net::download_report report;
                if (!local) {
                    sink.on_stage("pack", "start");
                    blb::progress_tracker tracker(&sink, "download:pack");
                    blb::net::download_client client;
                    report = client.run(std::vector<blb::net::download_task>{download}, &tracker, token);
                }
                if (!report.ok()) {
                    error = report.failures.front().reason.message;
                    sink.on_stage("pack", "failed");
                } else {
                    if (!local) {
                        sink.on_stage("pack", "done");
                    }
                    blb::install::install_options options;
                    options.mirror = blb::net::make_bmclapi_provider();
                    blb::install::game_installer installer(blb::meta::game_layout(root), options);
                    blb::modpack::import_options settings;
                    settings.id = id;
                    settings.curseforge.api_key = CurseForge::apiKey();
                    auto imported = blb::modpack::import_modpack(installer, archive, settings, &sink, token);
                    if (imported) {
                        result = *imported;
                    } else {
                        error = imported.err().message;
                        wasCancelled = imported.err().code == blb::errc::cancelled;
                    }
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (!local) {
                std::error_code removeError;
                std::filesystem::remove(archive, removeError);
            }
            if (result.empty()) {
                removeFolders(made);
            }
            finished(result, error, wasCancelled || token.is_cancelled());
        });
    }

    void start(const std::string& source, const std::string& minecraft,
               const std::vector<blb::install::loader_request>& loaders, const std::string& id) {
        const Instances::Source* target = Instances::source(source);
        if (busy || target == nullptr) {
            return;
        }
        begin(source);
        {
            std::lock_guard<std::mutex> guard(partLock);
            tracked.clear();
            if (loaders.empty() && id.empty()) {
                tracked.push_back(Tracked{Part{"游戏文件"}, "game"});
            } else {
                tracked.push_back(Tracked{Part{"游戏本体"}, "core"});
                tracked.push_back(Tracked{Part{"游戏资源"}, "assets"});
                for (const blb::install::loader_request& loader : loaders) {
                    tracked.push_back(Tracked{Part{InstancePage::loaderLabel(loader.type)},
                                              std::string(blb::install::loader_name(loader.type))});
                }
            }
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::filesystem::path root = target->root;
        blb::install::install_request request;
        request.minecraft = minecraft;
        request.loaders = loaders;
        request.id = id;
        std::string expected = id;
        if (expected.empty()) {
            std::vector<blb::install::loader_info> infos;
            for (const blb::install::loader_request& loader : loaders) {
                infos.push_back(blb::install::loader_info{loader.type, loader.version});
            }
            expected = loaders.empty() ? minecraft : blb::install::default_version_id(request, infos);
        }
        std::vector<std::filesystem::path> made =
                newFolders(root, {expected, loaders.empty() && id.empty() ? std::string() : minecraft});
        worker = std::thread([root, request, token, made] {
            std::string result;
            std::string error;
            bool wasCancelled = false;
            try {
                blb::install::install_options options;
                options.mirror = blb::net::make_bmclapi_provider();
                blb::install::game_installer installer(blb::meta::game_layout(root), options);
                Progress sink;
                if (request.loaders.empty() && request.id.empty()) {
                    auto installed = installer.install_vanilla(request.minecraft, &sink, token);
                    if (installed) {
                        result = installed->id;
                    } else {
                        error = installed.err().message;
                        wasCancelled = installed.err().code == blb::errc::cancelled;
                    }
                } else {
                    blb::install::loader_installer loaders(installer);
                    auto installed = loaders.install(request, &sink, token);
                    if (installed) {
                        result = *installed;
                    } else {
                        error = installed.err().message;
                        wasCancelled = installed.err().code == blb::errc::cancelled;
                    }
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (result.empty()) {
                removeFolders(made);
            }
            finished(result, error, wasCancelled || token.is_cancelled());
        });
    }

    void startModify(const std::string& instanceKey, const std::vector<blb::install::loader_request>& loaders) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Instances::Source* target = entry != nullptr ? Instances::source(entry->source) : nullptr;
        if (busy || target == nullptr) {
            return;
        }
        begin(entry->source);
        modifying = true;
        {
            std::lock_guard<std::mutex> guard(partLock);
            tracked.clear();
            tracked.push_back(Tracked{Part{"游戏本体"}, "core"});
            tracked.push_back(Tracked{Part{"游戏资源"}, "assets"});
            for (const blb::install::loader_request& loader : loaders) {
                tracked.push_back(Tracked{Part{InstancePage::loaderLabel(loader.type)},
                                          std::string(blb::install::loader_name(loader.type))});
            }
        }
        cancelling = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = cancelling->token();
        std::filesystem::path root = target->root;
        std::string id = entry->info.id;
        blb::instance::isolation_mode mode = Instances::isolation();
        worker = std::thread([root, id, loaders, mode, token] {
            std::string result;
            std::string error;
            bool wasCancelled = false;
            try {
                blb::install::install_options options;
                options.mirror = blb::net::make_bmclapi_provider();
                blb::install::game_installer installer(blb::meta::game_layout(root), options);
                blb::instance::instance_manager manager(blb::meta::game_layout(root), mode);
                Progress sink;
                auto changed = blb::instance::modify_loaders(installer, manager, id, loaders, &sink, token);
                if (changed) {
                    result = *changed;
                } else {
                    error = changed.err().message;
                    wasCancelled = changed.err().code == blb::errc::cancelled;
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            finished(result, error, wasCancelled || token.is_cancelled());
        });
    }

    void cancel() {
        if (busy && !lingering && cancelling) {
            cancelling->cancel();
        }
    }

    void poll() {
        if (!busy) {
            return;
        }
        if (lingering) {
            if (Clock::now() >= lingerUntil) {
                lingering = false;
                busy = false;
                std::string key = finishedKey;
                bool changedLoaders = modifying;
                Instances::refresh([key, changedLoaders] {
                    Instances::select(key);
                    NewInstanceScreen::reset();
                    if (changedLoaders) {
                        MainWindow::openVersionSettings(key);
                    } else {
                        MainWindow::showHome();
                    }
                });
            }
            return;
        }
        std::string id;
        std::string error;
        bool wasCancelled;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!done) {
                return;
            }
            id = installedId;
            error = failure;
            wasCancelled = cancelled;
        }
        join();
        if (!id.empty()) {
            Notices::add(Notices::Kind::Done,
                         (modifying ? "已修改 " + id + " 的加载器" : "已安装 " + id) + "，耗时 " +
                                 duration(Clock::now() - startedAt),
                         4000);
            finishAll(Part::Status::Done);
            finishedKey = Instances::keyOf(sourceKey, id);
            lingering = true;
            lingerUntil = Clock::now() + LINGER;
            return;
        }
        busy = false;
        if (!wasCancelled) {
            Notices::add(Notices::Kind::Failed, (modifying ? "修改加载器失败：" : "安装失败：") + error, 5000);
        }
        Instances::refresh();
    }

    void shutdown() {
        cancel();
        join();
    }
}

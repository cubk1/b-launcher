#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Instances.h"
#include "app/system/Platform.h"

#include "ui/panel/Notifications.h"

#include <core/base/cancellation.hpp>
#include <core/base/hash.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>
#include <core/network/download_client.hpp>
#include <core/network/mirror_provider.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace ProjectInstaller {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = std::function<void()>;

        constexpr int DEPENDENCY_DEPTH = 4;

        struct Task {
            std::atomic<int> state{(int) State::Idle};
            std::atomic<float> fraction{-1.0f};
        };

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;
        std::map<std::string, std::shared_ptr<Task>> tasks;

        void run(std::function<Continuation(blb::cancellation_token)> work) {
            blb::cancellation_token token = stopping.token();
            workers.emplace_back([work, token] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    std::string message = error.what();
                    next = [message] { Notifications::add(Notifications::FAILED, "下载失败：" + message, 5000); };
                }
                if (next) {
                    std::lock_guard<std::mutex> guard(lock);
                    finished.push_back(std::move(next));
                }
            });
        }

        std::shared_ptr<Task> begin(const std::string& versionId) {
            std::shared_ptr<Task>& task = tasks[versionId];
            if (!task) {
                task = std::make_shared<Task>();
            }
            task->state = (int) State::Running;
            task->fraction = -1.0f;
            return task;
        }

        class Progress final : public blb::progress_sink {
        public:
            explicit Progress(std::shared_ptr<Task> task) : task(std::move(task)) {}

            void on_progress(const blb::progress_event& event) override {
                std::lock_guard<std::mutex> guard(sizesLock);
                std::string name(event.task);
                if (event.total_bytes > 0) {
                    sizes[name] = {event.done_bytes, event.total_bytes};
                }
                unsigned long long done = 0;
                unsigned long long total = 0;
                for (const auto& size : sizes) {
                    done += size.second.first;
                    total += size.second.second;
                }
                task->fraction = total > 0 ? (float) ((double) std::min(done, total) / (double) total) : -1.0f;
            }

        private:
            std::shared_ptr<Task> task;
            std::mutex sizesLock;
            std::map<std::string, std::pair<unsigned long long, unsigned long long>> sizes;
        };

        blb::net::download_task taskFor(const Addons::File& file, const std::filesystem::path& target) {
            blb::net::download_task task;
            task.urls = Addons::fileUrls(file.url);
            task.target = target;
            task.size = file.size;
            if (!file.sha1.empty()) {
                task.sha1 = blb::parse_sha1(file.sha1);
            }
            return task;
        }

        std::string download(const std::vector<blb::net::download_task>& files, const std::shared_ptr<Task>& task,
                             blb::cancellation_token token) {
            Progress sink(task);
            blb::progress_tracker tracker(&sink, "download");
            blb::net::download_client client;
            blb::net::download_report report = client.run(files, &tracker, token);
            if (report.ok()) {
                return {};
            }
            return report.failures.front().reason.message;
        }

        void finish(const std::shared_ptr<Task>& task, bool ok, const std::string& text) {
            task->state = (int) (ok ? State::Done : State::Failed);
            Notifications::add(ok ? Notifications::DONE : Notifications::FAILED, text, ok ? 3000 : 5000);
        }
    }

    State state(const std::string& versionId) {
        auto found = tasks.find(versionId);
        return found != tasks.end() ? (State) found->second->state.load() : State::Idle;
    }

    float progress(const std::string& versionId) {
        auto found = tasks.find(versionId);
        return found != tasks.end() ? found->second->fraction.load() : -1.0f;
    }

    std::string folder(Addons::Kind kind) {
        switch (kind) {
            case Addons::Kind::Mod: return "mods";
            case Addons::Kind::ResourcePack: return "resourcepacks";
            case Addons::Kind::Shader: return "shaderpacks";
            default: return {};
        }
    }

    std::string loaderOf(const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr) {
            return {};
        }
        for (const blb::install::loader_info& loader : entry->info.loaders) {
            switch (loader.type) {
                case blb::install::loader_type::forge: return "forge";
                case blb::install::loader_type::neoforge: return "neoforge";
                case blb::install::loader_type::cleanroom: return "forge";
                case blb::install::loader_type::fabric: return "fabric";
                case blb::install::loader_type::legacy_fabric: return "legacy-fabric";
                case blb::install::loader_type::quilt: return "quilt";
                case blb::install::loader_type::liteloader: return "liteloader";
                default: break;
            }
        }
        return {};
    }

    bool installed(const Addons::Version& version, Addons::Kind kind, const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Addons::File* file = version.file();
        std::string sub = folder(kind);
        if (entry == nullptr || file == nullptr || sub.empty()) {
            return false;
        }
        std::error_code error;
        return std::filesystem::exists(entry->gameDirectory / sub / blb::path_from_utf8(file->filename), error);
    }

    void install(const Addons::Version& version, Addons::Kind kind, const std::string& title,
                 const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Addons::File* file = version.file();
        std::string sub = folder(kind);
        if (entry == nullptr || file == nullptr || sub.empty() || state(version.id) == State::Running) {
            return;
        }
        std::shared_ptr<Task> task = begin(version.id);
        std::filesystem::path target = entry->gameDirectory / sub;
        std::string gameVersion = entry->info.minecraft_version;
        std::string loader = loaderOf(instanceKey);
        std::string instanceName = entry->info.id;
        Addons::Version chosen = version;
        run([task, target, gameVersion, loader, chosen, kind, title,
             instanceName](blb::cancellation_token token) -> Continuation {
            std::vector<blb::net::download_task> files;
            std::set<std::string> projects = {chosen.projectId};
            std::set<std::string> names;
            auto add = [&](const Addons::Version& version) {
                const Addons::File* file = version.file();
                if (file != nullptr && names.insert(file->filename).second) {
                    files.push_back(taskFor(*file, target / blb::path_from_utf8(file->filename)));
                }
            };
            add(chosen);
            int dependencies = 0;
            std::vector<Addons::Version> level = {chosen};
            for (int depth = 0; kind == Addons::Kind::Mod && depth < DEPENDENCY_DEPTH && !level.empty(); depth++) {
                std::vector<Addons::Version> next;
                for (const Addons::Version& version : level) {
                    for (const Addons::Dependency& dependency : version.dependencies) {
                        if (!dependency.required ||
                            (!dependency.projectId.empty() && !projects.insert(dependency.projectId).second)) {
                            continue;
                        }
                        Addons::Fetched fetched = !dependency.versionId.empty()
                                ? Addons::fetchVersion(version.source, dependency.versionId)
                                : Addons::fetchProjectVersions(version.source, dependency.projectId, gameVersion,
                                                               loader);
                        if (token.is_cancelled()) {
                            return nullptr;
                        }
                        if (!fetched.ok || fetched.versions.empty()) {
                            continue;
                        }
                        const Addons::Version& found = fetched.versions.front();
                        const Addons::File* file = found.file();
                        std::error_code error;
                        if (file == nullptr ||
                            std::filesystem::exists(target / blb::path_from_utf8(file->filename), error)) {
                            continue;
                        }
                        add(found);
                        dependencies++;
                        next.push_back(found);
                    }
                }
                level = std::move(next);
            }
            std::error_code error;
            std::filesystem::create_directories(target, error);
            std::string problem = download(files, task, token);
            if (token.is_cancelled()) {
                return nullptr;
            }
            return [task, problem, title, instanceName, dependencies] {
                if (!problem.empty()) {
                    finish(task, false, "无法安装 " + title + "：" + problem);
                    return;
                }
                std::string extra =
                        dependencies > 0 ? "（含 " + std::to_string(dependencies) + " 个前置）" : std::string();
                finish(task, true, "已将 " + title + " 安装到 " + instanceName + extra);
            };
        });
    }

    void saveTo(const Addons::Version& version, const std::filesystem::path& folder, const std::string& title) {
        const Addons::File* file = version.file();
        if (file == nullptr || state(version.id) == State::Running) {
            return;
        }
        std::shared_ptr<Task> task = begin(version.id);
        std::vector<blb::net::download_task> files = {taskFor(*file, folder / blb::path_from_utf8(file->filename))};
        std::string where = Platform::display(folder);
        run([task, files, title, where](blb::cancellation_token token) -> Continuation {
            std::string problem = download(files, task, token);
            if (token.is_cancelled()) {
                return nullptr;
            }
            return [task, problem, title, where] {
                finish(task, problem.empty(),
                       problem.empty() ? "已将 " + title + " 保存到 " + where : "无法下载 " + title + "：" + problem);
            };
        });
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
    }

    void shutdown() {
        stopping.cancel();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }
}

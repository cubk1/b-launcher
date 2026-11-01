#include "app/downloads/ProjectInstaller.h"
#include "app/system/TaskThreads.h"
#include "app/instances/Instances.h"
#include "app/system/Platform.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
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
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>

namespace ProjectInstaller {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = std::function<void()>;

        constexpr size_t DEPENDENCY_LIMIT = 512;

        struct Task {
            std::atomic<int> state{(int) State::Idle};
            std::atomic<float> fraction{-1.0f};
        };

        std::mutex lock;
        std::deque<Continuation> finished;
        TaskThreads workers;
        blb::cancellation_source stopping;
        std::map<std::string, std::shared_ptr<Task>> tasks;

        void run(const std::shared_ptr<Task>& task, std::function<Continuation(blb::cancellation_token)> work) {
            blb::cancellation_token token = stopping.token();
            workers.start([work, token, task] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    task->state = (int) State::Failed;
                    std::string message = error.what();
                    next = [message] { Notices::add(Notices::Kind::Failed, "下载失败：" + message, 5000); };
                } catch (...) {
                    task->state = (int) State::Failed;
                    next = [] { Notices::add(Notices::Kind::Failed, "下载失败，请重试", 5000); };
                }
                if (!next) task->state = (int) State::Failed;
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

        bool safeFilename(std::string_view name) {
            if (!blb::fs::safe_relative(name) || name == "." ||
                name.find_first_of("/\\*?\"<>|") != std::string_view::npos ||
                name.back() == '.' || name.back() == ' ') {
                return false;
            }
            for (unsigned char c : name) {
                if (c < 32 || c == 127) return false;
            }
            auto stem = blb::to_lower(name.substr(0, name.find('.')));
            if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
                stem == "conin$" || stem == "conout$") return false;
            return !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
                     stem[3] >= '1' && stem[3] <= '9');
        }

        blb::net::download_task taskFor(const Addons::File& file, const std::filesystem::path& directory) {
            if (!safeFilename(file.filename) || directory.empty()) {
                throw std::runtime_error("下载文件名或保存目录无效");
            }
            auto target = directory / blb::path_from_utf8(file.filename);
            if (!blb::fs::is_inside(directory, target) || blb::fs::stat(target).is_link) {
                throw std::runtime_error("下载路径超出保存目录或指向链接");
            }
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
            Notices::add(ok ? Notices::Kind::Done : Notices::Kind::Failed, text, ok ? 3000 : 5000);
        }

        blb::result<std::vector<Addons::Version>> resolveDependencies(
                const Addons::Version& root, const std::string& gameVersion,
                const std::string& loader, blb::cancellation_token token) {
            using Key = std::pair<Addons::Source, std::string>;
            if (root.id.empty() || root.projectId.empty()) {
                return blb::make_error(blb::errc::parse, "模组缺少项目或版本信息");
            }
            Key rootKey{root.source, root.projectId};
            std::map<Key, Addons::Version> selected{{rootKey, root}};
            std::map<Key, Addons::Version> cached{{{root.source, root.id}, root}};
            std::map<Key, blb::error> unavailable;
            auto fetch = [&](Addons::Source source, const std::string& project,
                             const std::string& version) -> blb::result<Addons::Version> {
                BLB_TRY(token.check());
                Key lookup{source, version.empty() ? "project:" + project : "version:" + version};
                if (auto failed = unavailable.find(lookup); failed != unavailable.end()) return failed->second;
                auto found = cached.find({source, version});
                if (version.empty() || found == cached.end()) {
                    auto response = version.empty()
                            ? Addons::fetchProjectVersions(source, project, gameVersion, loader, token)
                            : Addons::fetchVersion(source, version, token);
                    BLB_TRY(token.check());
                    if (!response.ok || response.versions.empty()) {
                        auto error = blb::make_error(blb::errc::network, "无法获取必需前置 " +
                                (version.empty() ? project : version) + "：" +
                                (response.error.empty() ? "没有兼容版本" : response.error));
                        unavailable.emplace(lookup, error);
                        return error;
                    }
                    auto value = std::move(response.versions.front());
                    if (value.id.empty() || value.projectId.empty() ||
                        (!version.empty() && value.id != version)) {
                        return blb::make_error(blb::errc::parse, "前置返回的版本信息无效");
                    }
                    value.source = source;
                    found = cached.insert_or_assign(Key{source, value.id}, value).first;
                }
                if (!project.empty() && found->second.projectId != project) {
                    return blb::make_error(blb::errc::parse, "前置版本不属于指定的项目");
                }
                return found->second;
            };

            for (size_t round = 0; round < DEPENDENCY_LIMIT; ++round) {
                BLB_TRY(token.check());
                size_t knownBefore = selected.size();
                std::map<Key, std::set<std::string>> pins{{rootKey, {root.id}}};
                std::set<Key> visited;
                std::vector<Key> queue{rootKey};
                std::vector<Addons::Version> reached;
                std::optional<blb::error> failure;
                for (size_t i = 0; i < queue.size(); ++i) {
                    Key key = queue[i];
                    if (!visited.insert(key).second) continue;
                    const auto& value = selected.at(key);
                    reached.push_back(value);
                    for (const auto& dependency : value.dependencies) {
                        if (!dependency.required) continue;
                        std::string project = dependency.projectId;
                        if (project.empty()) {
                            if (dependency.versionId.empty()) {
                                return blb::make_error(blb::errc::parse, "必需前置缺少项目或版本信息");
                            }
                            auto exact = fetch(value.source, {}, dependency.versionId);
                            if (!exact) {
                                if (exact.err().code == blb::errc::cancelled) return exact.err();
                                failure = exact.err();
                                continue;
                            }
                            project = exact->projectId;
                        }
                        Key next{value.source, project};
                        if (!dependency.versionId.empty()) pins[next].insert(dependency.versionId);
                        if (!selected.contains(next)) {
                            if (selected.size() >= DEPENDENCY_LIMIT) {
                                return blb::make_error(blb::errc::parse, "依赖关系过多，无法完成安装");
                            }
                            auto candidate = fetch(value.source, project, dependency.versionId);
                            if (!candidate) {
                                if (candidate.err().code == blb::errc::cancelled) return candidate.err();
                                failure = candidate.err();
                                continue;
                            }
                            selected.emplace(next, std::move(*candidate));
                        }
                        queue.push_back(next);
                    }
                }
                bool changed = false;
                for (const auto& [key, versions] : pins) {
                    if (versions.size() != 1) continue;
                    auto current = selected.find(key);
                    if (current != selected.end() && current->second.id == *versions.begin()) continue;
                    auto exact = fetch(key.first, key.second, *versions.begin());
                    if (!exact) {
                        if (exact.err().code == blb::errc::cancelled) return exact.err();
                        failure = exact.err();
                        continue;
                    }
                    selected[key] = std::move(*exact);
                    changed = true;
                }
                // A pin can replace an unconstrained choice. Rebuild reachable
                // dependencies before judging conflicts or downloading files,
                // since dependencies of the replaced version no longer apply.
                if (changed || (failure && selected.size() != knownBefore)) continue;
                for (const auto& [key, versions] : pins) {
                    if (versions.size() > 1) {
                        std::vector<std::string> ids(versions.begin(), versions.end());
                        return blb::make_error(blb::errc::incompatible,
                                "前置版本冲突：" + key.second + " 同时要求 " + blb::join(ids, "、"));
                    }
                }
                if (failure) return *failure;
                return reached;
            }
            return blb::make_error(blb::errc::incompatible, "依赖版本约束无法满足");
        }
    }

    bool busy() {
        for (const auto& [versionId, task] : tasks) {
            if (task->state.load() == (int) State::Running) {
                return true;
            }
        }
        return false;
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
        if (entry == nullptr || file == nullptr || sub.empty() || !safeFilename(file->filename)) {
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
        run(task, [task, target, gameVersion, loader, chosen, kind, title,
             instanceName](blb::cancellation_token token) -> Continuation {
            std::vector<blb::net::download_task> files;
            std::set<std::string> names;
            auto add = [&](const Addons::Version& version) {
                const Addons::File* file = version.file();
                if (file == nullptr || file->url.empty()) {
                    throw std::runtime_error("模组文件信息无效");
                }
                if (names.insert(file->filename).second) {
                    files.push_back(taskFor(*file, target));
                }
            };
            add(chosen);
            int dependencies = 0;
            if (kind == Addons::Kind::Mod) {
                auto resolved = resolveDependencies(chosen, gameVersion, loader, token);
                if (!resolved) {
                    if (resolved.err().code == blb::errc::cancelled) return nullptr;
                    throw std::runtime_error(resolved.err().message);
                }
                for (size_t i = 1; i < resolved->size(); ++i) {
                    const auto& found = (*resolved)[i];
                    const auto* file = found.file();
                    if (file == nullptr || !safeFilename(file->filename)) {
                        throw std::runtime_error("必需前置没有有效的下载文件");
                    }
                    std::error_code error;
                    if (!std::filesystem::exists(target / blb::path_from_utf8(file->filename), error)) {
                        add(found);
                        dependencies++;
                    }
                }
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
        Addons::File chosen = *file;
        std::string where = Platform::display(folder);
        run(task, [task, chosen, folder, title, where](blb::cancellation_token token) -> Continuation {
            // Construct and validate the target inside the task so rejection
            // also clears Running and reports an ordinary download failure.
            std::vector<blb::net::download_task> files = {taskFor(chosen, folder)};
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
        workers.reap();
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
        workers.joinAll();
    }
}

#include "app/downloads/ProjectInstaller.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"
#include "app/instances/Instances.h"
#include "app/system/Platform.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/base/win_util.hpp>
#include <core/base/zip.hpp>
#include <core/installer/game_installer.hpp>
#include <core/meta/game_layout.hpp>
#include <core/modpack/modpack.hpp>
#include <core/network/download_client.hpp>
#include <core/network/mirror_provider.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>

namespace ProjectInstaller {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr size_t DEPENDENCY_LIMIT = 512;

        struct Task {
            std::atomic<int> state{(int) State::Idle};
            std::atomic<float> fraction{-1.0f};
        };

        AsyncTasks background;
        using TaskKey = std::pair<std::string, std::filesystem::path>;
        std::map<TaskKey, std::shared_ptr<Task>> tasks;

        std::filesystem::path worldDirectory(const Instances::Entry& entry) {
            if (entry.gameDirectory.empty()) return {};
            // Resolve the game directory even when saves has not been created yet.
            return blb::fs::real_path(blb::fs::real_path(entry.gameDirectory) / "saves");
        }

        std::shared_ptr<Task> findTask(const std::string& versionId, const std::string& instanceKey) {
            std::filesystem::path directory;
            if (!instanceKey.empty()) {
                const auto* entry = Instances::find(instanceKey);
                if (entry == nullptr) return {};
                directory = worldDirectory(*entry);
                if (directory.empty()) return {};
            }
            auto found = tasks.find({versionId, directory});
            return found == tasks.end() ? nullptr : found->second;
        }

        struct CheckedFile {
            uint64_t size = 0;
            int64_t modified = 0;
            std::optional<blb::sha1_digest> digest;
            bool ready = false;
        };
        std::map<std::string, std::shared_ptr<CheckedFile>> checkedFiles;
        Recent checkedUse;

        bool matches(const Addons::File& file, const std::filesystem::path& path) {
            auto stat = blb::fs::stat(path);
            if (!stat.exists || stat.is_directory ||
                (file.size >= 0 && stat.size != (uint64_t) file.size)) return false;
            if (file.sha1.empty()) return true;
            auto expected = blb::parse_sha1(file.sha1);
            if (!expected) return false;
            std::string key = blb::path_to_utf8(blb::fs::absolute(path));
            checkedUse.use(key);
            auto& checked = checkedFiles[key];
            if (!checked || checked->size != stat.size || checked->modified != stat.mtime) {
                checked = std::make_shared<CheckedFile>();
                checked->size = stat.size;
                checked->modified = stat.mtime;
                background.run([path, checked](blb::cancellation_token token) -> Continuation {
                    std::optional<blb::sha1_digest> digest;
                    try {
                        if (auto read = blb::sha1_file(path, token)) {
                            digest = *read;
                        }
                        auto now = blb::fs::stat(path);
                        if (!now.exists || now.size != checked->size || now.mtime != checked->modified) {
                            digest.reset();
                        }
                    } catch (const std::exception&) {
                        digest.reset();
                    }
                    return [checked, digest] {
                        checked->digest = digest;
                        checked->ready = true;
                    };
                }, {});
            }
            return checked->digest && *checked->digest == *expected;
        }

        void run(const std::shared_ptr<Task>& task, std::function<Continuation(blb::cancellation_token)> work) {
            background.run([work = std::move(work), task](blb::cancellation_token token) -> Continuation {
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
                return next;
            }, {});
        }

        std::shared_ptr<Task> begin(const std::string& versionId, const std::filesystem::path& directory = {}) {
            std::shared_ptr<Task>& task = tasks[{versionId, directory}];
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

        blb::net::download_task taskFor(const Addons::File& file, const std::filesystem::path& directory) {
            if (!blb::fs::safe_filename(file.filename) || directory.empty()) {
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

        bool junk(const std::string& name) {
            return name.starts_with("__MACOSX/");
        }

        bool validUtf8(std::string_view text) {
            return text.empty() ||
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int) text.size(), nullptr, 0) > 0;
        }

        bool formatCode(char c) {
            char lower = (char) std::tolower((unsigned char) c);
            return (lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'f') || (lower >= 'k' && lower <= 'o') ||
                    lower == 'r' || lower == 'x';
        }

        UINT legacyPage(const std::string& name) {
            for (size_t i = 0; i + 1 < name.size(); i++) {
                if (!formatCode(name[i + 1])) {
                    continue;
                }
                switch ((unsigned char) name[i]) {
                    case 0x15: return 437;
                    case 0xF5: return 850;
                    case 0xA7: return 1252;
                    default: break;
                }
            }
            return GetOEMCP();
        }

        std::string fromCodePage(const std::string& bytes, UINT page) {
            if (bytes.empty()) {
                return {};
            }
            DWORD flags = page == 437 ? MB_USEGLYPHCHARS : 0;
            int wide = MultiByteToWideChar(page, flags, bytes.data(), (int) bytes.size(), nullptr, 0);
            if (wide <= 0) {
                return bytes;
            }
            std::wstring buffer((size_t) wide, L'\0');
            MultiByteToWideChar(page, flags, bytes.data(), (int) bytes.size(), buffer.data(), wide);
            return blb::detail::wide_to_utf8(buffer);
        }

        std::string entryName(const std::string& raw) {
            std::string name = validUtf8(raw) ? raw : fromCodePage(raw, legacyPage(raw));
            std::replace(name.begin(), name.end(), '\\', '/');
            return name;
        }

        std::vector<std::string> worldRoots(const std::vector<blb::zip_entry>& entries) {
            std::vector<std::string> found;
            for (const blb::zip_entry& entry : entries) {
                std::string name = entryName(entry.name);
                if (entry.directory || junk(name)) {
                    continue;
                }
                size_t slash = name.rfind('/');
                if ((slash == std::string::npos ? name : name.substr(slash + 1)) == "level.dat") {
                    found.push_back(slash == std::string::npos ? std::string() : name.substr(0, slash + 1));
                }
            }
            std::sort(found.begin(), found.end(),
                      [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
            std::vector<std::string> roots;
            for (const std::string& root : found) {
                if (std::none_of(roots.begin(), roots.end(),
                                 [&](const std::string& outer) { return root.starts_with(outer); })) {
                    roots.push_back(root);
                }
            }
            return roots;
        }

        std::string worldName(const std::string& root, const std::string& fallback) {
            std::string name = root.empty() ? fallback : root.substr(0, root.size() - 1);
            size_t slash = name.rfind('/');
            if (slash != std::string::npos) {
                name = name.substr(slash + 1);
            }
            name = std::string(blb::trim(blb::fs::filter_filename(blb::strip_formatting(name))));
            size_t last = name.find_last_not_of(". ");
            name.resize(last == std::string::npos ? 0 : last + 1);
            return blb::fs::safe_filename(name) ? name : std::string("World");
        }

        // create must claim the destination without overwriting it. An existence
        // check followed by a write lets concurrent world installs share a path.
        template <class Create>
        std::filesystem::path createUniquePath(const std::filesystem::path& folder, const std::string& stem,
                                               const std::string& extension, Create create) {
            for (uint64_t i = 1;; ++i) {
                std::string suffix = i == 1 ? std::string() : " (" + std::to_string(i) + ")";
                auto target = folder / blb::path_from_utf8(stem + suffix + extension);
                std::error_code error;
                if (create(target, error)) return target;
                if (error && error != std::errc::file_exists) {
                    throw std::filesystem::filesystem_error("无法创建保存路径", target, error);
                }
            }
        }

        std::filesystem::path createWorldDirectory(const std::filesystem::path& parent, const std::string& name) {
            return createUniquePath(parent, name, {}, [](const auto& target, auto& error) {
                return std::filesystem::create_directory(target, error);
            });
        }

        std::vector<std::string> extractWorlds(blb::zip_reader& zip, const std::filesystem::path& saves,
                                               const std::string& title, blb::cancellation_token token) {
            auto entries = zip.entries();
            if (!entries) {
                throw std::runtime_error(entries.err().message);
            }
            std::vector<std::string> roots = worldRoots(*entries);
            if (roots.empty()) {
                throw std::runtime_error("压缩包里没有找到存档（缺少 level.dat）");
            }
            std::vector<std::string> names;
            for (const std::string& root : roots) {
                std::filesystem::path folder = createWorldDirectory(saves, worldName(root, title));
                std::optional<blb::error> failure;
                auto visited = zip.for_each([&](const blb::zip_entry& entry, blb::zip_reader& reader) {
                    if (token.is_cancelled()) {
                        failure = blb::make_error(blb::errc::cancelled, "cancelled");
                        return false;
                    }
                    std::string name = entryName(entry.name);
                    if (entry.directory || name.ends_with('/') || junk(name) || !name.starts_with(root)) {
                        return true;
                    }
                    std::string relative = name.substr(root.size());
                    std::filesystem::path target = folder / blb::path_from_utf8(relative);
                    if (relative.empty() || !blb::fs::safe_relative(relative) || !blb::fs::is_inside(folder, target)) {
                        return true;
                    }
                    if (auto done = reader.extract_current(target); !done) {
                        failure = done.err();
                        return false;
                    }
                    return true;
                });
                if (!visited && !failure) {
                    failure = visited.err();
                }
                if (failure) {
                    (void) blb::fs::remove_all(folder);
                    if (failure->code == blb::errc::cancelled) {
                        return {};
                    }
                    throw std::runtime_error(failure->message);
                }
                names.push_back(blb::path_to_utf8(folder.filename()));
            }
            return names;
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
        for (const auto& [key, task] : tasks) {
            if (task->state.load() == (int) State::Running) {
                return true;
            }
        }
        return false;
    }

    State state(const std::string& versionId, const std::string& instanceKey) {
        auto task = findTask(versionId, instanceKey);
        return task ? (State) task->state.load() : State::Idle;
    }

    float progress(const std::string& versionId, const std::string& instanceKey) {
        auto task = findTask(versionId, instanceKey);
        return task ? task->fraction.load() : -1.0f;
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
        if (entry == nullptr || file == nullptr || sub.empty() || !blb::fs::safe_filename(file->filename)) {
            return false;
        }
        return matches(*file, entry->gameDirectory / sub / blb::path_from_utf8(file->filename));
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
            std::vector<std::wstring> names;
            auto add = [&](const Addons::Version& version) {
                const Addons::File* file = version.file();
                if (file == nullptr || file->url.empty()) {
                    throw std::runtime_error("模组文件信息无效");
                }
                auto task = taskFor(*file, target);
                auto name = task.target.filename().wstring();
                if (std::any_of(names.begin(), names.end(), [&](const auto& existing) {
                        return CompareStringOrdinal(existing.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL;
                    })) {
                    throw std::runtime_error("模组文件名冲突：" + file->filename);
                }
                names.push_back(std::move(name));
                files.push_back(std::move(task));
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
                    if (file == nullptr || !blb::fs::safe_filename(file->filename)) {
                        throw std::runtime_error("必需前置没有有效的下载文件");
                    }
                    // The downloader verifies size and hash before reusing an existing file.
                    add(found);
                    dependencies++;
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

    void installWorld(const Addons::Version& version, const std::string& title, const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        const Addons::File* file = version.file();
        if (entry == nullptr || file == nullptr || state(version.id, instanceKey) == State::Running) {
            return;
        }
        std::filesystem::path saves = worldDirectory(*entry);
        if (saves.empty()) return;
        std::shared_ptr<Task> task = begin(version.id, saves);
        std::string instanceName = entry->info.id;
        Addons::File chosen = *file;
        run(task, [task, chosen, saves, title, instanceName](blb::cancellation_token token) -> Continuation {
            auto temporary = std::filesystem::temp_directory_path() / "b-launcher";
            if (auto made = blb::fs::create_directories(temporary); !made) {
                throw std::runtime_error(made.err().message);
            }
            auto scratch = createWorldDirectory(temporary, "world");
            struct Cleanup {
                const std::filesystem::path& directory;
                ~Cleanup() { (void) blb::fs::remove_all(directory); }
            } cleanup{scratch};
            std::vector<blb::net::download_task> files = {taskFor(chosen, scratch)};
            std::filesystem::path archive = files.front().target;
            std::string problem = download(files, task, token);
            if (token.is_cancelled()) {
                return nullptr;
            }
            if (!problem.empty()) {
                return [task, title, problem] { finish(task, false, "无法下载 " + title + "：" + problem); };
            }
            std::vector<std::string> names;
            std::string failure;
            std::string kept;
            if (auto made = blb::fs::create_directories(saves); !made) {
                failure = made.err().message;
            } else if (auto opened = blb::zip_reader::open(archive); opened) {
                try {
                    names = extractWorlds(*opened, saves, title, token);
                } catch (const std::exception& exception) {
                    failure = exception.what();
                }
            } else {
                std::string stem = blb::path_to_utf8(archive.stem());
                std::string extension = blb::path_to_utf8(archive.extension());
                try {
                    auto copy = createUniquePath(saves, stem, extension, [&](const auto& target, auto& error) {
                        return std::filesystem::copy_file(archive, target, std::filesystem::copy_options::none, error);
                    });
                    kept = blb::path_to_utf8(copy.filename());
                } catch (const std::exception& exception) {
                    failure = exception.what();
                }
            }
            if (token.is_cancelled()) {
                return nullptr;
            }
            return [task, title, instanceName, names, failure, kept] {
                if (!failure.empty()) {
                    finish(task, false, "无法安装 " + title + "：" + failure);
                } else if (!kept.empty()) {
                    finish(task, true,
                           title + " 不是 zip 压缩包，已把 " + kept + " 放进 " + instanceName +
                                   " 的 saves 文件夹，请手动解压");
                } else {
                    finish(task, true,
                           "已将 " + title + " 安装到 " + instanceName + "，存档：" + blb::join(names, "、"));
                }
            };
        });
    }

    void poll() {
        background.poll();
        checkedUse.trim(checkedFiles, 128, [](const auto& file) { return file->ready; });
    }

    void shutdown() {
        background.stop();
    }
}

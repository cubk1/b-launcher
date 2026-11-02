#include "app/instances/ModManager.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Instances.h"
#include "app/instances/Mods.h"
#include "app/launch/Game.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/json.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/modpack/mods.hpp>
#include <core/network/download_client.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

namespace ModManager {

    namespace blb = b_launcher_base;

    namespace {
        using json = nlohmann::json;
        using Continuation = AsyncTasks::Continuation;
        using Clock = std::chrono::steady_clock;

        const char* const API = "https://api.modrinth.com/v2";
        const char* const API_MIRROR = "https://mod.mcimirror.top/modrinth/v2";
        constexpr size_t BATCH = 80;
        const char* const BUILTIN[] = {"minecraft", "java",     "fabricloader", "fabric-loader", "quilt_loader",
                                       "forge",     "neoforge", "javafml",      "lowcodefml",    "mixinextras"};

        struct Shared {
            std::mutex lock;
            Phase phase = Phase::Downloading;
            float fraction = -1.0f;
        };

        struct Instance {
            int serial = -1;
            int revision = 0;
            std::tuple<int, int, int> requested{-1, -1, -1};
            bool running = false;
            std::string error;
            std::vector<Change> changes;
            std::set<std::string> adding;
            bool busy = false;
            Progress progress;
            std::shared_ptr<Shared> shared;
            std::shared_ptr<blb::cancellation_source> cancel;
        };

        struct Step {
            std::filesystem::path old;
            bool disabled = false;
            Addons::File file;
            std::string name;
        };

        struct Outcome {
            bool ok = false;
            bool cancelled = false;
            std::string error;
            std::vector<std::string> problems;
        };

        AsyncTasks tasks;
        AsyncTasks applying;
        std::map<std::string, Instance> instances;
        std::map<std::string, std::string> hashes;
        std::map<std::string, Remote> remotes;
        std::set<std::string> unmatched;
        std::map<std::string, Project> projects;
        std::set<std::string> fetchingProjects;
        std::map<std::string, Clock::time_point> projectRetry;
        std::map<std::string, Addons::Versions> versionLists;
        int remotesRevision = 0;
        const std::vector<Change> NO_CHANGES;
        const Progress IDLE;

        std::string stem(const std::filesystem::path& file) {
            std::string name = blb::path_to_utf8(file);
            const std::string disabled = ".disabled";
            if (name.size() > disabled.size() &&
                blb::to_lower(name.substr(name.size() - disabled.size())) == disabled) {
                name.resize(name.size() - disabled.size());
            }
            return name;
        }

        std::string fileKey(const blb::modpack::mod_info& info) {
            return stem(info.file) + "#" + std::to_string(info.size);
        }

        const Mods::Mod* modAt(const Mods::List& list, const std::filesystem::path& file) {
            std::string wanted = stem(file);
            for (const Mods::Mod& mod : list.mods) {
                if (stem(mod.info.file) == wanted) {
                    return &mod;
                }
            }
            return nullptr;
        }

        std::string loaderOf(const std::string& key) {
            return ProjectInstaller::loaderOf(key);
        }

        bool fits(const Addons::Version& version, const std::string& loader) {
            if (loader.empty() || version.loaders.empty()) {
                return true;
            }
            for (const std::string& tag : version.loaders) {
                if (tag == loader || (loader == "quilt" && tag == "fabric")) {
                    return true;
                }
            }
            return false;
        }

        blb::result<json> request(const std::string& method, const std::string& path, const std::string& body,
                                  blb::cancellation_token token) {
            std::string problem = "无法连接 Modrinth";
            for (const char* base : {API, API_MIRROR}) {
                blb::net::http_request call;
                call.method = method;
                call.url = std::string(base) + path;
                call.headers.push_back({"User-Agent", Platform::USER_AGENT});
                call.headers.push_back({"Accept", "application/json"});
                if (!body.empty()) {
                    call.headers.push_back({"Content-Type", "application/json"});
                    call.body = body;
                }
                call.timeout = std::chrono::seconds(30);
                auto response = blb::net::http_send(call, token);
                if (!response) {
                    if (response.err().code == blb::errc::cancelled) {
                        return response.err();
                    }
                    problem = response.err().message;
                    continue;
                }
                if (!response->ok()) {
                    problem = "Modrinth 返回 " + std::to_string(response->status);
                    continue;
                }
                json doc = json::parse(response->body, nullptr, false);
                if (doc.is_discarded()) {
                    problem = "Modrinth 返回的内容无效";
                    continue;
                }
                return doc;
            }
            return blb::make_error(blb::errc::network, problem);
        }

        void ensureProjects(const std::set<std::string>& ids) {
            auto now = Clock::now();
            std::vector<std::string> wanted;
            for (const std::string& id : ids) {
                if (id.empty() || projects.count(id) != 0 || fetchingProjects.count(id) != 0) {
                    continue;
                }
                auto retry = projectRetry.find(id);
                if (retry != projectRetry.end() && retry->second > now) {
                    continue;
                }
                wanted.push_back(id);
            }
            if (wanted.empty()) {
                return;
            }
            for (const std::string& id : wanted) {
                fetchingProjects.insert(id);
            }
            tasks.run(
                    [wanted](blb::cancellation_token token) -> Continuation {
                        std::vector<Project> found;
                        std::string error;
                        for (size_t start = 0; start < wanted.size(); start += BATCH) {
                            json ids = json::array();
                            for (size_t i = start; i < std::min(wanted.size(), start + BATCH); i++) {
                                ids.push_back(wanted[i]);
                            }
                            auto doc = request("GET", "/projects?ids=" + blb::url_encode(ids.dump()), {}, token);
                            if (!doc) {
                                error = doc.err().message;
                                break;
                            }
                            if (!doc->is_array()) {
                                continue;
                            }
                            for (const json& item : *doc) {
                                Project project;
                                project.id = blb::json_text(item, "id");
                                project.slug = blb::json_text(item, "slug");
                                project.title = blb::json_text(item, "title");
                                project.iconUrl = blb::json_text(item, "icon_url");
                                if (!project.id.empty()) {
                                    found.push_back(std::move(project));
                                }
                            }
                        }
                        return [wanted, found, error] {
                            for (const std::string& id : wanted) {
                                fetchingProjects.erase(id);
                            }
                            for (const Project& project : found) {
                                projects[project.id] = project;
                            }
                            for (const std::string& id : wanted) {
                                if (projects.count(id) != 0) {
                                    continue;
                                }
                                if (error.empty()) {
                                    projects[id] = Project{id, {}, {}, {}};
                                } else {
                                    projectRetry[id] = Clock::now() + std::chrono::seconds(30);
                                }
                            }
                        };
                    },
                    [wanted](const std::string&) -> Continuation {
                        return [wanted] {
                            for (const std::string& id : wanted) {
                                fetchingProjects.erase(id);
                                projectRetry[id] = Clock::now() + std::chrono::seconds(30);
                            }
                        };
                    });
        }

        void identify(const std::string& key, Instance& state, const Mods::List& list) {
            state.serial = list.serial;
            std::vector<std::pair<std::string, std::filesystem::path>> needHash;
            std::vector<std::string> needLookup;
            for (const Mods::Mod& mod : list.mods) {
                std::string id = fileKey(mod.info);
                auto known = hashes.find(id);
                if (known == hashes.end()) {
                    needHash.push_back({id, mod.info.file});
                } else if (remotes.count(known->second) == 0 && unmatched.count(known->second) == 0) {
                    needLookup.push_back(known->second);
                }
            }
            if (needHash.empty() && needLookup.empty()) {
                return;
            }
            state.running = true;
            state.error.clear();
            tasks.run(
                    [key, needHash, needLookup](blb::cancellation_token token) -> Continuation {
                        std::map<std::string, std::string> hashed;
                        for (const auto& [id, path] : needHash) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
                            auto digest = blb::sha1_file(path, token);
                            if (digest) {
                                hashed[id] = blb::to_hex(*digest);
                            }
                        }
                        std::vector<std::string> lookup = needLookup;
                        for (const auto& item : hashed) {
                            lookup.push_back(item.second);
                        }
                        std::sort(lookup.begin(), lookup.end());
                        lookup.erase(std::unique(lookup.begin(), lookup.end()), lookup.end());
                        std::map<std::string, Remote> found;
                        std::string error;
                        for (size_t start = 0; start < lookup.size(); start += BATCH) {
                            json body = {{"algorithm", "sha1"}, {"hashes", json::array()}};
                            for (size_t i = start; i < std::min(lookup.size(), start + BATCH); i++) {
                                body["hashes"].push_back(lookup[i]);
                            }
                            auto doc = request("POST", "/version_files", body.dump(), token);
                            if (!doc) {
                                error = doc.err().message;
                                break;
                            }
                            if (!doc->is_object()) {
                                continue;
                            }
                            for (auto item = doc->begin(); item != doc->end(); ++item) {
                                Addons::Version version = Modrinth::versionFrom(item.value());
                                if (version.projectId.empty()) {
                                    continue;
                                }
                                found[item.key()] =
                                        Remote{version.projectId, version.id, version.number, version.dependencies};
                            }
                        }
                        return [key, hashed, lookup, found, error] {
                            for (const auto& item : hashed) {
                                hashes[item.first] = item.second;
                            }
                            for (const auto& item : found) {
                                remotes[item.first] = item.second;
                            }
                            remotesRevision++;
                            if (error.empty()) {
                                for (const std::string& hash : lookup) {
                                    if (found.count(hash) == 0) {
                                        unmatched.insert(hash);
                                    }
                                }
                            }
                            Instance& state = instances[key];
                            state.running = false;
                            state.error = error;
                            if (Mods::of(key).serial != state.serial && error.empty()) {
                                state.serial = -1;
                            }
                        };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            Instance& state = instances[key];
                            state.running = false;
                            state.error = error;
                        };
                    });
        }

        void prune(Instance& state, const Mods::List& list) {
            auto& changes = state.changes;
            for (auto it = changes.begin(); it != changes.end();) {
                if (it->file.empty()) {
                    ++it;
                    continue;
                }
                const Mods::Mod* mod = modAt(list, it->file);
                if (mod == nullptr) {
                    it = changes.erase(it);
                    continue;
                }
                it->file = mod->info.file;
                it->disabled = !mod->info.enabled;
                ++it;
            }
        }

        Instance& sync(const std::string& key) {
            Instance& state = instances[key];
            const Mods::List& list = Mods::of(key);
            if (list.loading || state.busy) {
                return state;
            }
            prune(state, list);
            if (!state.running && list.serial != state.serial) {
                identify(key, state, list);
            }
            std::tuple<int, int, int> stamp{list.serial, state.revision, remotesRevision};
            if (stamp == state.requested) {
                return state;
            }
            state.requested = stamp;
            std::set<std::string> wanted;
            for (const Mods::Mod& mod : list.mods) {
                if (const Remote* found = remote(mod)) {
                    wanted.insert(found->projectId);
                    for (const Addons::Dependency& dependency : found->dependencies) {
                        wanted.insert(dependency.projectId);
                    }
                }
            }
            for (const Change& change : state.changes) {
                wanted.insert(change.projectId);
                for (const Addons::Dependency& dependency : change.target.dependencies) {
                    wanted.insert(dependency.projectId);
                }
            }
            ensureProjects(wanted);
            return state;
        }

        void fetchVersions(const std::string& key, const std::string& projectId) {
            std::string id = key + "|" + projectId;
            Addons::Versions& entry = versionLists[id];
            entry.status = Addons::Status::Loading;
            entry.error.clear();
            std::string game = gameVersion(key);
            std::string loader = loaderOf(key);
            tasks.run(
                    [id, projectId, game, loader](blb::cancellation_token token) -> Continuation {
                        Addons::Fetched fetched = Modrinth::fetchProjectVersions(projectId, game, "", token);
                        std::vector<Addons::Version> kept;
                        for (const Addons::Version& version : fetched.versions) {
                            if (fits(version, loader)) {
                                kept.push_back(version);
                            }
                        }
                        return [id, ok = fetched.ok, error = fetched.error, kept] {
                            Addons::Versions& entry = versionLists[id];
                            entry.status = ok ? Addons::Status::Ready : Addons::Status::Failed;
                            entry.error = error;
                            entry.list = kept;
                        };
                    },
                    [id](const std::string& error) -> Continuation {
                        return [id, error] {
                            Addons::Versions& entry = versionLists[id];
                            entry.status = Addons::Status::Failed;
                            entry.error = error;
                        };
                    });
        }

        std::set<std::string> presentIds(const Mods::List& list) {
            std::set<std::string> ids(std::begin(BUILTIN), std::end(BUILTIN));
            for (const Mods::Mod& mod : list.mods) {
                if (!mod.info.enabled) {
                    continue;
                }
                ids.insert(mod.info.id);
                ids.insert(mod.info.provides.begin(), mod.info.provides.end());
            }
            return ids;
        }

        class Sink final : public blb::progress_sink {
        public:
            explicit Sink(std::shared_ptr<Shared> shared) : shared(std::move(shared)) {}

            void on_progress(const blb::progress_event& event) override {
                if (event.total_bytes == 0) {
                    return;
                }
                std::lock_guard<std::mutex> guard(shared->lock);
                shared->fraction = (float) std::min(1.0, (double) event.done_bytes / (double) event.total_bytes);
            }

        private:
            std::shared_ptr<Shared> shared;
        };

        void enter(const std::shared_ptr<Shared>& shared, Phase phase) {
            std::lock_guard<std::mutex> guard(shared->lock);
            shared->phase = phase;
            shared->fraction = -1.0f;
        }

        std::map<std::string, std::string> rangeIssues(const std::vector<blb::modpack::mod_info>& mods) {
            std::map<std::string, std::string> found;
            for (const blb::modpack::mod_info& mod : mods) {
                if (!mod.enabled) {
                    continue;
                }
                for (const blb::modpack::mod_dependency& dependency : mod.dependencies) {
                    if (!dependency.required || dependency.version.empty()) {
                        continue;
                    }
                    const blb::modpack::mod_info* target = blb::modpack::find_mod(mods, dependency.id);
                    if (target == nullptr || !target->enabled ||
                        blb::modpack::version_in_range(target->version, dependency.version)) {
                        continue;
                    }
                    found[mod.id + "\n" + dependency.id + "\n" + target->version] = mod.name + " 需要 " + target->name +
                            " " + dependency.version + "，新版本为 " + target->version;
                }
            }
            return found;
        }

        Outcome transact(const std::vector<Step>& steps, const std::vector<blb::modpack::mod_info>& before,
                         const std::filesystem::path& folder, const std::shared_ptr<Shared>& shared,
                         blb::cancellation_token token) {
            Outcome out;
            std::filesystem::path staging = folder.parent_path() / (".blb-mod-update-" + blb::random_bytes_hex(4));
            std::filesystem::path fresh = staging / "new";
            std::filesystem::path old = staging / "old";
            auto cleanup = [&staging] { (void) blb::fs::remove_all(staging); };
            if (!blb::fs::create_directories(fresh) || !blb::fs::create_directories(old)) {
                out.error = "无法创建临时文件夹";
                cleanup();
                return out;
            }

            std::vector<blb::net::download_task> downloads;
            std::set<std::string> names;
            for (const Step& step : steps) {
                if (!names.insert(blb::to_lower(step.file.filename)).second) {
                    out.error = "有两项更改使用了同名文件 " + step.file.filename;
                    cleanup();
                    return out;
                }
                blb::net::download_task task;
                task.urls = Addons::fileUrls(step.file.url);
                task.target = fresh / blb::path_from_utf8(step.file.filename);
                task.size = step.file.size;
                if (!step.file.sha1.empty()) {
                    task.sha1 = blb::parse_sha1(step.file.sha1);
                }
                downloads.push_back(std::move(task));
            }
            {
                Sink sink(shared);
                blb::progress_tracker tracker(&sink, "mods");
                blb::net::download_client client;
                blb::net::download_report report = client.run(downloads, &tracker, token);
                if (token.is_cancelled()) {
                    out.cancelled = true;
                    cleanup();
                    return out;
                }
                if (!report.ok()) {
                    out.error = "下载失败：" + report.failures.front().reason.message;
                    cleanup();
                    return out;
                }
            }

            enter(shared, Phase::Verifying);
            std::vector<blb::modpack::mod_info> added;
            std::set<std::filesystem::path> replaced;
            for (const Step& step : steps) {
                if (!step.old.empty()) {
                    replaced.insert(step.old);
                }
                auto info = blb::modpack::read_mod(fresh / blb::path_from_utf8(step.file.filename));
                if (!info) {
                    out.problems.push_back("无法读取 " + step.file.filename + "：" + info.err().message);
                    continue;
                }
                info->enabled = !step.disabled;
                if (info->name.empty()) {
                    info->name = info->id.empty() ? step.name : info->id;
                }
                added.push_back(std::move(*info));
            }
            std::vector<blb::modpack::mod_info> after;
            for (const blb::modpack::mod_info& mod : before) {
                if (replaced.count(mod.file) == 0) {
                    after.push_back(mod);
                }
            }
            after.insert(after.end(), added.begin(), added.end());
            std::set<std::string> lacking;
            for (const blb::modpack::mod_issue& issue : blb::modpack::check_dependencies(before, {})) {
                lacking.insert(issue.mod_id + "\n" + issue.missing);
            }
            for (const blb::modpack::mod_issue& issue : blb::modpack::check_dependencies(after, {})) {
                if (lacking.count(issue.mod_id + "\n" + issue.missing) != 0) {
                    continue;
                }
                const blb::modpack::mod_info* mod = blb::modpack::find_mod(after, issue.mod_id);
                out.problems.push_back((mod != nullptr ? mod->name : issue.mod_id) + " 缺少前置 " + issue.missing);
            }
            std::map<std::string, std::string> earlier = rangeIssues(before);
            for (const auto& item : rangeIssues(after)) {
                if (earlier.count(item.first) == 0) {
                    out.problems.push_back(item.second);
                }
            }
            for (const Step& step : steps) {
                for (const std::string& suffix : {std::string(), std::string(".disabled")}) {
                    std::filesystem::path existing = folder / blb::path_from_utf8(step.file.filename + suffix);
                    std::error_code error;
                    if (std::filesystem::exists(existing, error) && replaced.count(existing) == 0) {
                        out.problems.push_back("模组文件夹里已经有 " + step.file.filename + suffix);
                    }
                }
            }
            if (!out.problems.empty()) {
                out.error = "应用后会出现问题，没有做任何更改";
                cleanup();
                return out;
            }
            if (token.is_cancelled()) {
                out.cancelled = true;
                cleanup();
                return out;
            }

            enter(shared, Phase::Replacing);
            (void) blb::fs::create_directories(folder);
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
            auto fail = [&](const std::string& what, const blb::error& reason) {
                bool restored = true;
                for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                    if (!blb::fs::rename_file(it->second, it->first)) {
                        restored = false;
                    }
                }
                out.error = what + "：" + reason.message;
                if (restored) {
                    cleanup();
                } else {
                    out.error += "，原文件保存在 " + blb::path_to_utf8(staging);
                }
            };
            for (const Step& step : steps) {
                if (step.old.empty()) {
                    continue;
                }
                std::filesystem::path backup = old / step.old.filename();
                auto renamed = blb::fs::rename_file(step.old, backup);
                if (!renamed) {
                    fail("无法移动 " + step.name, renamed.err());
                    return out;
                }
                moved.push_back({step.old, backup});
            }
            for (const Step& step : steps) {
                std::filesystem::path from = fresh / blb::path_from_utf8(step.file.filename);
                std::filesystem::path to =
                        folder / blb::path_from_utf8(step.file.filename + (step.disabled ? ".disabled" : ""));
                auto renamed = blb::fs::rename_file(from, to);
                if (!renamed) {
                    fail("无法放入 " + step.name, renamed.err());
                    return out;
                }
                moved.push_back({from, to});
            }
            cleanup();
            out.ok = true;
            return out;
        }

        void finished(const std::string& key, const Outcome& outcome, size_t count) {
            Instance& state = instances[key];
            state.busy = false;
            state.shared.reset();
            state.cancel.reset();
            if (outcome.ok) {
                state.changes.clear();
                state.progress = Progress{};
                Mods::scan(key);
                Notices::add(Notices::Kind::Done, "已应用 " + std::to_string(count) + " 项模组更改", 3000);
            } else if (outcome.cancelled) {
                state.progress = Progress{};
            } else {
                state.progress = Progress{};
                state.progress.phase = Phase::Failed;
                state.progress.error = outcome.error;
                state.progress.problems = outcome.problems;
            }
        }
    }

    void prepare(const std::string& key) {
        sync(key);
    }

    bool identifying(const std::string& key) {
        return sync(key).running || Mods::of(key).loading;
    }

    std::string identifyError(const std::string& key) {
        return sync(key).error;
    }

    const Remote* remote(const Mods::Mod& mod) {
        auto hash = hashes.find(fileKey(mod.info));
        if (hash == hashes.end()) {
            return nullptr;
        }
        auto found = remotes.find(hash->second);
        return found != remotes.end() ? &found->second : nullptr;
    }

    const Project* project(const std::string& projectId) {
        auto found = projects.find(projectId);
        return found != projects.end() ? &found->second : nullptr;
    }

    std::string title(const std::string& projectId) {
        const Project* found = project(projectId);
        if (found != nullptr && !found->title.empty()) {
            return found->title;
        }
        return projectId;
    }

    std::string gameVersion(const std::string& key) {
        const Instances::Entry* entry = Instances::find(key);
        return entry != nullptr ? entry->info.minecraft_version : std::string();
    }

    const Addons::Versions& versions(const std::string& key, const std::string& projectId) {
        std::string id = key + "|" + projectId;
        if (versionLists.count(id) == 0) {
            fetchVersions(key, projectId);
        }
        return versionLists[id];
    }

    void refreshVersions(const std::string& key, const std::string& projectId) {
        fetchVersions(key, projectId);
    }

    const std::vector<Change>& changes(const std::string& key) {
        auto found = instances.find(key);
        return found != instances.end() ? found->second.changes : NO_CHANGES;
    }

    const Change* changeFor(const std::string& key, const std::filesystem::path& file) {
        auto found = instances.find(key);
        if (found == instances.end()) {
            return nullptr;
        }
        std::string wanted = stem(file);
        for (const Change& change : found->second.changes) {
            if (!change.file.empty() && stem(change.file) == wanted) {
                return &change;
            }
        }
        return nullptr;
    }

    void stage(const std::string& key, const std::filesystem::path& file, const Addons::Version& target) {
        Instance& state = sync(key);
        if (state.busy) {
            return;
        }
        const Mods::Mod* mod = modAt(Mods::of(key), file);
        if (mod == nullptr) {
            return;
        }
        std::string wanted = stem(file);
        auto& changes = state.changes;
        changes.erase(std::remove_if(changes.begin(), changes.end(),
                                     [&](const Change& change) {
                                         return !change.file.empty() && stem(change.file) == wanted;
                                     }),
                      changes.end());
        state.revision++;
        const Remote* current = remote(*mod);
        if (current != nullptr && current->versionId == target.id) {
            return;
        }
        Change change;
        change.file = mod->info.file;
        change.name = mod->info.name;
        change.projectId = target.projectId;
        change.from = current != nullptr ? current->number : mod->info.version;
        change.disabled = !mod->info.enabled;
        change.target = target;
        changes.push_back(std::move(change));
        if (state.progress.phase == Phase::Failed) {
            state.progress = Progress{};
        }
    }

    void addProject(const std::string& key, const std::string& projectId) {
        Instance& state = sync(key);
        if (state.busy || projectId.empty() || state.adding.count(projectId) != 0) {
            return;
        }
        for (const Change& change : state.changes) {
            if (change.file.empty() && change.projectId == projectId) {
                return;
            }
        }
        state.adding.insert(projectId);
        std::string game = gameVersion(key);
        std::string loader = loaderOf(key);
        std::string id = key + "|" + projectId;
        tasks.run(
                [key, projectId, game, loader, id](blb::cancellation_token token) -> Continuation {
                    Addons::Fetched fetched = Modrinth::fetchProjectVersions(projectId, game, "", token);
                    std::vector<Addons::Version> kept;
                    for (const Addons::Version& version : fetched.versions) {
                        if (fits(version, loader)) {
                            kept.push_back(version);
                        }
                    }
                    return [key, projectId, game, id, ok = fetched.ok, error = fetched.error, kept] {
                        Instance& state = instances[key];
                        state.adding.erase(projectId);
                        if (!ok) {
                            Notices::add(Notices::Kind::Failed, "无法获取 " + title(projectId) + " 的版本：" + error,
                                         4000);
                            return;
                        }
                        Addons::Versions& entry = versionLists[id];
                        entry.status = Addons::Status::Ready;
                        entry.list = kept;
                        if (kept.empty()) {
                            Notices::add(Notices::Kind::Failed, "没有适用于 " + game + " 的 " + title(projectId), 4000);
                            return;
                        }
                        const Addons::Version* chosen = &kept.front();
                        for (const Addons::Version& version : kept) {
                            if (version.type == "release") {
                                chosen = &version;
                                break;
                            }
                        }
                        Change change;
                        change.name = title(projectId);
                        change.projectId = projectId;
                        change.target = *chosen;
                        state.changes.push_back(std::move(change));
                        state.revision++;
                    };
                },
                [key, projectId](const std::string& error) -> Continuation {
                    return [key, projectId, error] {
                        instances[key].adding.erase(projectId);
                        Notices::add(Notices::Kind::Failed, "无法获取 " + title(projectId) + " 的版本：" + error, 4000);
                    };
                });
    }

    bool adding(const std::string& key, const std::string& projectId) {
        auto found = instances.find(key);
        return found != instances.end() && found->second.adding.count(projectId) != 0;
    }

    void unstage(const std::string& key, const Change& change) {
        Instance& state = instances[key];
        if (state.busy) {
            return;
        }
        bool local = !change.file.empty();
        std::string wanted = local ? stem(change.file) : change.projectId;
        auto& changes = state.changes;
        changes.erase(std::remove_if(changes.begin(), changes.end(),
                                     [&](const Change& item) {
                                         return local ? !item.file.empty() && stem(item.file) == wanted
                                                      : item.file.empty() && item.projectId == wanted;
                                     }),
                      changes.end());
        state.revision++;
        if (changes.empty() && state.progress.phase == Phase::Failed) {
            state.progress = Progress{};
        }
    }

    void discard(const std::string& key) {
        Instance& state = instances[key];
        if (state.busy) {
            return;
        }
        state.changes.clear();
        state.revision++;
        state.progress = Progress{};
    }

    std::vector<Issue> check(const std::string& key) {
        Instance& state = sync(key);
        std::vector<Issue> issues;
        const Mods::List& list = Mods::of(key);
        std::string game = gameVersion(key);
        std::string loader = loaderOf(key);
        std::set<std::string> present;
        for (const Mods::Mod& mod : list.mods) {
            if (!mod.info.enabled) {
                continue;
            }
            if (const Remote* found = remote(mod)) {
                present.insert(found->projectId);
            }
        }
        for (const Change& change : state.changes) {
            if (change.file.empty()) {
                present.insert(change.projectId);
            }
        }
        std::set<std::string> ids = presentIds(list);
        auto satisfied = [&](const std::string& projectId) {
            if (present.count(projectId) != 0) {
                return true;
            }
            const Project* info = project(projectId);
            if (info == nullptr) {
                return true;
            }
            std::string slug = info->slug;
            std::string underscored = slug;
            std::replace(underscored.begin(), underscored.end(), '-', '_');
            return !slug.empty() && (ids.count(slug) != 0 || ids.count(underscored) != 0);
        };
        auto push = [&issues](Severity severity, const std::string& text, const std::string& missing) {
            for (const Issue& issue : issues) {
                if (issue.text == text) {
                    return;
                }
            }
            issues.push_back(Issue{severity, text, missing});
        };
        for (const Change& change : state.changes) {
            if (change.disabled) {
                continue;
            }
            for (const Addons::Dependency& dependency : change.target.dependencies) {
                if (dependency.projectId.empty()) {
                    continue;
                }
                if (dependency.incompatible) {
                    if (present.count(dependency.projectId) != 0) {
                        push(Severity::Error, change.name + " 与 " + title(dependency.projectId) + " 不兼容", {});
                    }
                } else if (dependency.required && !satisfied(dependency.projectId)) {
                    push(Severity::Error, change.name + " 需要前置 " + title(dependency.projectId),
                         dependency.projectId);
                }
            }
            const Addons::Version& target = change.target;
            if (!game.empty() && !target.gameVersions.empty() &&
                std::find(target.gameVersions.begin(), target.gameVersions.end(), game) == target.gameVersions.end()) {
                push(Severity::Warning, change.name + " " + target.number + " 不支持 " + game, {});
            }
            if (!fits(target, loader)) {
                push(Severity::Warning, change.name + " " + target.number + " 不支持当前加载器", {});
            }
            if (change.file.empty()) {
                continue;
            }
            const Mods::Mod* mod = modAt(list, change.file);
            if (mod == nullptr || mod->info.id.empty()) {
                continue;
            }
            for (const Mods::Mod& other : list.mods) {
                if (!other.info.enabled || &other == mod) {
                    continue;
                }
                for (const blb::modpack::mod_dependency& dependency : other.info.dependencies) {
                    bool names = dependency.id == mod->info.id ||
                            std::find(mod->info.provides.begin(), mod->info.provides.end(), dependency.id) !=
                                    mod->info.provides.end();
                    if (names && dependency.required && !dependency.version.empty() &&
                        !blb::modpack::version_in_range(target.number, dependency.version)) {
                        push(Severity::Warning, other.info.name + " 要求 " + change.name + " " + dependency.version,
                             {});
                    }
                }
            }
        }
        return issues;
    }

    bool checking(const std::string& key) {
        Instance& state = sync(key);
        if (state.running) {
            return true;
        }
        for (const Change& change : state.changes) {
            for (const Addons::Dependency& dependency : change.target.dependencies) {
                if (fetchingProjects.count(dependency.projectId) != 0) {
                    return true;
                }
            }
        }
        return !state.adding.empty();
    }

    bool apply(const std::string& key) {
        Instance& state = sync(key);
        if (state.busy || state.changes.empty()) {
            return false;
        }
        const Instances::Entry* entry = Instances::find(key);
        if (entry == nullptr) {
            return false;
        }
        if (Game::state(key) != Game::State::Idle) {
            Notices::add(Notices::Kind::Failed, "请先关闭游戏再应用更改", 4000);
            return false;
        }
        for (const Issue& issue : check(key)) {
            if (issue.severity == Severity::Error) {
                return false;
            }
        }
        std::vector<Step> steps;
        for (const Change& change : state.changes) {
            const Addons::File* file = change.target.file();
            if (file == nullptr || file->url.empty() || !blb::fs::safe_filename(file->filename)) {
                Notices::add(Notices::Kind::Failed, change.name + " 的下载文件无效", 4000);
                return false;
            }
            steps.push_back(Step{change.file, change.disabled, *file, change.name});
        }
        std::vector<blb::modpack::mod_info> before;
        for (const Mods::Mod& mod : Mods::of(key).mods) {
            before.push_back(mod.info);
        }
        std::filesystem::path folder = entry->gameDirectory / "mods";
        auto shared = std::make_shared<Shared>();
        auto source = std::make_shared<blb::cancellation_source>();
        state.busy = true;
        state.progress = Progress{};
        state.progress.phase = Phase::Downloading;
        state.shared = shared;
        state.cancel = source;
        size_t count = steps.size();
        applying.run(
                [key, steps, before, folder, shared, source, count](blb::cancellation_token) -> Continuation {
                    Outcome outcome = transact(steps, before, folder, shared, source->token());
                    return [key, outcome, count] { finished(key, outcome, count); };
                },
                [key](const std::string& error) -> Continuation {
                    return [key, error] {
                        Outcome outcome;
                        outcome.error = error;
                        finished(key, outcome, 0);
                    };
                });
        return true;
    }

    void cancel(const std::string& key) {
        auto found = instances.find(key);
        if (found != instances.end() && found->second.cancel) {
            found->second.cancel->cancel();
        }
    }

    bool busy(const std::string& key) {
        auto found = instances.find(key);
        return found != instances.end() && found->second.busy;
    }

    const Progress& progress(const std::string& key) {
        auto found = instances.find(key);
        return found != instances.end() ? found->second.progress : IDLE;
    }

    void dismiss(const std::string& key) {
        auto found = instances.find(key);
        if (found != instances.end() && found->second.progress.phase == Phase::Failed) {
            found->second.progress = Progress{};
        }
    }

    void poll() {
        tasks.poll();
        applying.poll();
        for (auto& item : instances) {
            Instance& state = item.second;
            if (state.busy && state.shared) {
                std::lock_guard<std::mutex> guard(state.shared->lock);
                state.progress.phase = state.shared->phase;
                state.progress.fraction = state.shared->fraction;
            }
        }
    }

    void shutdown() {
        for (auto& item : instances) {
            if (item.second.cancel) {
                item.second.cancel->cancel();
            }
        }
        tasks.stop();
        applying.stop();
    }
}

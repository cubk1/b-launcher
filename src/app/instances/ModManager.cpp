#include "app/instances/ModManager.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/DownloadSources.h"
#include "app/downloads/ModNames.h"
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
#include <core/modpack/modpack_impl.hpp>
#include <core/modpack/mods.hpp>
#include <core/network/download_client.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
        constexpr size_t BATCH = 80;
        const char* const BUILTIN[] = {"minecraft", "java",     "fabricloader", "fabric-loader", "quilt_loader",
                                       "forge",     "neoforge", "javafml",      "lowcodefml",    "mixinextras"};
        constexpr Addons::Source SOURCES[] = {Addons::Source::Modrinth, Addons::Source::CurseForge};

        struct Shared {
            std::mutex lock;
            Phase phase = Phase::Downloading;
            float fraction = -1.0f;
        };

        struct Identification {
            int serial = -1;
            bool running = false;
            std::string error;
        };

        struct Instance {
            int serial = -1;
            int revision = 0;
            std::tuple<int, int, int> requested{-1, -1, -1};
            Clock::time_point nextMetadataCheck;
            bool hashing = false;
            std::string hashError;
            std::map<Addons::Source, Identification> lookups;
            std::vector<Change> changes;
            std::set<std::string> adding;
            int addingGeneration = 0;
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

        struct HashRequest {
            std::string id;
            std::filesystem::path file;
            std::uint64_t size = 0;
            std::int64_t modified = 0;
        };

        struct Requirement {
            std::string name;
            Addons::Dependency dependency;
        };

        struct Digest {
            std::string sha1;
            std::uint32_t fingerprint = 0;
        };

        AsyncTasks tasks;
        AsyncTasks applying;
        std::map<std::string, Instance> instances;
        std::map<std::string, Digest> hashes;
        std::map<std::string, Remote> remotes;
        std::set<std::string> unmatched;
        std::map<std::uint32_t, Remote> curseRemotes;
        std::set<std::uint32_t> curseUnmatched;
        std::map<std::string, Project> projects;
        std::set<std::string> fetchingProjects;
        std::map<std::string, Clock::time_point> projectRetry;
        std::map<std::string, Addons::Versions> versionLists;
        std::map<std::string, Addons::Versions> dependencyVersions;
        std::map<std::string, Clock::time_point> dependencyRetry;
        int metadataVersion = 0;
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
            return stem(info.file) + "#" + std::to_string(info.size) + "#" + std::to_string(info.modified);
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

        Addons::Source sourceOf(const std::string& projectId) {
            bool numeric = !projectId.empty() &&
                    std::all_of(projectId.begin(), projectId.end(), [](char c) { return c >= '0' && c <= '9'; });
            return numeric ? Addons::Source::CurseForge : Addons::Source::Modrinth;
        }

        Addons::Version labelled(Addons::Version version) {
            if (version.source == Addons::Source::CurseForge && !version.name.empty()) {
                version.number = version.name;
                if (blb::to_lower(version.number).ends_with(".jar")) {
                    version.number.resize(version.number.size() - 4);
                }
            }
            return version;
        }

        std::optional<bool> sameVersion(const Addons::Version& selected, const Addons::Version& pinned) {
            if (selected.source == pinned.source) {
                return selected.projectId == pinned.projectId && selected.id == pinned.id;
            }
            const Addons::File* file = selected.file();
            auto hash = file != nullptr ? blb::parse_sha1(file->sha1) : std::nullopt;
            if (!hash) {
                return std::nullopt;
            }
            bool complete = !pinned.files.empty();
            for (const Addons::File& candidate : pinned.files) {
                auto other = blb::parse_sha1(candidate.sha1);
                if (!other) {
                    complete = false;
                } else if (*hash == *other) {
                    return true;
                }
            }
            return complete ? std::optional(false) : std::nullopt;
        }

        bool owns(const Mods::Mod& mod, const std::string& projectId) {
            for (Addons::Source source : SOURCES) {
                const Remote* found = remote(mod, source);
                if (found != nullptr && found->projectId == projectId) {
                    return true;
                }
            }
            return false;
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
            for (const std::string& url : DownloadSources::provider()->resolve(std::string(API) + path)) {
                blb::net::http_request call;
                call.method = method;
                call.url = url;
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
                        std::set<std::string> failed;
                        std::vector<std::string> modrinth;
                        std::vector<std::string> curse;
                        for (const std::string& id : wanted) {
                            (sourceOf(id) == Addons::Source::CurseForge ? curse : modrinth).push_back(id);
                        }
                        for (size_t start = 0; start < curse.size(); start += BATCH) {
                            std::vector<std::string> chunk(curse.begin() + (long) start,
                                                           curse.begin() +
                                                                   (long) std::min(curse.size(), start + BATCH));
                            auto hits = CurseForge::fetchProjects(chunk, token);
                            if (!hits) {
                                failed.insert(curse.begin() + (long) start, curse.end());
                                break;
                            }
                            for (const Addons::Hit& hit : *hits) {
                                found.push_back(Project{hit.id, hit.slug, hit.title, hit.iconUrl});
                            }
                        }
                        for (size_t start = 0; start < modrinth.size(); start += BATCH) {
                            json ids = json::array();
                            for (size_t i = start; i < std::min(modrinth.size(), start + BATCH); i++) {
                                ids.push_back(modrinth[i]);
                            }
                            auto doc = request("GET", "/projects?ids=" + blb::url_encode(ids.dump()), {}, token);
                            if (!doc) {
                                failed.insert(modrinth.begin() + (long) start, modrinth.end());
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
                        return [wanted, found, failed] {
                            ++metadataVersion;
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
                                if (failed.count(id) == 0) {
                                    projects[id] = Project{id, {}, {}, {}};
                                } else {
                                    projectRetry[id] = Clock::now() + std::chrono::seconds(30);
                                }
                            }
                        };
                    },
                    [wanted](const std::string&) -> Continuation {
                        return [wanted] {
                            ++metadataVersion;
                            for (const std::string& id : wanted) {
                                fetchingProjects.erase(id);
                                projectRetry[id] = Clock::now() + std::chrono::seconds(30);
                            }
                        };
                    });
        }

        const Addons::Version* dependencyVersion(const std::string& id) {
            auto found = dependencyVersions.find(id);
            return found != dependencyVersions.end() && found->second.status == Addons::Status::Ready &&
                            !found->second.list.empty()
                    ? &found->second.list.front()
                    : nullptr;
        }

        void ensureVersions(const std::set<std::string>& ids) {
            std::vector<std::string> wanted;
            auto now = Clock::now();
            for (const std::string& id : ids) {
                auto found = dependencyVersions.find(id);
                if (found != dependencyVersions.end() && found->second.status != Addons::Status::Failed) {
                    continue;
                }
                auto retry = dependencyRetry.find(id);
                if (retry != dependencyRetry.end() && retry->second > now) {
                    continue;
                }
                dependencyVersions[id] = Addons::Versions{};
                wanted.push_back(id);
            }
            if (wanted.empty()) {
                return;
            }
            tasks.run(
                    [wanted](blb::cancellation_token token) -> Continuation {
                        std::map<std::string, Addons::Versions> found;
                        for (const std::string& id : wanted) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
                            Addons::Fetched fetched = Modrinth::fetchVersion(id, token);
                            Addons::Versions entry;
                            bool valid = fetched.ok && fetched.versions.size() == 1 &&
                                    fetched.versions.front().id == id && !fetched.versions.front().projectId.empty();
                            entry.status = valid ? Addons::Status::Ready : Addons::Status::Failed;
                            entry.error = valid ? std::string()
                                                : fetched.error.empty() ? "版本信息无效" : fetched.error;
                            if (valid) {
                                entry.list = std::move(fetched.versions);
                            }
                            found.emplace(id, std::move(entry));
                        }
                        return [found = std::move(found)]() mutable {
                            for (auto& [id, entry] : found) {
                                if (entry.status == Addons::Status::Failed) {
                                    dependencyRetry[id] = Clock::now() + std::chrono::seconds(30);
                                } else {
                                    dependencyRetry.erase(id);
                                }
                                dependencyVersions[id] = std::move(entry);
                            }
                            ++metadataVersion;
                        };
                    },
                    [wanted](const std::string& error) -> Continuation {
                        return [wanted, error] {
                            for (const std::string& id : wanted) {
                                dependencyVersions[id].status = Addons::Status::Failed;
                                dependencyVersions[id].error = error;
                                dependencyRetry[id] = Clock::now() + std::chrono::seconds(30);
                            }
                            ++metadataVersion;
                        };
                    });
        }

        std::vector<Requirement> requirements(const std::string& key, const Instance& state) {
            std::vector<Requirement> out;
            std::set<std::string> affected;
            const Mods::List& list = Mods::of(key);
            for (const Change& change : state.changes) {
                if (change.disabled) {
                    continue;
                }
                affected.insert(change.projectId);
                if (const Mods::Mod* old = modAt(list, change.file)) {
                    for (Addons::Source source : SOURCES) {
                        if (const Remote* found = remote(*old, source)) {
                            affected.insert(found->projectId);
                        }
                    }
                }
                for (const Addons::Dependency& dependency : change.target.dependencies) {
                    if (dependency.required || dependency.incompatible) {
                        out.push_back({change.name, dependency});
                    }
                }
            }
            for (const Mods::Mod& mod : list.mods) {
                if (affected.empty() || !mod.info.enabled || changeFor(key, mod.info.file) != nullptr) {
                    continue;
                }
                const Remote* found = remote(mod);
                if (found == nullptr) {
                    continue;
                }
                for (const Addons::Dependency& dependency : found->dependencies) {
                    std::string projectId = dependencyProject(dependency);
                    if ((dependency.required || dependency.incompatible) &&
                        (projectId.empty() || affected.count(projectId) != 0)) {
                        out.push_back({mod.info.name, dependency});
                    }
                }
            }
            return out;
        }

        std::set<std::string> requiredVersions(const std::string& key, const Instance& state,
                                              const std::string& projectId) {
            std::set<std::string> pins;
            for (const Requirement& item : requirements(key, state)) {
                const Addons::Dependency& dependency = item.dependency;
                if (dependency.required && !dependency.incompatible && !dependency.versionId.empty() &&
                    dependencyProject(dependency) == projectId) {
                    pins.insert(dependency.versionId);
                }
            }
            return pins;
        }

        void hashFiles(const std::string& key, Instance& state, const Mods::List& list) {
            state.serial = list.serial;
            state.hashError.clear();
            std::vector<HashRequest> wanted;
            for (const Mods::Mod& mod : list.mods) {
                std::string id = fileKey(mod.info);
                if (hashes.count(id) == 0) {
                    wanted.push_back({id, mod.info.file, mod.info.size, mod.info.modified});
                }
            }
            if (wanted.empty()) {
                return;
            }
            state.hashing = true;
            tasks.run(
                    [key, wanted](blb::cancellation_token token) -> Continuation {
                        std::map<std::string, Digest> found;
                        for (const HashRequest& file : wanted) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
                            auto before = blb::fs::stat(file.file);
                            if (!before.exists || before.size != file.size || before.mtime != file.modified) {
                                continue;
                            }
                            auto data = blb::fs::read_file(file.file);
                            auto after = blb::fs::stat(file.file);
                            if (data && after.exists && after.size == file.size && after.mtime == file.modified) {
                                found[file.id] = Digest{blb::to_hex(blb::sha1::digest(*data)),
                                                        blb::modpack::detail::curseforge_fingerprint(*data)};
                            }
                        }
                        return [key, found = std::move(found)]() mutable {
                            for (auto& [id, digest] : found) {
                                hashes[id] = std::move(digest);
                            }
                            instances[key].hashing = false;
                            ++metadataVersion;
                        };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            instances[key].hashing = false;
                            instances[key].hashError = error;
                        };
                    });
        }

        void identify(const std::string& key, Addons::Source source, Instance& state, const Mods::List& list) {
            Identification& identification = state.lookups[source];
            identification.serial = list.serial;
            identification.error.clear();
            std::vector<std::string> lookup;
            std::vector<std::uint32_t> prints;
            for (const Mods::Mod& mod : list.mods) {
                auto known = hashes.find(fileKey(mod.info));
                if (known == hashes.end()) {
                    continue;
                }
                const Digest& digest = known->second;
                if (source == Addons::Source::Modrinth) {
                    if (remotes.count(digest.sha1) == 0 && unmatched.count(digest.sha1) == 0) {
                        lookup.push_back(digest.sha1);
                    }
                } else if (curseRemotes.count(digest.fingerprint) == 0 && curseUnmatched.count(digest.fingerprint) == 0) {
                    prints.push_back(digest.fingerprint);
                }
            }
            if (lookup.empty() && prints.empty()) {
                return;
            }
            std::sort(lookup.begin(), lookup.end());
            lookup.erase(std::unique(lookup.begin(), lookup.end()), lookup.end());
            std::sort(prints.begin(), prints.end());
            prints.erase(std::unique(prints.begin(), prints.end()), prints.end());
            identification.running = true;
            tasks.run(
                    [key, source, lookup, prints](blb::cancellation_token token) -> Continuation {
                        std::map<std::string, Remote> found;
                        std::map<std::uint32_t, Remote> curseFound;
                        std::string error;
                        for (size_t start = 0; start < lookup.size(); start += BATCH) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
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
                                if (!version.projectId.empty()) {
                                    found[item.key()] = Remote{source, version.projectId, version.id,
                                                               version.number, version.dependencies};
                                }
                            }
                        }
                        for (size_t start = 0; start < prints.size(); start += BATCH) {
                            if (token.is_cancelled()) {
                                return nullptr;
                            }
                            std::vector<std::uint32_t> chunk(prints.begin() + (long) start,
                                                             prints.begin() +
                                                                     (long) std::min(prints.size(), start + BATCH));
                            auto matched = CurseForge::matchFingerprints(chunk, token);
                            if (!matched) {
                                error = matched.err().message;
                                break;
                            }
                            for (const auto& [print, match] : *matched) {
                                Addons::Version version = labelled(match);
                                curseFound[print] = Remote{source, version.projectId, version.id,
                                                           version.number, version.dependencies};
                            }
                        }
                        return [key, source, lookup, prints, found = std::move(found),
                                curseFound = std::move(curseFound), error] {
                            for (const auto& item : found) {
                                remotes[item.first] = item.second;
                            }
                            for (const auto& item : curseFound) {
                                curseRemotes[item.first] = item.second;
                            }
                            if (error.empty()) {
                                for (const std::string& hash : lookup) {
                                    if (found.count(hash) == 0) {
                                        unmatched.insert(hash);
                                    }
                                }
                                for (std::uint32_t print : prints) {
                                    if (curseFound.count(print) == 0) {
                                        curseUnmatched.insert(print);
                                    }
                                }
                            }
                            Identification& identification = instances[key].lookups[source];
                            identification.running = false;
                            identification.error = error;
                            ++metadataVersion;
                        };
                    },
                    [key, source](const std::string& error) -> Continuation {
                        return [key, source, error] {
                            Identification& identification = instances[key].lookups[source];
                            identification.running = false;
                            identification.error = error;
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
            if (!state.hashing && list.serial != state.serial) {
                hashFiles(key, state, list);
            }
            if (!state.hashing) {
                for (Addons::Source source : SOURCES) {
                    const Identification& identification = state.lookups[source];
                    if (!identification.running && identification.serial != list.serial) {
                        identify(key, source, state, list);
                    }
                }
            }
            std::tuple<int, int, int> stamp{list.serial, state.revision, metadataVersion};
            auto now = Clock::now();
            if (stamp == state.requested && now < state.nextMetadataCheck) {
                return state;
            }
            state.requested = stamp;
            state.nextMetadataCheck = now + std::chrono::seconds(1);
            std::set<std::string> wanted;
            std::set<std::string> pinned;
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
            }
            for (const Requirement& item : requirements(key, state)) {
                wanted.insert(dependencyProject(item.dependency));
                if (!item.dependency.versionId.empty()) {
                    pinned.insert(item.dependency.versionId);
                }
            }
            ensureVersions(pinned);
            // A slow project endpoint must not hold back metadata from the other source either.
            for (Addons::Source source : SOURCES) {
                std::set<std::string> selected;
                for (const std::string& id : wanted) {
                    if (sourceOf(id) == source) {
                        selected.insert(id);
                    }
                }
                ensureProjects(selected);
            }
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
                        Addons::Fetched fetched =
                                Addons::fetchProjectVersions(sourceOf(projectId), projectId, game, "", token);
                        std::vector<Addons::Version> kept;
                        for (const Addons::Version& version : fetched.versions) {
                            if (fits(version, loader)) {
                                kept.push_back(labelled(version));
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
                    if (!dependency.required || !dependency.client || dependency.version.empty()) {
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

    bool identifying(const std::string& key, Addons::Source source) {
        Instance& state = sync(key);
        return state.hashing || state.lookups[source].running || Mods::of(key).loading;
    }

    std::string identifyError(const std::string& key, Addons::Source source) {
        Instance& state = sync(key);
        return !state.hashError.empty() ? state.hashError : state.lookups[source].error;
    }

    const Remote* remote(const Mods::Mod& mod) {
        const Remote* found = remote(mod, Addons::Source::Modrinth);
        return found != nullptr ? found : remote(mod, Addons::Source::CurseForge);
    }

    const Remote* remote(const Mods::Mod& mod, Addons::Source source) {
        auto digest = hashes.find(fileKey(mod.info));
        if (digest == hashes.end()) {
            return nullptr;
        }
        if (source == Addons::Source::CurseForge) {
            auto found = curseRemotes.find(digest->second.fingerprint);
            return found != curseRemotes.end() ? &found->second : nullptr;
        }
        auto found = remotes.find(digest->second.sha1);
        return found != remotes.end() ? &found->second : nullptr;
    }

    const Project* project(const std::string& projectId) {
        auto found = projects.find(projectId);
        return found != projects.end() ? &found->second : nullptr;
    }

    int metadataRevision() {
        return metadataVersion;
    }

    std::string dependencyProject(const Addons::Dependency& dependency) {
        if (!dependency.projectId.empty()) {
            return dependency.projectId;
        }
        const Addons::Version* version = dependencyVersion(dependency.versionId);
        return version != nullptr ? version->projectId : std::string();
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

    namespace {
        std::string nameKey(const Mods::Mod& mod) {
            if (!ModNames::zh(mod.info.name).empty()) {
                return mod.info.name;
            }
            if (const Remote* found = remote(mod)) {
                const Project* info = project(found->projectId);
                if (info != nullptr && !info->title.empty() && info->title != mod.info.name &&
                    !ModNames::zh(info->title).empty()) {
                    return info->title;
                }
            }
            return std::string();
        }
    }

    std::string chineseName(const Mods::Mod& mod) {
        std::string key = nameKey(mod);
        return key.empty() ? std::string() : ModNames::zh(key);
    }

    std::string displayName(const Mods::Mod& mod) {
        if (ModNames::original()) {
            return mod.info.name;
        }
        std::string zh = chineseName(mod);
        return zh.empty() ? mod.info.name : zh;
    }

    long long wikiId(const Mods::Mod& mod) {
        std::string key = nameKey(mod);
        return key.empty() ? 0 : ModNames::wikiId(key);
    }

    bool matches(const Mods::Mod& mod, const std::string& query) {
        return Mods::matches(mod, query) || blb::icontains(chineseName(mod), query);
    }

    bool namesReady(const Mods::List& list) {
        if (ModNames::original()) {
            return true;
        }
        std::vector<std::string> names;
        names.reserve(list.mods.size());
        for (const Mods::Mod& mod : list.mods) {
            names.push_back(mod.info.name);
        }
        return ModNames::resolved(names);
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
        const Remote* current = remote(*mod, target.source);
        if (current != nullptr && current->versionId == target.id) {
            return;
        }
        const Remote* shown = current != nullptr ? current : remote(*mod);
        Change change;
        change.file = mod->info.file;
        change.name = displayName(*mod);
        change.projectId = target.projectId;
        change.from = shown != nullptr ? shown->number : mod->info.version;
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
        std::set<std::string> pins = requiredVersions(key, state, projectId);
        if (pins.size() > 1) {
            Notices::add(Notices::Kind::Failed, title(projectId) + " 的前置版本要求冲突，请调整待应用的版本", 4000);
            return;
        }
        std::string pin = pins.empty() ? std::string() : *pins.begin();
        for (const Change& change : state.changes) {
            if (change.file.empty() && change.projectId == projectId && (pin.empty() || change.target.id == pin)) {
                return;
            }
        }
        std::optional<Addons::Version> pinned;
        if (const Addons::Version* version = dependencyVersion(pin)) {
            pinned = *version;
        }
        state.adding.insert(projectId);
        int generation = state.addingGeneration;
        std::string game = gameVersion(key);
        std::string loader = loaderOf(key);
        std::string id = key + "|" + projectId;
        tasks.run(
                [key, projectId, game, loader, id, generation, pins, pin, pinned](blb::cancellation_token token)
                        -> Continuation {
                    Addons::Fetched fetched;
                    if (pinned) {
                        fetched.ok = true;
                        fetched.versions.push_back(*pinned);
                    } else if (!pin.empty()) {
                        fetched = Modrinth::fetchVersion(pin, token);
                    } else {
                        fetched = Addons::fetchProjectVersions(sourceOf(projectId), projectId, game, "", token);
                    }
                    std::vector<Addons::Version> kept;
                    for (const Addons::Version& version : fetched.versions) {
                        bool supportsGame = game.empty() || version.gameVersions.empty() ||
                                std::find(version.gameVersions.begin(), version.gameVersions.end(), game) !=
                                        version.gameVersions.end();
                        if (version.projectId == projectId && (pin.empty() || version.id == pin) &&
                            supportsGame && fits(version, loader)) {
                            kept.push_back(labelled(version));
                        }
                    }
                    return [key, projectId, game, id, generation, pins, pin, ok = fetched.ok, error = fetched.error,
                            kept] {
                        Instance& state = instances[key];
                        if (state.addingGeneration != generation) {
                            return;
                        }
                        state.adding.erase(projectId);
                        if (requiredVersions(key, state, projectId) != pins || gameVersion(key) != game) {
                            Notices::add(Notices::Kind::Info, "前置要求已改变，请重新添加 " + title(projectId), 4000);
                            return;
                        }
                        if (!ok) {
                            Notices::add(Notices::Kind::Failed, "无法获取 " + title(projectId) + " 的版本：" + error,
                                         4000);
                            return;
                        }
                        if (pin.empty()) {
                            Addons::Versions& entry = versionLists[id];
                            entry.status = Addons::Status::Ready;
                            entry.list = kept;
                        }
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
                        for (const Mods::Mod& mod : Mods::of(key).mods) {
                            if (!owns(mod, projectId)) {
                                continue;
                            }
                            if (!mod.info.enabled) {
                                Notices::add(Notices::Kind::Failed, "请先启用前置 " + mod.info.name, 4000);
                                return;
                            }
                            stage(key, mod.info.file, *chosen);
                            return;
                        }
                        Change change;
                        change.name = title(projectId);
                        change.projectId = projectId;
                        change.target = *chosen;
                        std::erase_if(state.changes, [&projectId](const Change& item) {
                            return item.file.empty() && item.projectId == projectId;
                        });
                        state.changes.push_back(std::move(change));
                        state.revision++;
                    };
                },
                [key, projectId, generation](const std::string& error) -> Continuation {
                    return [key, projectId, generation, error] {
                        Instance& state = instances[key];
                        if (state.addingGeneration != generation) {
                            return;
                        }
                        state.adding.erase(projectId);
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
        state.adding.clear();
        ++state.addingGeneration;
        state.revision++;
        state.progress = Progress{};
    }

    std::vector<Issue> check(const std::string& key) {
        Instance& state = sync(key);
        std::vector<Issue> issues;
        const Mods::List& list = Mods::of(key);
        std::string game = gameVersion(key);
        std::string loader = loaderOf(key);
        std::map<std::string, std::set<std::string>> present;
        std::set<std::string> ids(std::begin(BUILTIN), std::end(BUILTIN));
        for (const Mods::Mod& mod : list.mods) {
            if (!mod.info.enabled || changeFor(key, mod.info.file) != nullptr) {
                continue;
            }
            ids.insert(mod.info.id);
            ids.insert(mod.info.provides.begin(), mod.info.provides.end());
            for (Addons::Source source : SOURCES) {
                if (const Remote* found = remote(mod, source)) {
                    present[found->projectId].insert(found->versionId);
                }
            }
        }
        for (const Change& change : state.changes) {
            if (change.disabled) {
                continue;
            }
            present[change.projectId].insert(change.target.id);
            const Mods::Mod* old = change.file.empty() ? nullptr : modAt(list, change.file);
            for (Addons::Source source : SOURCES) {
                if (const Remote* found = old != nullptr ? remote(*old, source) : nullptr) {
                    // The replacement still provides this project, but its other source has a different version ID.
                    present.try_emplace(found->projectId);
                }
            }
        }
        auto satisfied = [&](const std::string& projectId) {
            if (present.count(projectId) != 0) {
                return true;
            }
            const Project* info = project(projectId);
            if (info == nullptr) {
                return false;
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
        for (const Requirement& item : requirements(key, state)) {
            const Addons::Dependency& dependency = item.dependency;
            std::string projectId = dependencyProject(dependency);
            const Addons::Version* pinned = dependencyVersion(dependency.versionId);
            if (!dependency.versionId.empty() &&
                (pinned == nullptr || (!projectId.empty() && pinned->projectId != projectId))) {
                push(Severity::Error, "无法确认 " + item.name + " 的前置版本 " + dependency.versionId, {});
                continue;
            }
            if (projectId.empty()) {
                push(Severity::Error, item.name + " 的前置信息无效", {});
                continue;
            }
            auto installed = present.find(projectId);
            bool exact = installed != present.end() && installed->second.count(dependency.versionId) != 0;
            bool unknown = false;
            if (pinned != nullptr && !exact) {
                for (const Change& change : state.changes) {
                    if (change.disabled) {
                        continue;
                    }
                    auto same = sameVersion(change.target, *pinned);
                    exact = exact || same.value_or(false);
                    const Mods::Mod* old = change.file.empty() ? nullptr : modAt(list, change.file);
                    bool related = change.projectId == projectId || (old != nullptr && owns(*old, projectId));
                    unknown = unknown || (related && !same.has_value());
                }
            }
            if (!exact && unknown) {
                push(Severity::Error, "无法确认 " + item.name + " 的指定前置版本，请从 " +
                             Addons::sourceName(pinned->source) + " 选择 " + title(projectId) + " 的对应版本", {});
                continue;
            }
            if (dependency.incompatible) {
                if (dependency.versionId.empty() ? installed != present.end() : exact) {
                    push(Severity::Error, item.name + " 与 " + title(projectId) + " 不兼容", {});
                }
            } else if (dependency.versionId.empty() ? !satisfied(projectId) : !exact) {
                std::string version = pinned != nullptr ? " " + pinned->number : std::string();
                push(Severity::Error, item.name + " 需要前置 " + title(projectId) + version, projectId);
            }
        }
        for (const Change& change : state.changes) {
            if (change.disabled) {
                continue;
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
                    if (names && dependency.required && dependency.client && !dependency.version.empty() &&
                        target.source == Addons::Source::Modrinth &&
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
        if (state.hashing || Mods::of(key).loading) {
            return true;
        }
        for (const Change& change : state.changes) {
            if (state.lookups[change.target.source].running) {
                return true;
            }
        }
        for (const Requirement& item : requirements(key, state)) {
            const Addons::Dependency& dependency = item.dependency;
            if (!dependency.versionId.empty()) {
                auto found = dependencyVersions.find(dependency.versionId);
                if (found == dependencyVersions.end() || found->second.status == Addons::Status::Loading) {
                    return true;
                }
            }
            if (fetchingProjects.count(dependencyProject(dependency)) != 0) {
                return true;
            }
        }
        return !state.adding.empty();
    }

    bool apply(const std::string& key) {
        Instance& state = sync(key);
        if (state.busy || state.changes.empty() || checking(key)) {
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

    bool busy() {
        return std::any_of(instances.begin(), instances.end(), [](const auto& item) { return item.second.busy; });
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

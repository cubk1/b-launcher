#include "app/downloads/PackContents.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/Projects.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>
#include <core/modpack/modpack.hpp>
#include <core/modpack/remote_zip.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace PackContents {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Source;
    using Addons::Status;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr size_t KEPT = 4;
        constexpr size_t MAX_FILES = 4000;
        constexpr size_t MODRINTH_BATCH = 100;
        constexpr size_t CURSE_BATCH = 200;

        AsyncTasks tasks;
        std::map<std::string, Contents> cache;
        Recent use;

        std::string keyOf(const Addons::Version& version) {
            return std::to_string((int) version.source) + ":" + version.id;
        }

        long long number(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<long long>() : 0;
        }

        std::string fileName(const std::string& path) {
            size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::string stem(const std::string& name) {
            size_t dot = name.find_last_of('.');
            return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
        }

        Folder& folderAt(Folder& root, const std::string& path) {
            Folder* current = &root;
            size_t start = 0;
            while (start < path.size()) {
                size_t slash = path.find('/', start);
                std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                start = slash == std::string::npos ? path.size() : slash + 1;
                if (part.empty()) {
                    continue;
                }
                auto found = std::find_if(current->folders.begin(), current->folders.end(),
                                          [&](const Folder& f) { return f.name == part; });
                if (found == current->folders.end()) {
                    current->folders.push_back(Folder{part, {}, {}, 0});
                    current = &current->folders.back();
                } else {
                    current = &*found;
                }
            }
            return *current;
        }

        bool hidden(const std::string& path) {
            size_t start = 0;
            while (start < path.size()) {
                if (path[start] == '.') {
                    return true;
                }
                size_t slash = path.find('/', start);
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }
            return false;
        }

        void place(Folder& root, const std::string& path, Item item) {
            size_t slash = path.find_last_of('/');
            Folder& folder = slash == std::string::npos ? root : folderAt(root, path.substr(0, slash));
            folder.items.push_back(std::move(item));
        }

        int folderRank(const std::string& name) {
            static const char* const order[] = {"mods", "resourcepacks", "shaderpacks", "datapacks", "config"};
            for (int i = 0; i < 5; i++) {
                if (name == order[i]) {
                    return i;
                }
            }
            return 10;
        }

        size_t settle(Folder& folder) {
            std::sort(folder.folders.begin(), folder.folders.end(), [](const Folder& a, const Folder& b) {
                int ra = folderRank(a.name);
                int rb = folderRank(b.name);
                return ra != rb ? ra < rb : blb::to_lower(a.name) < blb::to_lower(b.name);
            });
            std::stable_sort(folder.items.begin(), folder.items.end(), [](const Item& a, const Item& b) {
                if (a.file != b.file) {
                    return !a.file;
                }
                return blb::to_lower(a.title) < blb::to_lower(b.title);
            });
            folder.total = folder.items.size();
            for (Folder& child : folder.folders) {
                folder.total += settle(child);
            }
            return folder.total;
        }

        std::string loaderLabel(const blb::install::loader_request& loader) {
            std::string name = Modrinth::label(std::string(blb::install::loader_name(loader.type)));
            return loader.version.empty() ? name : name + " " + loader.version;
        }

        void resolveModrinth(const blb::modpack::pack_manifest& manifest, Folder& root, blb::cancellation_token token) {
            std::vector<std::string> hashes;
            for (const auto& file : manifest.files) {
                if (!file.sha1.empty()) {
                    hashes.push_back(file.sha1);
                }
            }
            std::map<std::string, Addons::Version> versions;
            for (size_t i = 0; i < hashes.size(); i += MODRINTH_BATCH) {
                std::vector<std::string> chunk(hashes.begin() + (std::ptrdiff_t) i,
                                               hashes.begin() +
                                                       (std::ptrdiff_t) std::min(hashes.size(), i + MODRINTH_BATCH));
                auto doc = Modrinth::post("/version_files", json{{"hashes", chunk}, {"algorithm", "sha1"}}, token);
                if (!doc || !doc->is_object()) {
                    continue;
                }
                for (auto it = doc->begin(); it != doc->end(); ++it) {
                    if (it->is_object()) {
                        versions[blb::to_lower(it.key())] = Modrinth::versionFrom(*it);
                    }
                }
            }
            std::vector<std::string> ids;
            std::set<std::string> seen;
            for (const auto& [hash, found] : versions) {
                if (!found.projectId.empty() && seen.insert(found.projectId).second) {
                    ids.push_back(found.projectId);
                }
            }
            std::map<std::string, json> projects;
            for (size_t i = 0; i < ids.size(); i += MODRINTH_BATCH) {
                std::vector<std::string> chunk(ids.begin() + (std::ptrdiff_t) i,
                                               ids.begin() + (std::ptrdiff_t) std::min(ids.size(), i + MODRINTH_BATCH));
                auto doc = Modrinth::api("/projects?ids=" + blb::url_encode(json(chunk).dump()), token);
                if (!doc || !doc->is_array()) {
                    continue;
                }
                for (const json& p : *doc) {
                    projects[blb::json_text(p, "id")] = p;
                }
            }
            for (const auto& file : manifest.files) {
                Item item;
                item.name = fileName(file.path);
                item.title = stem(item.name);
                item.source = Source::Modrinth;
                item.size = file.size;
                item.optional = file.optional;
                auto version = versions.find(blb::to_lower(file.sha1));
                if (version != versions.end()) {
                    item.projectId = version->second.projectId;
                    item.version = version->second.number;
                    item.dependencies = version->second.dependencies;
                    auto project = projects.find(item.projectId);
                    if (project != projects.end()) {
                        const json& p = project->second;
                        item.title = blb::json_text(p, "title");
                        item.iconUrl = blb::json_text(p, "icon_url");
                        item.slug = blb::json_text(p, "slug");
                        item.client = blb::json_text(p, "client_side");
                        item.server = blb::json_text(p, "server_side");
                        item.resolved = true;
                    }
                }
                place(root, file.path, std::move(item));
            }
        }

        std::string curseFolder(long long classId) {
            switch (classId) {
                case 12: return "resourcepacks";
                case 6552: return "shaderpacks";
                case 6945: return "datapacks";
                case 17: return "saves";
                default: return "mods";
            }
        }

        void resolveCurse(const blb::modpack::pack_manifest& manifest, Folder& root, blb::cancellation_token token) {
            std::vector<long long> fileIds;
            for (const auto& file : manifest.files) {
                fileIds.push_back(file.curseforge_file);
            }
            std::map<long long, json> files;
            for (size_t i = 0; i < fileIds.size(); i += CURSE_BATCH) {
                std::vector<long long> chunk(fileIds.begin() + (std::ptrdiff_t) i,
                                             fileIds.begin() +
                                                     (std::ptrdiff_t) std::min(fileIds.size(), i + CURSE_BATCH));
                auto doc = CurseForge::call("/mods/files", json{{"fileIds", chunk}}.dump(), token);
                if (!doc) {
                    continue;
                }
                if (auto data = doc->find("data"); data != doc->end() && data->is_array()) {
                    for (const json& f : *data) {
                        files[number(f, "id")] = f;
                    }
                }
            }
            std::vector<long long> modIds;
            for (const auto& file : manifest.files) {
                modIds.push_back(file.curseforge_project);
            }
            std::map<long long, json> mods;
            for (size_t i = 0; i < modIds.size(); i += CURSE_BATCH) {
                std::vector<long long> chunk(modIds.begin() + (std::ptrdiff_t) i,
                                             modIds.begin() +
                                                     (std::ptrdiff_t) std::min(modIds.size(), i + CURSE_BATCH));
                auto doc = CurseForge::call("/mods", json{{"modIds", chunk}, {"filterPcOnly", true}}.dump(), token);
                if (!doc) {
                    continue;
                }
                if (auto data = doc->find("data"); data != doc->end() && data->is_array()) {
                    for (const json& m : *data) {
                        mods[number(m, "id")] = m;
                    }
                }
            }
            for (const auto& file : manifest.files) {
                Item item;
                item.source = Source::CurseForge;
                item.projectId = std::to_string(file.curseforge_project);
                item.optional = file.optional;
                item.title = "#" + item.projectId;
                std::string folder = "mods";
                auto mod = mods.find(file.curseforge_project);
                if (mod != mods.end()) {
                    Addons::Hit hit = CurseForge::hitFrom(mod->second);
                    item.title = hit.title;
                    item.iconUrl = hit.iconUrl;
                    item.slug = hit.slug;
                    item.resolved = true;
                    folder = curseFolder(number(mod->second, "classId"));
                }
                auto found = files.find(file.curseforge_file);
                if (found != files.end()) {
                    const json& f = found->second;
                    item.name = blb::json_text(f, "fileName");
                    item.version = blb::json_text(f, "displayName");
                    item.size = number(f, "fileLength");
                    item.manual = blb::json_text(f, "downloadUrl").empty();
                    item.dependencies = CurseForge::versionFrom(f).dependencies;
                }
                if (item.name.empty()) {
                    item.name = item.title;
                }
                place(root, folder + "/" + item.name, std::move(item));
            }
        }

        blb::result<Contents> read(const Addons::Version& version, blb::cancellation_token token) {
            const Addons::File* file = version.file();
            if (file == nullptr || file->url.empty()) {
                return blb::make_error(blb::errc::not_found, "这个版本没有可下载的文件");
            }
            BLB_TRY_ASSIGN(auto zip,
                           blb::modpack::remote_zip::open(Addons::fileUrls(file->url),
                                                          file->size > 0 ? (std::uint64_t) file->size : 0, token));
            std::string index = zip.find("modrinth.index.json") != nullptr ? "modrinth.index.json"
                    : zip.find("manifest.json") != nullptr                 ? "manifest.json"
                                                                           : std::string();
            if (index.empty()) {
                return blb::make_error(blb::errc::unsupported, "不是 Modrinth 或 CurseForge 格式的整合包");
            }
            BLB_TRY_ASSIGN(std::string text, zip.read(index, token));
            BLB_TRY_ASSIGN(auto manifest, blb::modpack::parse_pack_index(index, text));
            Contents out;
            out.minecraft = manifest.minecraft;
            for (const auto& loader : manifest.loaders) {
                out.loaders.push_back(loaderLabel(loader));
            }
            if (manifest.format == blb::modpack::pack_format::modrinth) {
                resolveModrinth(manifest, out.root, token);
            } else {
                resolveCurse(manifest, out.root, token);
            }
            size_t listed = 0;
            for (const auto& entry : zip.entries()) {
                if (entry.name.empty() || entry.name.back() == '/') {
                    continue;
                }
                for (const std::string& prefix : manifest.overrides) {
                    if (entry.name.rfind(prefix, 0) != 0) {
                        continue;
                    }
                    std::string path = entry.name.substr(prefix.size());
                    if (path.empty() || hidden(path)) {
                        break;
                    }
                    Item item;
                    item.name = fileName(path);
                    item.title = item.name;
                    item.size = (long long) entry.size;
                    bool jar = path.rfind("mods/", 0) == 0 && path.find('/', 5) == std::string::npos &&
                            path.size() > 4 && blb::to_lower(path.substr(path.size() - 4)) == ".jar";
                    if (jar) {
                        item.title = stem(item.name);
                        item.bundled = true;
                        out.bundled++;
                    } else {
                        item.file = true;
                        if (listed >= MAX_FILES) {
                            break;
                        }
                        listed++;
                    }
                    place(out.root, path, std::move(item));
                    break;
                }
            }
            settle(out.root);
            for (const Folder& folder : out.root.folders) {
                if (folder.name == "mods") {
                    for (const Item& item : folder.items) {
                        if (!item.file) {
                            out.mods++;
                        }
                    }
                }
            }
            out.downloaded = zip.downloaded();
            out.size = zip.size();
            out.status = Status::Ready;
            return out;
        }

        void fetch(const Addons::Version& version) {
            std::string key = keyOf(version);
            Contents& entry = cache[key];
            entry.status = Status::Loading;
            entry.error.clear();
            tasks.run(
                    [key, version](blb::cancellation_token token) -> Continuation {
                        auto result = read(version, token);
                        if (!result) {
                            std::string error = result.err().message;
                            return [key, error] {
                                cache[key].status = Status::Failed;
                                cache[key].error = error;
                            };
                        }
                        return [key, contents = std::move(*result)]() mutable { cache[key] = std::move(contents); };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            cache[key].status = Status::Failed;
                            cache[key].error = error;
                        };
                    });
        }
    }

    const Contents& contents(const Addons::Version& version) {
        std::string key = keyOf(version);
        use.use(key);
        auto found = cache.find(key);
        if (found == cache.end()) {
            fetch(version);
            found = cache.find(key);
        }
        return found->second;
    }

    void refresh(const Addons::Version& version) {
        auto found = cache.find(keyOf(version));
        if (found == cache.end() || found->second.status != Status::Loading) {
            fetch(version);
        }
    }

    void poll() {
        tasks.poll();
        use.trim(cache, KEPT, [](const Contents& c) { return c.status != Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
    }
}

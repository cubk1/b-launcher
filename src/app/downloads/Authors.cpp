#include "app/downloads/Authors.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/Mcmod.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <map>

namespace Authors {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Status;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr size_t KEPT = 8;
        constexpr int CURSE_PAGE = 50;
        constexpr int CURSE_PAGES = 4;

        AsyncTasks tasks;
        std::map<std::string, Profile> profiles;
        Recent profileUse;

        std::string keyOf(Kind kind, const std::string& id) {
            return std::to_string((int) kind) + ":" + id;
        }

        long long number(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<long long>() : 0;
        }

        Project modrinthProject(const json& p, bool v3) {
            Project project;
            Addons::Hit& hit = project.hit;
            hit.id = blb::json_text(p, "id");
            hit.slug = blb::json_text(p, "slug");
            hit.title = blb::json_text(p, v3 ? "name" : "title");
            hit.description = blb::json_text(p, v3 ? "summary" : "description");
            hit.iconUrl = blb::json_text(p, "icon_url");
            hit.downloads = number(p, "downloads");
            hit.follows = number(p, "followers");
            hit.loaders = blb::json_strings(p, "loaders");
            hit.categories = blb::json_strings(p, "categories");
            std::string type = blb::json_text(p, "project_type");
            if (v3) {
                std::vector<std::string> types = blb::json_strings(p, "project_types");
                type = types.empty() ? std::string("mod") : types.front();
            }
            project.kind = Modrinth::kindOf(type);
            hit.pageUrl = "https://modrinth.com/" + (type.empty() ? std::string("mod") : type) + "/" +
                    (hit.slug.empty() ? hit.id : hit.slug);
            project.updated = blb::json_text(p, "updated");
            return project;
        }

        void finish(Profile& profile) {
            profile.status = Status::Ready;
            profile.downloads = 0;
            for (const Project& project : profile.projects) {
                profile.downloads += project.hit.downloads;
            }
            std::stable_sort(profile.projects.begin(), profile.projects.end(),
                             [](const Project& a, const Project& b) { return a.hit.downloads > b.hit.downloads; });
        }

        blb::result<Profile> modrinthUser(const std::string& id, blb::cancellation_token token) {
            BLB_TRY_ASSIGN(json user, Modrinth::api("/user/" + blb::url_encode(id), token));
            Profile profile;
            profile.name = blb::json_text(user, "username");
            profile.avatar = blb::json_text(user, "avatar_url");
            profile.bio = blb::json_text(user, "bio");
            profile.joined = blb::json_text(user, "created");
            profile.url = "https://modrinth.com/user/" + profile.name;
            BLB_TRY_ASSIGN(json projects, Modrinth::api("/user/" + blb::url_encode(id) + "/projects", token));
            if (projects.is_array()) {
                for (const json& p : projects) {
                    profile.projects.push_back(modrinthProject(p, false));
                }
            }
            finish(profile);
            return profile;
        }

        blb::result<Profile> modrinthOrganization(const std::string& id, blb::cancellation_token token) {
            const std::string base = "https://api.modrinth.com/v3/organization/" + blb::url_encode(id);
            BLB_TRY_ASSIGN(json org, Modrinth::fetchJson(base, token));
            Profile profile;
            profile.name = blb::json_text(org, "name");
            profile.avatar = blb::json_text(org, "icon_url");
            profile.bio = blb::json_text(org, "description");
            std::string slug = blb::json_text(org, "slug");
            profile.url = "https://modrinth.com/organization/" + (slug.empty() ? id : slug);
            BLB_TRY_ASSIGN(json projects, Modrinth::fetchJson(base + "/projects", token));
            if (projects.is_array()) {
                for (const json& p : projects) {
                    profile.projects.push_back(modrinthProject(p, true));
                }
            }
            finish(profile);
            return profile;
        }

        blb::result<Profile> curseAuthor(const std::string& id, const std::string& name, const std::string& avatar,
                                         blb::cancellation_token token) {
            Profile profile;
            profile.name = name;
            profile.avatar = avatar;
            for (int page = 0; page < CURSE_PAGES; page++) {
                std::string path = "/mods/search?gameId=432&authorId=" + blb::url_encode(id) +
                        "&sortField=6&sortOrder=desc&pageSize=" + std::to_string(CURSE_PAGE) +
                        "&index=" + std::to_string(page * CURSE_PAGE);
                auto doc = CurseForge::call(path, std::string(), token);
                if (!doc) {
                    if (page == 0) {
                        return doc.err();
                    }
                    break;
                }
                size_t got = 0;
                if (auto data = doc->find("data"); data != doc->end() && data->is_array()) {
                    for (const json& item : *data) {
                        Project project;
                        project.hit = CurseForge::hitFrom(item);
                        project.kind = CurseForge::kindOf(number(item, "classId"));
                        project.updated = blb::json_text(item, "dateModified");
                        if (profile.name.empty() || profile.avatar.empty()) {
                            if (auto authors = item.find("authors"); authors != item.end() && authors->is_array()) {
                                for (const json& author : *authors) {
                                    if (std::to_string(number(author, "id")) == id) {
                                        if (profile.name.empty()) {
                                            profile.name = blb::json_text(author, "name");
                                        }
                                        if (profile.avatar.empty()) {
                                            profile.avatar = blb::json_text(author, "avatarUrl");
                                        }
                                        profile.url = blb::json_text(author, "url");
                                    }
                                }
                            }
                        }
                        profile.projects.push_back(std::move(project));
                        got++;
                    }
                }
                long long total = 0;
                if (auto pagination = doc->find("pagination"); pagination != doc->end() && pagination->is_object()) {
                    total = number(*pagination, "totalCount");
                }
                if (got < (size_t) CURSE_PAGE || (long long) profile.projects.size() >= total) {
                    break;
                }
            }
            finish(profile);
            return profile;
        }

        blb::result<Profile> mcmodAuthor(const std::string& id, blb::cancellation_token token) {
            BLB_TRY_ASSIGN(json doc, Mcmod::call("/authors/" + blb::url_encode(id), token));
            Profile profile;
            profile.name = blb::json_text(doc, "name");
            profile.avatar = blb::json_text(doc, "avatar");
            profile.url = blb::json_text(doc, "url");
            profile.aliases = blb::json_strings(doc, "aliases");
            if (auto projects = doc.find("projects"); projects != doc.end() && projects->is_array()) {
                for (const json& item : *projects) {
                    Mcmod::Ref ref = Mcmod::refOf(item);
                    Project project;
                    project.hit.title = ref.en.empty() ? ref.zh : ref.en;
                    project.hit.pageUrl = ref.url;
                    project.kind = ref.type == "modpack" ? Addons::Kind::Modpack : Addons::Kind::Mod;
                    project.mcmodId = ref.id;
                    project.mcmodType = ref.type;
                    project.roles = ref.roles;
                    project.keys = ref.keys;
                    profile.projects.push_back(std::move(project));
                }
            }
            profile.status = Status::Ready;
            return profile;
        }

        void fetch(Kind kind, const std::string& id, const std::string& name, const std::string& avatar) {
            std::string key = keyOf(kind, id);
            Profile& entry = profiles[key];
            entry.status = Status::Loading;
            entry.error.clear();
            if (entry.name.empty()) {
                entry.name = name;
                entry.avatar = avatar;
            }
            tasks.run(
                    [key, kind, id, name, avatar](blb::cancellation_token token) -> Continuation {
                        blb::result<Profile> result = blb::make_error(blb::errc::unsupported);
                        switch (kind) {
                            case Kind::ModrinthUser: result = modrinthUser(id, token); break;
                            case Kind::ModrinthOrganization: result = modrinthOrganization(id, token); break;
                            case Kind::CurseForge: result = curseAuthor(id, name, avatar, token); break;
                            case Kind::Mcmod: result = mcmodAuthor(id, token); break;
                        }
                        if (!result) {
                            std::string error = result.err().message;
                            return [key, error] {
                                profiles[key].status = Status::Failed;
                                profiles[key].error = error;
                            };
                        }
                        return [key, profile = std::move(*result)]() mutable { profiles[key] = std::move(profile); };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            profiles[key].status = Status::Failed;
                            profiles[key].error = error;
                        };
                    });
        }
    }

    const Profile& profile(Kind kind, const std::string& id, const std::string& name, const std::string& avatar) {
        std::string key = keyOf(kind, id);
        profileUse.use(key);
        auto found = profiles.find(key);
        if (found == profiles.end()) {
            fetch(kind, id, name, avatar);
            found = profiles.find(key);
        }
        return found->second;
    }

    void refresh(Kind kind, const std::string& id) {
        auto found = profiles.find(keyOf(kind, id));
        if (found == profiles.end() || found->second.status != Status::Loading) {
            fetch(kind, id, found != profiles.end() ? found->second.name : std::string(),
                  found != profiles.end() ? found->second.avatar : std::string());
        }
    }

    void poll() {
        tasks.poll();
        profileUse.trim(profiles, KEPT, [](const Profile& p) { return p.status != Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
    }
}

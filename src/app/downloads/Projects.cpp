#include "app/downloads/Projects.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace Projects {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Kind;
    using Addons::Source;
    using Addons::Status;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr size_t KEPT_DETAILS = 12;
        constexpr size_t KEPT_CHANGELOGS = 80;
        constexpr size_t BRIEF_BATCH = 80;

        AsyncTasks tasks;
        std::map<std::string, Detail> details;
        std::map<std::string, Changelog> changelogs;
        std::map<std::string, Brief> briefs;
        std::set<std::string> wanted[2];
        std::set<std::string> asked[2];
        bool fetching[2] = {false, false};
        Recent detailUse, changelogUse;

        std::string keyOf(Source source, const std::string& id) {
            return std::to_string((int) source) + ":" + id;
        }

        long long number(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<long long>() : 0;
        }

        std::string idText(const json& node, const char* key) {
            auto it = node.find(key);
            if (it == node.end()) {
                return std::string();
            }
            if (it->is_number_integer()) {
                return std::to_string(it->get<long long>());
            }
            return it->is_string() ? it->get<std::string>() : std::string();
        }

        void addLink(Detail& d, const std::string& label, const std::string& url, int glyph) {
            if (!url.empty()) {
                d.links.push_back(Link{label, url, glyph});
            }
        }

        Detail modrinthDetail(const json& p) {
            Detail d;
            d.status = Status::Ready;
            d.source = Source::Modrinth;
            d.id = blb::json_text(p, "id");
            d.slug = blb::json_text(p, "slug");
            d.title = blb::json_text(p, "title");
            d.summary = blb::json_text(p, "description");
            d.body = blb::json_text(p, "body");
            d.bodyLoaded = true;
            d.iconUrl = blb::json_text(p, "icon_url");
            std::string type = blb::json_text(p, "project_type");
            d.kind = Modrinth::kindOf(type);
            d.pageUrl = "https://modrinth.com/" + (type.empty() ? std::string("mod") : type) + "/" +
                    (d.slug.empty() ? d.id : d.slug);
            d.downloads = number(p, "downloads");
            d.followers = number(p, "followers");
            d.published = blb::json_text(p, "published");
            d.updated = blb::json_text(p, "updated");
            if (auto license = p.find("license"); license != p.end() && license->is_object()) {
                d.license = licenseName(blb::json_text(*license, "id"), blb::json_text(*license, "name"));
                d.licenseUrl = blb::json_text(*license, "url");
            }
            d.gameVersions = blb::json_strings(p, "game_versions");
            d.loaders = blb::json_strings(p, "loaders");
            d.categories = blb::json_strings(p, "categories");
            for (const std::string& extra : blb::json_strings(p, "additional_categories")) {
                if (std::find(d.categories.begin(), d.categories.end(), extra) == d.categories.end()) {
                    d.categories.push_back(extra);
                }
            }
            d.client = blb::json_text(p, "client_side");
            d.server = blb::json_text(p, "server_side");
            addLink(d, "问题反馈", blb::json_text(p, "issues_url"), 0xf188);
            addLink(d, "源代码", blb::json_text(p, "source_url"), 0xf121);
            addLink(d, "Wiki", blb::json_text(p, "wiki_url"), 0xf02d);
            addLink(d, "Discord", blb::json_text(p, "discord_url"), 0xf086);
            if (auto donations = p.find("donation_urls"); donations != p.end() && donations->is_array()) {
                for (const json& donation : *donations) {
                    std::string platform = blb::json_text(donation, "platform");
                    addLink(d, platform.empty() || platform == "Other" ? "捐助" : platform,
                            blb::json_text(donation, "url"), 0xf004);
                }
            }
            if (auto gallery = p.find("gallery"); gallery != p.end() && gallery->is_array()) {
                std::vector<std::pair<long long, Image>> ordered;
                for (const json& item : *gallery) {
                    Image image;
                    image.thumb = blb::json_text(item, "url");
                    image.full = blb::json_text(item, "raw_url");
                    if (image.full.empty()) {
                        image.full = image.thumb;
                    }
                    image.title = blb::json_text(item, "title");
                    image.description = blb::json_text(item, "description");
                    long long order = number(item, "ordering");
                    if (item.value("featured", false)) {
                        order -= 1000000;
                    }
                    ordered.emplace_back(order, std::move(image));
                }
                std::stable_sort(ordered.begin(), ordered.end(),
                                 [](const auto& a, const auto& b) { return a.first < b.first; });
                for (auto& [order, image] : ordered) {
                    d.gallery.push_back(std::move(image));
                }
            }
            return d;
        }

        std::vector<Member> modrinthMembers(const json& members, const json& organization) {
            std::vector<Member> out;
            if (organization.is_object() && !blb::json_text(organization, "id").empty()) {
                Member org;
                org.id = blb::json_text(organization, "id");
                org.name = blb::json_text(organization, "name");
                org.avatar = blb::json_text(organization, "icon_url");
                org.role = "组织";
                org.organization = true;
                std::string slug = blb::json_text(organization, "slug");
                org.url = "https://modrinth.com/organization/" + (slug.empty() ? org.id : slug);
                out.push_back(std::move(org));
            }
            if (!members.is_array()) {
                return out;
            }
            std::vector<std::pair<long long, Member>> ordered;
            for (const json& member : members) {
                auto user = member.find("user");
                if (user == member.end() || !user->is_object()) {
                    continue;
                }
                Member m;
                m.id = blb::json_text(*user, "id");
                m.name = blb::json_text(*user, "username");
                m.avatar = blb::json_text(*user, "avatar_url");
                m.role = blb::json_text(member, "role");
                m.url = "https://modrinth.com/user/" + m.name;
                ordered.emplace_back(number(member, "ordering"), std::move(m));
            }
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& [order, m] : ordered) {
                out.push_back(std::move(m));
            }
            return out;
        }

        Detail curseDetail(const json& m) {
            Detail d;
            d.status = Status::Ready;
            d.source = Source::CurseForge;
            Addons::Hit hit = CurseForge::hitFrom(m);
            d.id = hit.id;
            d.slug = hit.slug;
            d.title = hit.title;
            d.summary = hit.description;
            d.iconUrl = hit.iconUrl;
            d.pageUrl = hit.pageUrl;
            d.downloads = hit.downloads;
            d.followers = hit.follows;
            d.categories = hit.categories;
            d.loaders = hit.loaders;
            d.kind = CurseForge::kindOf(number(m, "classId"));
            d.published = blb::json_text(m, "dateCreated");
            d.updated = blb::json_text(m, "dateModified");
            if (auto links = m.find("links"); links != m.end() && links->is_object()) {
                addLink(d, "问题反馈", blb::json_text(*links, "issuesUrl"), 0xf188);
                addLink(d, "源代码", blb::json_text(*links, "sourceUrl"), 0xf121);
                addLink(d, "Wiki", blb::json_text(*links, "wikiUrl"), 0xf02d);
            }
            std::set<std::string> versions;
            if (auto files = m.find("latestFilesIndexes"); files != m.end() && files->is_array()) {
                for (const json& file : *files) {
                    std::string version = blb::json_text(file, "gameVersion");
                    if (!version.empty() && versions.insert(version).second) {
                        d.gameVersions.push_back(version);
                    }
                }
            }
            if (auto screenshots = m.find("screenshots"); screenshots != m.end() && screenshots->is_array()) {
                for (const json& shot : *screenshots) {
                    Image image;
                    image.thumb = blb::json_text(shot, "thumbnailUrl");
                    image.full = blb::json_text(shot, "url");
                    if (image.thumb.empty()) {
                        image.thumb = image.full;
                    }
                    image.title = blb::json_text(shot, "title");
                    image.description = blb::json_text(shot, "description");
                    d.gallery.push_back(std::move(image));
                }
            }
            if (auto authors = m.find("authors"); authors != m.end() && authors->is_array()) {
                for (const json& author : *authors) {
                    Member member;
                    member.id = idText(author, "id");
                    member.name = blb::json_text(author, "name");
                    member.avatar = blb::json_text(author, "avatarUrl");
                    member.url = blb::json_text(author, "url");
                    member.role = "作者";
                    d.members.push_back(std::move(member));
                }
            }
            d.membersLoaded = true;
            return d;
        }

        void failDetail(const std::string& key, const std::string& error) {
            auto found = details.find(key);
            if (found != details.end()) {
                found->second.status = Status::Failed;
                found->second.error = error;
            }
        }

        void fetchDetail(Source source, const std::string& id) {
            std::string key = keyOf(source, id);
            Detail& entry = details[key];
            entry.status = Status::Loading;
            entry.error.clear();
            entry.source = source;
            entry.id = id;
            auto failure = [key](const std::string& error) -> Continuation {
                return [key, error] { failDetail(key, error); };
            };
            if (source == Source::CurseForge) {
                tasks.run(
                        [key, id](blb::cancellation_token token) -> Continuation {
                            auto doc = CurseForge::call("/mods/" + blb::url_encode(id), std::string(), token);
                            if (!doc) {
                                std::string error = doc.err().message;
                                return [key, error] { failDetail(key, error); };
                            }
                            auto data = doc->find("data");
                            if (data == doc->end() || !data->is_object()) {
                                return [key] { failDetail(key, "CurseForge 上找不到这个项目"); };
                            }
                            Detail d = curseDetail(*data);
                            auto description = CurseForge::call("/mods/" + blb::url_encode(id) + "/description",
                                                                std::string(), token);
                            if (description) {
                                d.body = blb::json_text(*description, "data");
                                d.html = true;
                            }
                            d.bodyLoaded = true;
                            return [key, d = std::move(d)]() mutable { details[key] = std::move(d); };
                        },
                        failure);
                return;
            }
            tasks.run(
                    [key, id](blb::cancellation_token token) -> Continuation {
                        auto doc = Modrinth::api("/project/" + blb::url_encode(id), token);
                        if (!doc) {
                            std::string error = doc.err().message;
                            return [key, error] { failDetail(key, error); };
                        }
                        if (!doc->is_object()) {
                            return [key] { failDetail(key, "Modrinth 上找不到这个项目"); };
                        }
                        Detail d = modrinthDetail(*doc);
                        return [key, d = std::move(d)]() mutable {
                            Detail& slot = details[key];
                            std::vector<Member> members = std::move(slot.members);
                            bool loaded = slot.membersLoaded;
                            slot = std::move(d);
                            slot.members = std::move(members);
                            slot.membersLoaded = loaded;
                        };
                    },
                    failure);
            tasks.run(
                    [key, id](blb::cancellation_token token) -> Continuation {
                        auto members = Modrinth::api("/project/" + blb::url_encode(id) + "/members", token);
                        auto organization = Modrinth::fetchJson(
                                "https://api.modrinth.com/v3/project/" + blb::url_encode(id) + "/organization", token);
                        std::vector<Member> list =
                                modrinthMembers(members ? *members : json(), organization ? *organization : json());
                        return [key, list = std::move(list)]() mutable {
                            Detail& slot = details[key];
                            slot.members = std::move(list);
                            slot.membersLoaded = true;
                        };
                    },
                    [key](const std::string&) -> Continuation { return [key] { details[key].membersLoaded = true; }; });
        }

        void fetchBriefs(Source source) {
            std::vector<std::string> ids;
            for (const std::string& id : wanted[(int) source]) {
                if (ids.size() >= BRIEF_BATCH) {
                    break;
                }
                ids.push_back(id);
            }
            for (const std::string& id : ids) {
                wanted[(int) source].erase(id);
                asked[(int) source].insert(id);
            }
            if (ids.empty()) {
                return;
            }
            fetching[(int) source] = true;
            tasks.run(
                    [source, ids](blb::cancellation_token token) -> Continuation {
                        std::vector<Brief> found;
                        if (source == Source::CurseForge) {
                            auto hits = CurseForge::fetchProjects(ids, token);
                            if (hits) {
                                for (const Addons::Hit& hit : *hits) {
                                    found.push_back(Brief{hit.id, hit.slug, hit.title, hit.iconUrl, Kind::Mod});
                                }
                            }
                        } else {
                            auto doc = Modrinth::api("/projects?ids=" + blb::url_encode(json(ids).dump()), token);
                            if (doc && doc->is_array()) {
                                for (const json& p : *doc) {
                                    found.push_back(Brief{blb::json_text(p, "id"), blb::json_text(p, "slug"),
                                                          blb::json_text(p, "title"), blb::json_text(p, "icon_url"),
                                                          Modrinth::kindOf(blb::json_text(p, "project_type"))});
                                }
                            }
                        }
                        return [source, found = std::move(found)] {
                            fetching[(int) source] = false;
                            for (const Brief& brief : found) {
                                remember(source, brief);
                            }
                        };
                    },
                    [source](const std::string&) -> Continuation {
                        return [source] { fetching[(int) source] = false; };
                    });
        }

        void fetchChangelog(const Addons::Version& version) {
            std::string key = keyOf(version.source, version.id);
            changelogs[key].status = Status::Loading;
            std::string projectId = version.projectId;
            std::string fileId = version.id;
            tasks.run(
                    [key, projectId, fileId](blb::cancellation_token token) -> Continuation {
                        auto doc = CurseForge::call("/mods/" + blb::url_encode(projectId) + "/files/" +
                                                            blb::url_encode(fileId) + "/changelog",
                                                    std::string(), token);
                        if (!doc) {
                            return [key] { changelogs[key].status = Status::Failed; };
                        }
                        std::string text = blb::json_text(*doc, "data");
                        return [key, text] {
                            Changelog& entry = changelogs[key];
                            entry.status = Status::Ready;
                            entry.text = text;
                            entry.html = true;
                        };
                    },
                    [key](const std::string&) -> Continuation {
                        return [key] { changelogs[key].status = Status::Failed; };
                    });
        }
    }

    const Detail& detail(Source source, const std::string& id) {
        std::string key = keyOf(source, id);
        detailUse.use(key);
        auto found = details.find(key);
        if (found == details.end()) {
            fetchDetail(source, id);
            found = details.find(key);
        }
        return found->second;
    }

    void refresh(Source source, const std::string& id) {
        auto found = details.find(keyOf(source, id));
        if (found == details.end() || found->second.status != Status::Loading) {
            fetchDetail(source, id);
        }
    }

    const Brief* brief(Source source, const std::string& id) {
        if (id.empty()) {
            return nullptr;
        }
        auto found = briefs.find(keyOf(source, id));
        if (found != briefs.end()) {
            return &found->second;
        }
        if (!asked[(int) source].count(id)) {
            wanted[(int) source].insert(id);
        }
        return nullptr;
    }

    void remember(Source source, const Brief& brief) {
        if (brief.id.empty()) {
            return;
        }
        briefs[keyOf(source, brief.id)] = brief;
        if (!brief.slug.empty()) {
            briefs[keyOf(source, brief.slug)] = brief;
        }
    }

    const Changelog& changelog(const Addons::Version& version) {
        static Changelog none;
        if (version.source == Source::Modrinth) {
            none.status = Status::Ready;
            none.text = version.changelog;
            none.html = false;
            return none;
        }
        std::string key = keyOf(version.source, version.id);
        changelogUse.use(key);
        auto found = changelogs.find(key);
        if (found == changelogs.end()) {
            fetchChangelog(version);
            found = changelogs.find(key);
        }
        return found->second;
    }

    std::string licenseName(const std::string& id, const std::string& name) {
        if (!name.empty() && name != "Custom") {
            return name;
        }
        std::string text = id;
        const std::string prefix = "LicenseRef-";
        if (text.rfind(prefix, 0) == 0) {
            text = text.substr(prefix.size());
            std::replace(text.begin(), text.end(), '-', ' ');
        }
        if (text.empty() || blb::to_lower(text) == "unknown") {
            return std::string();
        }
        if (text == "All Rights Reserved") {
            return "保留所有权利";
        }
        return text;
    }

    void poll() {
        tasks.poll();
        for (Source source : {Source::Modrinth, Source::CurseForge}) {
            if (!fetching[(int) source] && !wanted[(int) source].empty()) {
                fetchBriefs(source);
            }
        }
        detailUse.trim(details, KEPT_DETAILS, [](const Detail& d) { return d.status != Status::Loading; });
        changelogUse.trim(changelogs, KEPT_CHANGELOGS, [](const Changelog& c) { return c.status != Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
    }
}

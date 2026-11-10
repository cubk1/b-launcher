#include "app/downloads/ProjectBrowser.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Instances.h"

#include <algorithm>
#include <map>
#include <string>

namespace ProjectBrowser {

    namespace {
        bool separator(char c) {
            return c == ' ' || c == '-' || c == '_' || c == ':' || c == '|' || c == '/' || c == '(' || c == '[';
        }

        char lower(char c) {
            return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
        }

        size_t sameStart(const std::string& text, const std::string& head) {
            size_t same = 0;
            while (same < head.size() && same < text.size() && lower(text[same]) == lower(head[same])) {
                same++;
            }
            return same;
        }

        std::string dropStart(const std::string& name, const std::string& head) {
            if (head.empty() || sameStart(name, head) != head.size()) {
                return name;
            }
            if (head.size() < name.size() && !separator(head.back()) && !separator(name[head.size()])) {
                return name;
            }
            size_t start = head.size();
            while (start < name.size() && separator(name[start])) {
                start++;
            }
            return name.substr(start);
        }

        // Lowercase letters and digits only: spaces, punctuation and CJK drop out, so names compare across platforms.
        std::string normalised(const std::string& text) {
            std::string out;
            for (char c : text) {
                if (c >= 'A' && c <= 'Z') {
                    out += (char) (c - 'A' + 'a');
                } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    out += c;
                }
            }
            return out;
        }

        void appendUnique(std::vector<std::string>& into, const std::vector<std::string>& values) {
            for (const std::string& value : values) {
                if (std::find(into.begin(), into.end(), value) == into.end()) {
                    into.push_back(value);
                }
            }
        }

        bool sameProject(const Addons::Hit& a, const Addons::Hit& b) {
            std::string name = normalised(a.title);
            std::string slug = normalised(a.slug);
            return (!name.empty() && name == normalised(b.title)) || (!slug.empty() && slug == normalised(b.slug));
        }
    }

    bool usesInstance(Addons::Kind kind) {
        return kind != Addons::Kind::Modpack;
    }

    bool hasLoaders(Addons::Source source, Addons::Kind kind) {
        return kind == Addons::Kind::Mod || kind == Addons::Kind::Modpack ||
                (source != Addons::Source::CurseForge && kind == Addons::Kind::Shader);
    }

    const std::vector<std::string>& loaderTags(Addons::Source source, Addons::Kind kind) {
        if (source == Addons::Source::CurseForge) {
            static const std::vector<std::string> packs = {"forge", "fabric", "neoforge", "quilt"};
            static const std::vector<std::string> mods = {"forge", "fabric", "neoforge", "quilt", "liteloader"};
            return kind == Addons::Kind::Modpack ? packs : mods;
        }
        static const std::vector<std::string> shaders = {"iris", "optifine", "canvas", "vanilla"};
        static const std::vector<std::string> packs = {"fabric", "forge", "neoforge", "quilt"};
        static const std::vector<std::string> mods = {"fabric", "forge", "neoforge", "quilt", "legacy-fabric", "liteloader"};
        if (kind == Addons::Kind::Shader) {
            return shaders;
        }
        return kind == Addons::Kind::Modpack ? packs : mods;
    }

    const std::vector<SortOption>& sorts(Addons::Source source) {
        using Addons::Sort;
        static const std::vector<SortOption> modrinth = {
                {Sort::Relevance, "相关度"}, {Sort::Downloads, "下载量"}, {Sort::Follows, "关注数"},
                {Sort::Newest, "最新发布"}, {Sort::Updated, "最近更新"},
        };
        static const std::vector<SortOption> curseforge = {
                {Sort::Popularity, "热门"}, {Sort::Downloads, "下载量"}, {Sort::Updated, "最近更新"},
                {Sort::Newest, "最新发布"}, {Sort::Name, "名称"},
        };
        return source == Addons::Source::CurseForge ? curseforge : modrinth;
    }

    void followInstance(Addons::Query& query, const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr) {
            return;
        }
        if (query.kind != Addons::Kind::Modpack) {
            query.gameVersion = entry->info.minecraft_version;
        }
        if (query.kind == Addons::Kind::Mod) {
            std::string loader = ProjectInstaller::loaderOf(instanceKey);
            query.loaders.clear();
            if (!loader.empty()) {
                query.loaders.push_back(loader);
            }
        }
        query.page = 0;
    }

    std::vector<const Addons::Version*> matching(const std::vector<Addons::Version>& versions,
                                                const std::string& game, const std::string& loader) {
        std::vector<const Addons::Version*> out;
        for (const auto& version : versions) {
            if ((game.empty() || std::find(version.gameVersions.begin(), version.gameVersions.end(), game) !=
                                         version.gameVersions.end()) &&
                (loader.empty() || std::find(version.loaders.begin(), version.loaders.end(), loader) !=
                                           version.loaders.end())) {
                out.push_back(&version);
            }
        }
        return out;
    }

    std::string sharedPrefix(const std::vector<Addons::Version>& versions) {
        std::string prefix;
        if (versions.size() >= 2) {
            prefix = versions.front().name;
            for (const auto& version : versions) {
                prefix.resize(sameStart(version.name, prefix));
                if (prefix.empty()) {
                    break;
                }
            }
        }
        size_t cut = prefix.size();
        while (cut > 0 && !separator(prefix[cut - 1])) {
            cut--;
        }
        prefix.resize(cut);
        return prefix;
    }

    std::string shortName(const Addons::Version& version, const std::string& prefix, const std::string& title) {
        std::string name = dropStart(dropStart(version.name, prefix), title);
        return !name.empty() ? name : version.number.empty() ? version.name : version.number;
    }

    bool supports(Addons::Source source, Addons::Kind kind) {
        return source != Addons::Source::Modrinth || kind != Addons::Kind::World;
    }

    std::vector<std::string> loaderTagsOf(const std::vector<Addons::Source>& sources, Addons::Kind kind) {
        std::vector<std::string> out;
        for (Addons::Source source : sources) {
            appendUnique(out, loaderTags(source, kind));
        }
        return out;
    }

    const std::vector<SortOption>& sorts() {
        using Addons::Sort;
        static const std::vector<SortOption> common = {
                {Sort::Relevance, "相关度"}, {Sort::Downloads, "下载量"}, {Sort::Updated, "最近更新"},
                {Sort::Newest, "最新发布"},
        };
        return common;
    }

    Merged merge(const std::vector<std::pair<Addons::Source, const Addons::Results*>>& lists) {
        Merged out;
        int counted = 0;
        int failed = 0;
        bool ready = false;
        std::string firstError;
        for (const auto& entry : lists) {
            const Addons::Results* results = entry.second;
            if (results == nullptr) {
                continue;
            }
            counted++;
            ready = ready || results->status == Addons::Status::Ready;
            out.total += results->total;
            out.pages = std::max(out.pages, (results->total + Addons::PAGE_SIZE - 1) / Addons::PAGE_SIZE);
            if (results->status == Addons::Status::Failed) {
                failed++;
                out.failed.push_back(entry.first);
                if (firstError.empty() && !results->error.empty()) {
                    firstError = results->error;
                }
            }
        }
        if (ready) {
            out.status = Addons::Status::Ready;
        } else if (counted > 0 && counted == failed) {
            out.status = Addons::Status::Failed;
            out.error = firstError;
        }

        // By rank, one hit of every source in turn, so each page holds them all.
        size_t ranks = 0;
        for (const auto& entry : lists) {
            if (entry.second != nullptr) {
                ranks = std::max(ranks, entry.second->hits.size());
            }
        }
        for (size_t rank = 0; rank < ranks; rank++) {
            for (const auto& entry : lists) {
                Addons::Source source = entry.first;
                const Addons::Results* results = entry.second;
                if (results == nullptr || rank >= results->hits.size()) {
                    continue;
                }
                const Addons::Hit& hit = results->hits[rank];
                MergedHit* same = nullptr;
                for (MergedHit& row : out.hits) {
                    if (std::any_of(row.found.begin(), row.found.end(),
                                    [&hit](const Found& found) { return sameProject(found.hit, hit); })) {
                        same = &row;
                        break;
                    }
                }
                if (same == nullptr) {
                    MergedHit row;
                    row.hit = hit;
                    row.primary = source;
                    row.found.push_back(Found{source, hit});
                    out.hits.push_back(std::move(row));
                    continue;
                }
                if (std::none_of(same->found.begin(), same->found.end(),
                                 [source](const Found& found) { return found.source == source; })) {
                    same->found.push_back(Found{source, hit});
                }
                std::vector<std::string> loaders = same->hit.loaders;
                appendUnique(loaders, hit.loaders);
                if (hit.downloads > same->hit.downloads) {
                    same->hit = hit;
                    same->primary = source;
                }
                // Loaders of either platform count for the row; its categories stay those of the platform it is shown from.
                same->hit.loaders = std::move(loaders);
            }
        }
        return out;
    }

    const std::vector<MergedCategory>& categories(const std::vector<Addons::Source>& sources, Addons::Kind kind) {
        using Addons::Source;
        static const std::vector<MergedCategory> none;
        // The lists are loaded once, so a complete answer is kept for good; while they arrive it is rebuilt instead.
        static std::map<std::string, std::pair<bool, std::vector<MergedCategory>>> cache;
        std::string key = std::to_string((int) kind);
        bool complete = true;
        for (Source source : sources) {
            key += source == Source::CurseForge ? "/cf" : "/mr";
            complete = complete && (!supports(source, kind) || !Addons::categories(source, kind).empty());
        }
        auto cached = cache.find(key);
        if (cached != cache.end() && cached->second.first) {
            return cached->second.second;
        }
        // Modrinth first, so its grouping and labels win, then the CurseForge rows it does not have.
        std::vector<MergedCategory> out;
        std::vector<std::string> names;
        for (Source source : {Source::Modrinth, Source::CurseForge}) {
            if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
                continue;
            }
            bool curse = source == Source::CurseForge;
            std::string prefix = curse ? "cf:" : "mr:";
            for (const Addons::Category& category : Addons::categories(source, kind)) {
                std::string name = normalised(category.name.empty() ? category.id : category.name);
                size_t at = names.size();
                if (!name.empty()) {
                    for (size_t i = 0; i < names.size(); i++) {
                        if (names[i] == name) {
                            at = i;
                            break;
                        }
                    }
                }
                if (at == names.size()) {
                    MergedCategory row;
                    row.label = curse ? category.name : Modrinth::label(category.id);
                    row.header = curse ? std::string() : category.header;
                    row.keys.push_back(prefix + category.id);
                    row.iconUrl = category.iconUrl;
                    out.push_back(std::move(row));
                    names.push_back(std::move(name));
                    continue;
                }
                MergedCategory& row = out[at];
                if (std::find(row.keys.begin(), row.keys.end(), prefix + category.id) == row.keys.end()) {
                    row.keys.push_back(prefix + category.id);
                }
                if (row.iconUrl.empty()) {
                    row.iconUrl = category.iconUrl;
                }
            }
        }
        if (out.empty()) {
            return none;
        }
        auto& kept = cache[key];
        kept.first = complete;
        kept.second = std::move(out);
        return kept.second;
    }
}

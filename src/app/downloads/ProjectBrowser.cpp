#include "app/downloads/ProjectBrowser.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Instances.h"

#include <algorithm>

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
}

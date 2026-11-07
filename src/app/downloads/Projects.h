#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace Projects {

    struct Link {
        std::string label;
        std::string url;
        int glyph = 0;
    };

    struct Image {
        std::string thumb;
        std::string full;
        std::string title;
        std::string description;
    };

    struct Member {
        std::string id;
        std::string name;
        std::string avatar;
        std::string role;
        std::string url;
        bool organization = false;
    };

    struct Detail {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        Addons::Source source = Addons::Source::Modrinth;
        Addons::Kind kind = Addons::Kind::Mod;
        std::string id;
        std::string slug;
        std::string title;
        std::string summary;
        std::string body;
        bool html = false;
        bool bodyLoaded = false;
        std::string iconUrl;
        std::string pageUrl;
        long long downloads = 0;
        long long followers = 0;
        std::string published;
        std::string updated;
        std::string license;
        std::string licenseUrl;
        std::vector<std::string> gameVersions;
        std::vector<std::string> loaders;
        std::vector<std::string> categories;
        std::string client;
        std::string server;
        std::vector<Link> links;
        std::vector<Image> gallery;
        std::vector<Member> members;
        bool membersLoaded = false;
    };

    struct Brief {
        std::string id;
        std::string slug;
        std::string title;
        std::string iconUrl;
        Addons::Kind kind = Addons::Kind::Mod;
    };

    struct Changelog {
        Addons::Status status = Addons::Status::Loading;
        std::string text;
        bool html = false;
    };

    const Detail& detail(Addons::Source source, const std::string& id);
    void refresh(Addons::Source source, const std::string& id);
    const Brief* brief(Addons::Source source, const std::string& id);
    void remember(Addons::Source source, const Brief& brief);
    const Changelog& changelog(const Addons::Version& version);
    std::string licenseName(const std::string& id, const std::string& name);

    void poll();
    void shutdown();
}

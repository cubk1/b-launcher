#pragma once

#include "app/downloads/Addons.h"

#include <core/base/error.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Mcmod {

    struct Ref {
        long long id = 0;
        std::string type;
        std::string zh;
        std::string en;
        std::string url;
        std::string roles;
        std::vector<std::string> keys;
    };

    struct Author {
        long long id = 0;
        std::string name;
        std::string avatar;
        std::string roles;
    };

    struct Lookup {
        Addons::Status status = Addons::Status::Loading;
        Ref ref;
    };

    struct Mod {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        Ref ref;
        std::string state;
        std::vector<std::string> tags;
        std::vector<Author> authors;
        std::string modrinth;
        std::string curseforge;
        long long views = 0;
        long long modpacks = 0;
    };

    struct Text {
        Addons::Status status = Addons::Status::Loading;
        std::string content;
        bool html = false;
    };

    struct Relation {
        std::string type;
        std::string scope;
        Ref mod;
    };

    struct Relations {
        Addons::Status status = Addons::Status::Loading;
        std::vector<Relation> items;
    };

    const Lookup& lookup(Addons::Source source, const std::string& slug);
    const Mod& mod(const Ref& ref);
    const Text& intro(const Ref& ref);
    const Relations& relations(const Ref& ref);
    std::string displayName(const Ref& ref);
    std::string slugOn(const Ref& ref, const std::string& site);
    Ref refOf(const nlohmann::json& node);
    b_launcher_base::result<nlohmann::json> call(const std::string& path, b_launcher_base::cancellation_token token);

    void poll();
    void shutdown();
}

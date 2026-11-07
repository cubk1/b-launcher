#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace Authors {

    enum class Kind { ModrinthUser, ModrinthOrganization, CurseForge, Mcmod };

    struct Project {
        Addons::Hit hit;
        Addons::Kind kind = Addons::Kind::Mod;
        std::string updated;
        long long mcmodId = 0;
        std::string mcmodType;
        std::string roles;
        std::vector<std::string> keys;
    };

    struct Profile {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        std::string name;
        std::string avatar;
        std::string bio;
        std::string joined;
        std::string url;
        std::vector<std::string> aliases;
        std::vector<Project> projects;
        long long downloads = 0;
    };

    const Profile& profile(Kind kind, const std::string& id, const std::string& name = std::string(),
                           const std::string& avatar = std::string());
    void refresh(Kind kind, const std::string& id);

    void poll();
    void shutdown();
}

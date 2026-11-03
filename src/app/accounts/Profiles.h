#pragma once

#include "app/system/Pixels.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Profiles {

    enum class Status { Loading, Ready, Failed };
    enum class OptiFine { Checking, Owned, None, Failed };

    struct Cape {
        std::string id;
        std::string alias;
        std::string url;
        bool active = false;
    };

    struct Profile {
        Status status = Status::Loading;
        std::string error;
        std::string name;
        std::string skinUrl;
        bool slim = false;
        std::vector<Cape> capes;
        bool renameAllowed = true;
        std::string renameAfter;
        std::string working;
        OptiFine optifine = OptiFine::Checking;
        std::string optifineUrl;
    };

    enum class Check { Idle, Checking, Available, Taken, Invalid, Failed };

    struct NameCheck {
        std::string name;
        Check state = Check::Idle;
        std::string error;
    };

    bool validName(const std::string& name);

    const Profile& of(const std::string& key);
    void reload(const std::string& key);
    void uploadSkin(const std::string& key, const std::filesystem::path& file);
    void resetSkin(const std::string& key);
    void setCape(const std::string& key, const std::string& capeId);
    void checkOptiFine(const std::string& key);
    void manageOptiFine(const std::string& key);

    const NameCheck& nameCheck(const std::string& key);
    void checkName(const std::string& key, const std::string& name);
    void forgetName(const std::string& key);
    void rename(const std::string& key, const std::string& name);

    std::shared_ptr<const Pixels> capeFront(const std::string& url);
    bool capeFrontFailed(const std::string& url);

    void poll();
    void shutdown();
}

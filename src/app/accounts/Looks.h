#pragma once

#include "app/launch/Patches.h"

#include <filesystem>
#include <string>

namespace Looks {

    enum class Model { Auto, Classic, Slim };

    struct Look {
        std::string skin;
        std::string cape;
        std::filesystem::path skinFile;
        std::filesystem::path capeFile;
        Model model = Model::Auto;
    };

    const Look& of(const std::string& key);
    std::string setSkin(const std::string& key, const std::filesystem::path& file);
    std::string setCape(const std::string& key, const std::filesystem::path& file);
    void clearSkin(const std::string& key);
    void clearCape(const std::string& key);
    void move(const std::string& from, const std::string& to);
    void forget(const std::string& key);
    void apply(const std::string& key, Patches::Plan& plan);
}

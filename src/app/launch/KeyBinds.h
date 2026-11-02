#pragma once

#include "app/system/Pixels.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace KeyBinds {

    constexpr const char* UNBOUND = "key.keyboard.unknown";
    constexpr const char* VANILLA_GROUP = "minecraft";
    constexpr const char* DEBUG_GROUP = "debug";
    constexpr const char* OTHER_GROUP = "other";

    struct Binding {
        std::string id;
        std::string key;
        std::string modifier;
        bool suffixed = false;
        std::string name;
        std::string category;
        std::string group;
    };

    struct Group {
        std::string id;
        std::string label;
        std::shared_ptr<const Pixels> icon;
        int count = 0;
    };

    struct Table {
        bool exists = false;
        bool legacy = false;
        bool naming = false;
        std::vector<Binding> bindings;
        std::vector<Group> groups;
    };

    struct Target {
        std::filesystem::path file;
        bool legacy = false;
    };

    const Table& table(const std::filesystem::path& file);
    std::string keyLabel(const std::string& key);
    std::string modifierLabel(const std::string& modifier);
    bool bound(const std::string& key);
    bool conflicts(const Binding& first, const Binding& second);
    bool legacyVersion(const std::string& minecraft);

    std::string assign(const std::filesystem::path& file, const std::string& id, const std::string& key,
                       bool legacyHint = false);
    std::string unbind(const std::filesystem::path& file, const std::string& id, bool legacyHint = false);
    std::string copy(const std::filesystem::path& from, const std::vector<Target>& targets);

    void poll();
    void shutdown();
}

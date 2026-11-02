#pragma once

#include "app/system/Pixels.h"

#include <core/modpack/mods.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Mods {

    namespace blb = b_launcher_base;

    struct Mod {
        blb::modpack::mod_info info;
        std::shared_ptr<const Pixels> icon;
        std::vector<std::string> missing;
    };

    struct List {
        bool loading = true;
        std::vector<Mod> mods;
    };

    const List& of(const std::string& instanceKey);
    bool matches(const Mod& mod, std::string_view query);
    // Dropped mods exclude ZIP packs and old backups, which the directory scanner may accept.
    bool acceptsDrop(std::string_view filename);
    void scan(const std::string& instanceKey);
    void setEnabled(const std::string& instanceKey, const std::filesystem::path& file, bool enabled);
    void remove(const std::string& instanceKey, const std::filesystem::path& file);

    std::filesystem::path folder(const std::string& instanceKey, const char* kind);
    void add(const std::string& instanceKey, const std::vector<std::filesystem::path>& files, const char* kind,
             const std::string& what);

    void poll();
    void shutdown();
}

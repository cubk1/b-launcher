#pragma once

#include <core/modpack/mods.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Mods {

    namespace blb = b_launcher_base;

    struct Mod {
        blb::modpack::mod_info info;
        uint32_t icon = 0;
        std::vector<std::string> missing;
    };

    struct List {
        bool loading = true;
        std::vector<Mod> mods;
    };

    const List& of(const std::string& instanceKey);
    void rescan(const std::string& instanceKey);
    void setEnabled(const std::string& instanceKey, const std::filesystem::path& file, bool enabled);
    void remove(const std::string& instanceKey, const std::filesystem::path& file);

    std::filesystem::path folder(const std::string& instanceKey, const char* kind);
    void add(const std::string& instanceKey, const std::vector<std::filesystem::path>& files, const char* kind,
             const std::string& what);

    void poll();
    void shutdown();
}

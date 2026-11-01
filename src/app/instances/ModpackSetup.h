#pragma once

#include "app/instances/Installer.h"

#include <optional>

namespace Addons { struct Version; }
namespace b_launcher_base::modpack { struct pack_manifest; }

// The install request and its display metadata, independent of either interface.
struct ModpackSetup {
    std::string title;
    std::string version;
    std::string minecraft;
    std::vector<std::string> loaders;
    Installer::Modpack pack;

    static ModpackSetup fromFile(const std::filesystem::path& file,
                                 const b_launcher_base::modpack::pack_manifest& manifest);
    static std::optional<ModpackSetup> fromProject(const Addons::Version& version, const std::string& title);
};

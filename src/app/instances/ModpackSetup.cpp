#include "app/instances/ModpackSetup.h"
#include "app/downloads/Addons.h"
#include "app/downloads/Modrinth.h"

#include <core/base/strings.hpp>
#include <core/modpack/modpack.hpp>

namespace blb = b_launcher_base;

ModpackSetup ModpackSetup::fromFile(const std::filesystem::path& file, const blb::modpack::pack_manifest& manifest) {
    ModpackSetup setup;
    setup.title = manifest.name.empty() ? blb::path_to_utf8(file.stem()) : manifest.name;
    setup.version = manifest.version.empty() ? std::string("未知") : manifest.version;
    setup.minecraft = manifest.minecraft;
    for (const auto& loader : manifest.loaders) {
        setup.loaders.push_back(Installer::loaderName(loader.type) +
                                (loader.version.empty() ? std::string() : " " + loader.version));
        setup.pack.loaders.push_back(loader.type);
    }
    setup.pack.minecraft = manifest.minecraft;
    setup.pack.file = file;
    return setup;
}

std::optional<ModpackSetup> ModpackSetup::fromProject(const Addons::Version& version, const std::string& title) {
    const Addons::File* file = version.file();
    if (file == nullptr) {
        return std::nullopt;
    }
    ModpackSetup setup;
    setup.title = title;
    setup.version = version.name.empty() ? version.number : version.name;
    setup.minecraft = version.gameVersions.empty() ? std::string() : version.gameVersions.front();
    setup.pack.minecraft = setup.minecraft;
    setup.pack.urls = Addons::fileUrls(file->url);
    setup.pack.sha1 = file->sha1;
    setup.pack.size = file->size;
    using blb::install::loader_type;
    const std::pair<const char*, loader_type> types[] = {{"fabric", loader_type::fabric},
                                                      {"forge", loader_type::forge},
                                                      {"neoforge", loader_type::neoforge},
                                                      {"quilt", loader_type::quilt}};
    for (const auto& tag : version.loaders) {
        setup.loaders.push_back(Modrinth::label(tag));
        for (const auto& [name, type] : types) {
            if (tag == name) {
                setup.pack.loaders.push_back(type);
            }
        }
    }
    return setup;
}

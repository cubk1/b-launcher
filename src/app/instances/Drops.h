#pragma once

#include <core/modpack/modpack.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace Drops {

    namespace blb = b_launcher_base;

    // What an interface does with the files dropped on its window.
    struct Handlers {
        // The instance that mods, resource packs and shader packs go into.
        std::function<std::string()> target;
        // A modpack to set up as a new instance, with what its file says of it.
        std::function<void(const std::filesystem::path& file, const blb::modpack::pack_manifest& manifest)> openModpack;
        // Whether files are taken now; always when unset.
        std::function<bool()> accepting;
    };

    // Takes the files dropped on window: a modpack is set up as a new instance, mods, resource packs and shader packs
    // go into the target instance.
    void attach(void* window, Handlers handlers);
    void detach();
    // Whether files are dragged over the window, and what dropping them would do; it starts with 松开 when they can
    // be taken.
    bool hovering();
    std::string promise();
    // A modpack file picked by hand, set up as if it had been dropped.
    void importPack(const std::filesystem::path& file);
    // Acts on the dropped files once they are read; one of the services.
    void poll();
    void shutdown();
}

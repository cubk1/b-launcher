#pragma once

#include <core/modpack/modpack.hpp>

#include <string>

// Modpacks written from an instance, one at a time in the background.
namespace Exports {

    using Options = b_launcher_base::modpack::export_options;

    // The options of an instance: those saved for it, or else its name and the current account as the author.
    Options& options(const std::string& instanceKey);
    // Keeps the options of an instance for the next run.
    void save(const std::string& instanceKey);
    // Asks where to write the instance's pack and starts writing it, unless one is written already.
    void start(const std::string& instanceKey);
    void cancel();
    bool running();
    // The instance whose pack is written, and how far that has come.
    const std::string& runningKey();
    std::string status();
    // Tells how the writing ended and shows the pack in its folder.
    void poll();
    void shutdown();
}

#pragma once

#include "app/system/Deletion.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace DeleteDialogs {

    // notes explains why the recycle bin refused the folder and is shown in the dialog.
    void openInstance(const std::string& name, const std::vector<std::string>& notes,
                      std::function<void()> confirmed);

    void openInstanceRecycleFailed(const std::string& name, const std::string& reason, bool tooLarge,
                                   const std::filesystem::path& folder, std::function<void()> confirmed);

    // Call it only when the permanent-delete confirmation setting asks for one.
    void openResource(const std::string& name, std::function<void()> confirmed);

    void openRecycleFailed(const std::string& name, const std::string& reason, bool tooLarge,
                           const std::filesystem::path& file, std::function<void()> confirmed);

    // The one entry point for deleting a resource file: a permanent delete is confirmed first, and a recycle
    // bin failure falls back to asking the user. erase receives true when this delete must be permanent.
    using Erase = std::function<Deletion::Outcome(bool permanently)>;
    void removeResourceFile(const std::string& name, const std::filesystem::path& file, Erase erase);
}

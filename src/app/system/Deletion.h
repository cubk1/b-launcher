#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Deletion {

    enum class Mode { Recycle, Permanent };

    Mode mode();
    void setMode(Mode value);
    // Also applies to the fallback deletion used when the recycle bin refuses a file.
    bool confirmPermanent();
    void setConfirmPermanent(bool value);

    struct Outcome {
        bool ok = false;
        bool recycled = false;
        // The recycle bin refused it; the caller asks the user what to do next.
        bool needsPermanent = false;
        bool tooLarge = false;
        std::string reason;
    };

    // Deletes a resource file as configured; a recycle bin failure is reported, never turned into a permanent
    // delete on its own.
    Outcome removeResource(const std::filesystem::path& path);
    Outcome removeResources(const std::vector<std::filesystem::path>& paths);

    Outcome recycle(const std::filesystem::path& path);

    Outcome removePermanent(const std::filesystem::path& path);

    struct Space {
        bool known = false;
        unsigned long long usedBytes = 0;
        unsigned long long limitBytes = 0;
    };
    Space space(const std::filesystem::path& path);
}

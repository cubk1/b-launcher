#include "app/system/Deletion.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"

#include <core/base/json.hpp>

namespace Deletion {

    namespace blb = b_launcher_base;

    namespace {
        const char* const MODE = "deleteMode";
        const char* const CONFIRM = "permanentDeleteConfirm";

        Outcome permanent(const std::filesystem::path& path) {
            Outcome outcome;
            std::error_code error;
            std::filesystem::remove_all(path, error);
            if (error) {
                outcome.reason = "永久删除失败：" + error.message();
                return outcome;
            }
            outcome.ok = true;
            return outcome;
        }

        Outcome toRecycle(const std::filesystem::path& path) {
            Outcome outcome;
            Platform::RecycleResult result = Platform::recycleDetailed(path);
            if (result.ok) {
                outcome.ok = true;
                outcome.recycled = true;
                return outcome;
            }
            outcome.needsPermanent = result.refused;
            outcome.tooLarge = result.tooLarge;
            outcome.reason = result.reason;
            return outcome;
        }
    }

    Mode mode() {
        return blb::json_text(Config::data(), MODE) == "permanent" ? Mode::Permanent : Mode::Recycle;
    }

    void setMode(Mode value) {
        Config::data()[MODE] = value == Mode::Permanent ? "permanent" : "recycle";
        Config::save();
    }

    bool confirmPermanent() {
        return blb::json_bool(Config::data(), CONFIRM, true);
    }

    void setConfirmPermanent(bool value) {
        Config::data()[CONFIRM] = value;
        Config::save();
    }

    Outcome removeResource(const std::filesystem::path& path) {
        if (mode() == Mode::Permanent) {
            return permanent(path);
        }
        return toRecycle(path);
    }

    Outcome removeResources(const std::vector<std::filesystem::path>& paths) {
        Outcome outcome;
        if (mode() == Mode::Permanent) {
            for (const std::filesystem::path& path : paths) {
                outcome = permanent(path);
                if (!outcome.ok) {
                    return outcome;
                }
            }
            outcome.ok = true;
            return outcome;
        }
#if defined(_WIN32)
        Platform::RecycleResult result = Platform::recycleAll(paths);
        outcome.ok = result.ok;
        outcome.recycled = result.ok;
        outcome.needsPermanent = result.refused;
        outcome.tooLarge = result.tooLarge;
        outcome.reason = result.reason;
        return outcome;
#else
        for (const std::filesystem::path& path : paths) {
            std::error_code error;
            if (!std::filesystem::exists(std::filesystem::symlink_status(path, error))) {
                continue;
            }
            outcome = toRecycle(path);
            if (!outcome.ok) {
                return outcome;
            }
        }
        outcome.ok = true;
        outcome.recycled = true;
        return outcome;
#endif
    }

    Outcome recycle(const std::filesystem::path& path) {
        return toRecycle(path);
    }

    Outcome removePermanent(const std::filesystem::path& path) {
        return permanent(path);
    }

    Space space(const std::filesystem::path& path) {
        Platform::RecycleSpace result = Platform::recycleSpace(path);
        Space out;
        out.known = result.known;
        out.usedBytes = result.usedBytes;
        out.limitBytes = result.limitBytes;
        return out;
    }
}

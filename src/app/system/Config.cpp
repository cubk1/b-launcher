#include "app/system/Config.h"
#include "app/system/Platform.h"

#include <optional>
#include <string>
#include <utility>

namespace Config {

    namespace {
        nlohmann::json doc;
        bool loaded = false;
        bool empty = false;
        // After a reset the old session must not write its state back before restarting.
        bool frozen = false;

        const char* const VALUE = "Settings";
    }

    nlohmann::json& data() {
        if (!loaded) {
            loaded = true;
            std::optional<std::string> stored = Platform::loadValue(VALUE);
            empty = !stored.has_value();
            if (stored) {
                doc = nlohmann::json::parse(*stored, nullptr, false);
            }
            if (!doc.is_object()) {
                doc = nlohmann::json::object();
            }
        }
        return doc;
    }

    bool fresh() {
        data();
        return empty;
    }

    bool save() {
        if (frozen) {
            return false;
        }
        return Platform::storeValue(VALUE, data().dump(), true);
    }

    bool reset() {
        nlohmann::json defaults = nlohmann::json::object();
        const auto& previous = data();
        if (auto folders = previous.find("gameFolders"); folders != previous.end()) {
            defaults["gameFolders"] = *folders;
        }
        if (!Platform::storeValue(VALUE, defaults.dump(), true)) {
            return false;
        }
        doc = std::move(defaults);
        loaded = true;
        frozen = true;
        return true;
    }
}

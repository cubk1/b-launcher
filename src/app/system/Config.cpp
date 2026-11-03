#include "app/system/Config.h"
#include "app/system/Platform.h"

#include <optional>
#include <string>
#include <utility>

namespace Config {

    namespace {
        nlohmann::json doc;
        bool loaded = false;
        // After a reset the old session must not write its state back before restarting.
        bool frozen = false;

        const char* const VALUE = "Settings";
    }

    nlohmann::json& data() {
        if (!loaded) {
            loaded = true;
            if (std::optional<std::string> stored = Platform::loadValue(VALUE)) {
                doc = nlohmann::json::parse(*stored, nullptr, false);
            }
            if (!doc.is_object()) {
                doc = nlohmann::json::object();
            }
        }
        return doc;
    }

    bool save() {
        if (frozen) {
            return false;
        }
        return Platform::storeValue(VALUE, data().dump(), true);
    }

    bool reset() {
        nlohmann::json defaults = nlohmann::json::object();
        if (!Platform::storeValue(VALUE, defaults.dump(), true)) {
            return false;
        }
        doc = std::move(defaults);
        loaded = true;
        frozen = true;
        return true;
    }
}

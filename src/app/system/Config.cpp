#include "app/system/Config.h"
#include "app/system/Platform.h"

#include <optional>
#include <string>

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

    void reset() {
        doc = nlohmann::json::object();
        loaded = true;
        frozen = true;
        Platform::storeValue(VALUE, doc.dump(), true);
    }
}

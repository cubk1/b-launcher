#include "app/system/Config.h"
#include "app/system/Platform.h"

#include <optional>
#include <string>

namespace Config {

    namespace {
        nlohmann::json doc;
        bool loaded = false;

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
        return Platform::storeValue(VALUE, data().dump(), true);
    }
}

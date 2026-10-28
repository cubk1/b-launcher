#include "Config.h"
#include "Platform.h"

#include <fstream>
#include <sstream>

namespace Config {

    namespace {
        nlohmann::json doc;
        bool loaded = false;

        const char* const VALUE = "Settings";

        std::filesystem::path oldFile() {
            return Platform::exeDirectory() / L"b-launcher.json";
        }
    }

    nlohmann::json& data() {
        if (!loaded) {
            loaded = true;
            std::optional<std::string> stored = Platform::loadValue(VALUE);
            bool moved = false;
            if (!stored) {
                std::ifstream in(oldFile(), std::ios::binary);
                if (in) {
                    std::stringstream text;
                    text << in.rdbuf();
                    stored = text.str();
                    moved = true;
                }
            }
            if (stored) {
                doc = nlohmann::json::parse(*stored, nullptr, false);
            }
            if (!doc.is_object()) {
                doc = nlohmann::json::object();
            }
            if (moved && save()) {
                std::error_code error;
                std::filesystem::remove(oldFile(), error);
            }
        }
        return doc;
    }

    bool save() {
        return Platform::storeValue(VALUE, data().dump(), true);
    }
}

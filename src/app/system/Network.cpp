#include "app/system/Network.h"
#include "app/system/Config.h"

#include <core/network/http_client.hpp>

namespace Network {

    namespace {
        const char* const KEY = "systemProxy";
    }

    bool systemProxy() {
        const nlohmann::json& doc = Config::data();
        auto found = doc.find(KEY);
        return found != doc.end() && found->is_boolean() && found->get<bool>();
    }

    void setSystemProxy(bool on) {
        Config::data()[KEY] = on;
        Config::save();
        apply();
    }

    void apply() {
        b_launcher_base::net::network_settings settings = b_launcher_base::net::get_network_settings();
        settings.use_system_proxy = systemProxy();
        b_launcher_base::net::set_network_settings(settings);
    }
}

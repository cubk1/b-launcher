#include "app/system/Network.h"
#include "app/system/Config.h"

#include <core/base/json.hpp>
#include <core/network/http_client.hpp>

namespace Network {

    namespace {
        const char* const KEY = "systemProxy";
        const char* const OFFLINE_KEY = "offlineLaunch";
    }

    bool systemProxy() {
        return b_launcher_base::json_bool(Config::data(), KEY);
    }

    void setSystemProxy(bool on) {
        Config::data()[KEY] = on;
        Config::save();
        apply();
    }

    bool offlineLaunch() {
        return b_launcher_base::json_bool(Config::data(), OFFLINE_KEY);
    }

    void setOfflineLaunch(bool on) {
        Config::data()[OFFLINE_KEY] = on;
        Config::save();
    }

    void apply() {
        b_launcher_base::net::network_settings settings = b_launcher_base::net::get_network_settings();
        settings.use_system_proxy = systemProxy();
        b_launcher_base::net::set_network_settings(settings);
    }
}

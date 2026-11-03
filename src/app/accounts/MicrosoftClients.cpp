#include "app/accounts/MicrosoftClients.h"

namespace MicrosoftClients {

    namespace {
        struct Entry {
            App app;
            const char* name;
            const char* id;
            const char* scope;
            const char* rpsPrefix;
            bool live;
        };

        const Entry ENTRIES[] = {
                {App::Launcher, "b-launcher", "2289e44b-73ce-4577-bbd0-bb7f3c1a27cb", "XboxLive.signin offline_access",
                 "d=", true},
                {App::Official, "Minecraft 启动器", "00000000402b5328", "service::user.auth.xboxlive.com::MBI_SSL", "",
                 true},
                {App::Hmcl, "HMCL", "6a3728d6-27a3-4180-99bb-479895b8f88e", "XboxLive.signin offline_access",
                 "d=", true},
                {App::Pcl, "PCL", "fe72edc2-3a6f-4280-90e8-e2beb64ce7e1", "XboxLive.signin offline_access", "d=", true},
                {App::Prism, "Prism Launcher", PRISM_ID, "XboxLive.SignIn XboxLive.offline_access", "d=", false},
        };

        const Entry& entry(App app) {
            for (const Entry& e : ENTRIES) {
                if (e.app == app) {
                    return e;
                }
            }
            return ENTRIES[0];
        }
    }

    b_launcher_base::auth::microsoft_client client(App app) {
        const Entry& e = entry(app);
        b_launcher_base::auth::microsoft_client out;
        out.client_id = e.id;
        out.scope = e.scope;
        out.live = e.live;
        out.rps_prefix = e.rpsPrefix;
        return out;
    }

    App of(const std::string& clientId) {
        for (const Entry& e : ENTRIES) {
            if (clientId == e.id) {
                return e.app;
            }
        }
        return App::Launcher;
    }

    const char* name(App app) {
        return entry(app).name;
    }
}

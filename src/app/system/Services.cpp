#include "app/system/Services.h"
#include "app/accounts/Accounts.h"
#include "app/downloads/Addons.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Exports.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/instances/Mods.h"
#include "app/launch/Game.h"
#include "app/launch/JavaDownloads.h"
#include "app/launch/ShortcutLaunch.h"
#include "app/system/Catalog.h"
#include "app/system/Updates.h"

namespace Services {

    void start() {
        Instances::init();
        Accounts::init();
        Catalog::preload();
        Addons::preload();
        Updates::check(true, false);
    }

    void poll() {
        Instances::poll();
        Accounts::poll();
        Game::poll();
        Catalog::poll();
        Addons::poll();
        ProjectInstaller::poll();
        Installer::poll();
        Mods::poll();
        Exports::poll();
        Updates::poll();
        JavaDownloads::poll();
        ShortcutLaunch::poll();
    }

    void stop() {
        Updates::shutdown();
        JavaDownloads::shutdown();
        Exports::shutdown();
        Mods::shutdown();
        Installer::shutdown();
        ProjectInstaller::shutdown();
        Addons::shutdown();
        Catalog::shutdown();
        Game::shutdown();
        Accounts::shutdown();
        Instances::shutdown();
    }

    bool busy() {
        return Game::state() != Game::State::Idle || Installer::running() || JavaDownloads::busy() ||
                ProjectInstaller::busy() || Exports::running() || !Instances::activity().empty();
    }
}

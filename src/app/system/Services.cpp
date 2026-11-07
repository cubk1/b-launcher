#include "app/system/Services.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Avatars.h"
#include "app/accounts/LittleSkin.h"
#include "app/accounts/Profiles.h"
#include "app/downloads/Addons.h"
#include "app/downloads/DownloadSources.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/instances/Drops.h"
#include "app/instances/Exports.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/instances/ModManager.h"
#include "app/instances/Mods.h"
#include "app/instances/Screenshots.h"
#include "app/launch/Game.h"
#include "app/launch/JavaDownloads.h"
#include "app/launch/KeyBinds.h"
#include "app/launch/ServerList.h"
#include "app/launch/ShortcutLaunch.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/system/Catalog.h"
#include "app/system/CustomDownload.h"
#include "app/system/DiskScan.h"
#include "app/system/Network.h"
#include "app/system/ServerStatus.h"
#include "app/system/Updates.h"

namespace Services {

    void start() {
        Network::apply();
        DownloadSources::apply();
        Instances::init();
        Accounts::init();
        Catalog::preload();
        Addons::preload();
        Updates::check(true, false);
    }

    void poll() {
        Instances::poll();
        Accounts::poll();
        Profiles::poll();
        Avatars::poll();
        LittleSkin::poll();
        Game::poll();
        Catalog::poll();
        Addons::poll();
        ProjectInstaller::poll();
        Installer::poll();
        Mods::poll();
        ModManager::poll();
        KeyBinds::poll();
        ServerList::poll();
        Screenshots::poll();
        Exports::poll();
        Drops::poll();
        Updates::poll();
        JavaDownloads::poll();
        ShortcutLaunch::poll();
        ServerStatus::poll();
        Multiplayer::poll();
        CustomDownload::poll();
        DiskScan::poll();
    }

    void stop() {
        Multiplayer::shutdown();
        CustomDownload::shutdown();
        DiskScan::shutdown();
        ServerStatus::shutdown();
        Updates::shutdown();
        Drops::shutdown();
        JavaDownloads::shutdown();
        Exports::shutdown();
        Screenshots::shutdown();
        KeyBinds::shutdown();
        ServerList::shutdown();
        ModManager::shutdown();
        Mods::shutdown();
        Installer::shutdown();
        ProjectInstaller::shutdown();
        Addons::shutdown();
        Catalog::shutdown();
        Game::shutdown();
        LittleSkin::shutdown();
        Avatars::shutdown();
        Profiles::shutdown();
        Accounts::shutdown();
        Instances::shutdown();
    }

    bool busy() {
        return Game::active() || Multiplayer::active() || Installer::running() || JavaDownloads::busy() ||
                ProjectInstaller::busy() || Exports::running() || CustomDownload::busy() || ModManager::busy() ||
                !Instances::activity().empty();
    }
}

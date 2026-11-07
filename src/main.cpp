#include "app/launch/Game.h"
#include "app/system/DiskScan.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"
#include "app/system/UiStyles.h"
#if defined(_WIN32)
#include "ui/exui/windows/ForgottenMelody.h"
#endif
#include "ui/imgui/screens/shell/ImGuiApp.h"

#if !defined(_WIN32)
#include <csignal>
#endif

#include <optional>
#include <string>
#include <string_view>

int main() {
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif
    if (std::optional<DiskScan::Helper> helper = DiskScan::helperArgument()) {
        return DiskScan::runHelper(*helper);
    }
    Platform::waitForPrevious();
    std::string pendingLaunch = Platform::launchArgument();
    bool headless = !pendingLaunch.empty() && Platform::headlessArgument();
    if (!Config::data().value("allowMultipleInstances", true) && Platform::forwardLaunch(pendingLaunch, headless)) {
        return 0;
    }
    if (!pendingLaunch.empty()) {
        Platform::claimTaskbar();
    }
    Game::setQuiet(headless);
    UiStyles::start(Platform::uiArgument());
#if defined(_WIN32)
    if (std::string_view(UiStyles::running().key) == "forgotten-melody") {
        return ForgottenMelody::run(pendingLaunch, headless);
    }
#endif
    return ImGuiApp::run(pendingLaunch, headless);
}

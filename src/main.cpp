#include "app/launch/Game.h"
#include "app/system/DiskScan.h"
#include "app/system/Platform.h"
#include "app/system/UiStyles.h"
#if defined(_WIN32)
#include "ui/exui/windows/ForgottenMelody.h"
#endif
#include "ui/imgui/screens/shell/ImGuiApp.h"

#include <optional>
#include <string>
#include <string_view>

int main() {
    if (std::optional<DiskScan::Helper> helper = DiskScan::helperArgument()) {
        return DiskScan::runHelper(*helper);
    }
    Platform::waitForPrevious();
    std::string pendingLaunch = Platform::launchArgument();
    bool headless = !pendingLaunch.empty() && Platform::headlessArgument();
    if (!pendingLaunch.empty() && Platform::forwardLaunch(pendingLaunch, headless)) {
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

#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "app/system/UiStyles.h"
#include "ui/exui/windows/ForgottenMelody.h"
#include "ui/imgui/screens/shell/ImGuiApp.h"

#include <string>
#include <string_view>

int main() {
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
    if (std::string_view(UiStyles::running().key) == "forgotten-melody") {
        return ForgottenMelody::run(pendingLaunch, headless);
    }
    return ImGuiApp::run(pendingLaunch, headless);
}

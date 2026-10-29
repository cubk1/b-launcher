#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/ImGuiApp.h"

#include <string>

int main() {
    std::string pendingLaunch = Platform::launchArgument();
    bool headless = !pendingLaunch.empty() && Platform::headlessArgument();
    if (!pendingLaunch.empty() && Platform::forwardLaunch(pendingLaunch, headless)) {
        return 0;
    }
    if (!pendingLaunch.empty()) {
        Platform::claimTaskbar();
    }
    Game::setQuiet(headless);
    return ImGuiApp::run(pendingLaunch, headless);
}

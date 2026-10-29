#include "app/launch/ShortcutLaunch.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/system/Interface.h"
#include "app/system/Platform.h"

#include <utility>

namespace ShortcutLaunch {

    namespace {
        std::string pending;
        bool pendingQuiet = false;
        std::optional<Outcome> done;
    }

    void request(const std::string& key, bool quiet) {
        pending = key;
        pendingQuiet = quiet;
    }

    bool receive(unsigned long long kind, const std::wstring& text) {
        if ((kind != Platform::LAUNCH_MESSAGE && kind != Platform::LAUNCH_QUIET_MESSAGE) || text.empty()) {
            return false;
        }
        bool quiet = kind == Platform::LAUNCH_QUIET_MESSAGE;
        request(Platform::narrow(text), quiet);
        if (!quiet) {
            Platform::showWindow();
        }
        return true;
    }

    void poll() {
        if (pending.empty() || Instances::loading()) {
            return;
        }
        std::string key = std::exchange(pending, std::string());
        std::string name = key.substr(key.find('/') + 1);
        bool launched = false;
        if (Instances::find(key) == nullptr) {
            Instances::notify("找不到实例 " + name + "，快捷方式可能已失效", true);
        } else if (Game::state() != Game::State::Idle) {
            Instances::notify("已有游戏正在启动或运行，没有启动 " + name, true);
        } else {
            Instances::select(key);
            Interface::showInstance(key);
            Game::launch(key);
            launched = Game::state() != Game::State::Idle;
        }
        done = Outcome{launched, pendingQuiet};
    }

    std::optional<Outcome> take() {
        return std::exchange(done, std::nullopt);
    }
}

#include "app/system/Interface.h"

#include <atomic>

namespace Interface {

    namespace {
        Handlers handlers;
        std::atomic<bool> summoned = false;
    }

    void connect(Handlers next) {
        handlers = std::move(next);
    }

    void showLog() {
        if (handlers.showLog) {
            handlers.showLog();
        }
    }

    void needAccount(const std::string& login, const std::string& authServer) {
        if (handlers.needAccount) {
            handlers.needAccount(login, authServer);
        }
    }

    void installed(const std::string& key, bool loadersChanged) {
        if (handlers.installed) {
            handlers.installed(key, loadersChanged);
        }
    }

    void showInstance(const std::string& key) {
        if (handlers.showInstance) {
            handlers.showInstance(key);
        }
    }

    bool crashed(std::shared_ptr<const Crashes::Report> report) {
        if (!handlers.crashed) {
            return false;
        }
        handlers.crashed(std::move(report));
        return true;
    }

    bool loginFailed(const LoginCheck::Problem& problem) {
        if (!handlers.loginFailed) {
            return false;
        }
        handlers.loginFailed(problem);
        return true;
    }

    void summon() {
        summoned = true;
    }

    void poll() {
        if (summoned.exchange(false) && handlers.summon) {
            handlers.summon();
        }
    }
}

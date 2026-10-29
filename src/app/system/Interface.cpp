#include "app/system/Interface.h"

namespace Interface {

    namespace {
        Handlers handlers;
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
}

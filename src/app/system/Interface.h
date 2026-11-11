#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Crashes {
    struct Report;
}

namespace LoginCheck {
    struct Problem;
}

namespace Interface {

    struct Handlers {
        std::function<void()> showLog;
        std::function<void(const std::string& login, const std::string& authServer)> needAccount;
        std::function<void(const std::string& key, bool loadersChanged)> installed;
        std::function<void(const std::string& key)> showInstance;
        std::function<void(std::shared_ptr<const Crashes::Report> report)> crashed;
        std::function<void(const LoginCheck::Problem& problem)> loginFailed;
        std::function<void()> summon;
    };

    void connect(Handlers handlers);
    void showLog();
    void needAccount(const std::string& login, const std::string& authServer);
    void installed(const std::string& key, bool loadersChanged);
    void showInstance(const std::string& key);
    bool crashed(std::shared_ptr<const Crashes::Report> report);
    // False when no interface can show the offline dialog, so the caller reports it itself.
    bool loginFailed(const LoginCheck::Problem& problem);
    void summon();
    void poll();
}

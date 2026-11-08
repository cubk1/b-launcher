#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Crashes {
    struct Report;
}

namespace Interface {

    struct Handlers {
        std::function<void()> showLog;
        std::function<void(const std::string& login, const std::string& authServer)> needAccount;
        std::function<void(const std::string& key, bool loadersChanged)> installed;
        std::function<void(const std::string& key)> showInstance;
        std::function<void(std::shared_ptr<const Crashes::Report> report)> crashed;
    };

    void connect(Handlers handlers);
    void showLog();
    void needAccount(const std::string& login, const std::string& authServer);
    void installed(const std::string& key, bool loadersChanged);
    void showInstance(const std::string& key);
    bool crashed(std::shared_ptr<const Crashes::Report> report);
}

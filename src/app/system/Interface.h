#pragma once

#include <functional>
#include <string>

namespace Interface {

    struct Handlers {
        std::function<void()> showLog;
        std::function<void(const std::string& login, const std::string& authServer)> needAccount;
        std::function<void(const std::string& key, bool loadersChanged)> installed;
        std::function<void(const std::string& key)> showInstance;
    };

    void connect(Handlers handlers);
    void showLog();
    void needAccount(const std::string& login, const std::string& authServer);
    void installed(const std::string& key, bool loadersChanged);
    void showInstance(const std::string& key);
}

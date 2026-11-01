#pragma once

#include <core/base/cancellation.hpp>

#include <functional>
#include <string>

namespace ZeroTier {

    enum class Start { Online, Cancelled, Failed, Offline };

    constexpr int ONLINE_SECONDS = 45;

    bool available();
    Start start(const b_launcher_base::cancellation_token& token);
    std::string details();
    std::string networkStatus(const std::string& network);
    std::string device();
    bool join(const std::string& network);
    void leave(const std::string& network);
    std::string address(const std::string& network);
    bool refused(const std::string& network);

    bool serve(int port, std::function<int()> target);
    int expose(const std::string& host, int port, int preferred);
    int links();
    void halt();
    void stop();
}

#pragma once

#include <core/base/cancellation.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace RoomApi {

    struct Reply {
        bool ok = false;
        long status = 0;
        std::string error;
        std::string message;
    };

    struct Service {
        bool maintenance = false;
        std::string message;
        int available = 0;
        int total = 0;
    };

    struct Created {
        std::string code;
        std::string network;
        std::string token;
    };

    struct Member {
        std::string device;
        std::string name;
        std::vector<std::string> addresses;
        bool authorized = false;
        bool host = false;
        long long seen = 0;
    };

    struct Room {
        int port = 0;
        std::vector<Member> members;
        std::vector<std::string> banned;
    };

    bool validCode(const std::string& code);
    bool gone(const Reply& reply);
    std::string describe(const Reply& reply);

    Reply service(Service& out, const b_launcher_base::cancellation_token& token);
    Reply create(Created& out, const b_launcher_base::cancellation_token& token);
    Reply exists(const std::string& code, bool& out, const b_launcher_base::cancellation_token& token);
    Reply join(const std::string& code, std::string& network, const b_launcher_base::cancellation_token& token);
    Reply authorize(const std::string& code, const std::string& device,
                    const b_launcher_base::cancellation_token& token);
    Reply bindPort(const std::string& code, const std::string& roomToken, int port, const std::string& device,
                   const b_launcher_base::cancellation_token& token);
    Reply bindName(const std::string& code, const std::string& device, const std::string& name,
                   const b_launcher_base::cancellation_token& token);
    Reply kick(const std::string& code, const std::string& roomToken, const std::string& device,
               const b_launcher_base::cancellation_token& token);
    Reply ban(const std::string& code, const std::string& roomToken, const std::string& device,
              const b_launcher_base::cancellation_token& token);
    Reply keepalive(const std::string& code, const std::string& roomToken,
                    const b_launcher_base::cancellation_token& token);
    Reply close(const std::string& code, const std::string& roomToken, std::chrono::milliseconds timeout);
    Reply room(const std::string& code, Room& out, const b_launcher_base::cancellation_token& token);
}

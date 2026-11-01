#pragma once

#include "app/multiplayer/LanWorlds.h"

#include <string>
#include <vector>

namespace Multiplayer {

    enum class Role { None, Host, Guest };
    enum class Stage { Starting, Opening, Linking, Searching, Ready, Closing, Failed };

    struct Member {
        std::string device;
        std::string name;
        std::string address;
        bool self = false;
        bool host = false;
        bool online = false;
    };

    struct State {
        Role role = Role::None;
        Stage stage = Stage::Starting;
        std::string code;
        std::string error;
        std::string detail;
        std::string address;
        std::string hostName;
        bool published = false;
        int target = 0;
        int links = 0;
        std::vector<Member> members;
    };

    struct Service {
        bool known = false;
        bool checking = false;
        bool reachable = false;
        bool maintenance = false;
        std::string message;
        int available = 0;
        int total = 0;
    };

    struct Lookup {
        std::string code;
        bool checking = false;
        bool done = false;
        bool exists = false;
        std::string error;
    };

    bool available();
    void warmUp();
    const State& state();
    bool active();
    std::string stageText();

    const Service& service();
    void watchService();
    bool canHost();

    const Lookup& lookup();
    void check(const std::string& code);
    void forgetLookup();

    const std::vector<LanWorlds::World>& worlds();
    int worldsSerial();
    void watchWorlds();
    int picked();
    void pick(int port);
    int typed();
    void type(int port);

    void host();
    void join(const std::string& code);
    void kick(const std::string& device, bool ban);
    bool canRetry();
    void retry();
    void leave();
    void dismiss();

    void poll();
    void shutdown();
}

#pragma once

#include <string>
#include <vector>

namespace Game {

    enum class State { Idle, Preparing, Running };

    struct Step {
        enum class Status { Waiting, Running, Done, Failed };

        std::string label;
        Status status = Status::Waiting;
        std::string detail;
    };

    struct Session {
        int id = 0;
        std::string instance;
        std::string name;
        std::string account;
        std::string accountKey;
        State state = State::Idle;
        int log = 0;
        double uptime = 0.0;
    };

    State state(const std::string& key);
    float progress(const std::string& key);
    std::vector<Step> steps(const std::string& key);
    std::vector<Session> sessions();
    std::vector<Session> sessionsOf(const std::string& key);
    bool preparing();
    bool running();
    bool active();
    bool inUse(const std::string& accountKey);

    std::string accountOf(const std::string& key);
    void setAccount(const std::string& key, const std::string& accountKey);

    int launch(const std::string& key, bool test = false, const std::string& account = std::string());
    void setQuiet(bool on);
    void cancel(const std::string& key);
    void stop(const std::string& key);
    void stop(int session);
    void poll();
    void shutdown();
}

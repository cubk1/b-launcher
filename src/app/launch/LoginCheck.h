#pragma once

#include <core/base/error.hpp>

#include <string>

namespace LoginCheck {

    // What the connectivity probe found after a launch failed while logging in.
    enum class Reach {
        Offline,     // no internet connection at all
        LoginServer, // the internet works, the login server does not answer
        Reachable,   // both answer, so the login server refused the login by itself
    };

    struct Problem {
        // The failed launch, kept so the dialog can try again with the same arguments.
        std::string instance;
        std::string name;
        std::string account;
        std::string server;
        bool test = false;
        // Accounts::describe() of the failure, shown in the dialog.
        std::string reason;
        Reach reach = Reach::LoginServer;
    };

    // Whether a launch that failed this way means the account could not log in over the network.
    bool loginFailed(b_launcher_base::errc code);

    // Probes the connection in the background after a launch failed to log in, then asks the
    // interface to show the offline dialog. Polling happens on the owner thread.
    void report(std::string instance, std::string name, std::string account, std::string server, bool test,
                std::string reason);
    void poll();
    void shutdown();
}

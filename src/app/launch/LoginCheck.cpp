#include "app/launch/LoginCheck.h"
#include "app/accounts/Accounts.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Connectivity.h"
#include "app/system/Interface.h"
#include "app/system/Notices.h"

#include <core/auth/yggdrasil.hpp>
#include <core/base/strings.hpp>

#include <string>
#include <thread>
#include <vector>

namespace LoginCheck {

    namespace blb = b_launcher_base;

    namespace {
        AsyncTasks tasks;

        // The servers the launcher logs in against, so a working internet alone does not hide them.
        std::vector<std::string> loginUrls(const blb::auth::account* account) {
            std::vector<std::string> urls;
            if (account != nullptr && blb::auth::is_yggdrasil(account->type)) {
                std::string server(blb::trim(account->server));
                if (!server.empty()) {
                    urls.push_back(std::move(server));
                }
                return urls;
            }
            urls.push_back("https://login.live.com/oauth20_authorize.srf");
            urls.push_back("https://api.minecraftservices.com/");
            return urls;
        }

        Reach probe(const std::vector<std::string>& urls) {
            bool online = false;
            bool login = false;
            // Both checks wait on the network, so they run together instead of one after the other.
            std::thread reaches([&online] { online = Connectivity::internet(); });
            std::thread answers([&login, &urls] { login = Connectivity::answers(urls); });
            reaches.join();
            answers.join();
            if (!online) {
                return Reach::Offline;
            }
            return login ? Reach::Reachable : Reach::LoginServer;
        }

        void present(Problem problem) {
            if (!Interface::loginFailed(problem)) {
                Notices::add(Notices::Kind::Failed, "启动失败：" + problem.reason, 5000);
            }
        }
    }

    bool loginFailed(blb::errc code) {
        switch (code) {
            // Everything a missing or broken route to the login server can raise.
            case blb::errc::network:
            case blb::errc::timeout:
            case blb::errc::http_status:
            case blb::errc::auth:
            case blb::errc::unknown: return true;
            // Credentials that need the user, not the network.
            default: return false;
        }
    }

    void report(std::string instance, std::string name, std::string account, std::string server, bool test,
                std::string reason) {
        Problem problem;
        problem.instance = std::move(instance);
        problem.name = std::move(name);
        problem.account = std::move(account);
        problem.server = std::move(server);
        problem.test = test;
        problem.reason = std::move(reason);
        std::vector<std::string> urls = loginUrls(Accounts::find(problem.account));
        tasks.run(
                [urls, problem](blb::cancellation_token token) -> AsyncTasks::Continuation {
                    Reach reach = Reach::LoginServer;
                    if (!token.is_cancelled()) {
                        reach = probe(urls);
                    }
                    return [problem, reach] {
                        Problem next = problem;
                        next.reach = reach;
                        present(std::move(next));
                    };
                },
                [problem](const std::string&) -> AsyncTasks::Continuation {
                    return [problem] {
                        Problem next = problem;
                        next.reach = Reach::LoginServer;
                        present(std::move(next));
                    };
                });
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}

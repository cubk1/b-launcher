#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <string>

class Redirect {
public:
    using Query = std::map<std::string, std::string>;

    Redirect() = default;
    Redirect(const Redirect&) = delete;
    Redirect& operator=(const Redirect&) = delete;
    ~Redirect();

    b_launcher_base::result<void> open(int port);
    b_launcher_base::result<Query> wait(const std::string& path, b_launcher_base::cancellation_token token,
                                        std::chrono::seconds timeout,
                                        const std::function<std::string(const Query&)>& page);
    void close();

private:
    unsigned long long sockets[2] = {~0ull, ~0ull};
};

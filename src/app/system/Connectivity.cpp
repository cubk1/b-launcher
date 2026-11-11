#include "app/system/Connectivity.h"

#include <core/network/http_client.hpp>

#include <string>
#include <vector>

namespace Connectivity {

    namespace {
        namespace blb = b_launcher_base;
        const std::vector<std::string>& internetHosts() {
            static const std::vector<std::string> hosts = {
                    "8.8.8.8",
                    "1.2.4.8",
                    "223.5.5.5",
            };
            return hosts;
        }

        // The resolver port, which these servers always accept connections on.
        constexpr unsigned short DNS_PORT = 53;

        bool answered(const std::string& url, std::chrono::milliseconds timeout) {
            blb::net::http_request request;
            // Only the route matters, so the headers are enough of an answer.
            request.method = "HEAD";
            request.url = url;
            request.timeout = timeout;
            request.follow_redirects = true;
            request.max_response_size = 4096;
            auto response = blb::net::http_send(request);
            // Any status code means the host answered; only a curl failure means no route to it.
            return response.has_value();
        }
    }

    bool answers(const std::string& url, std::chrono::milliseconds timeout) {
        return !url.empty() && answered(url, timeout);
    }

    bool answers(const std::vector<std::string>& urls, std::chrono::milliseconds timeout) {
        for (const std::string& url : urls) {
            if (answered(url, timeout)) {
                return true;
            }
        }
        return false;
    }

    bool internet(std::chrono::milliseconds timeout) {
        // A connection is enough: DNS servers say nothing over HTTP on this port.
        for (const std::string& host : internetHosts()) {
            if (blb::net::tcp_reachable(host, DNS_PORT, timeout)) {
                return true;
            }
        }
        return false;
    }
}

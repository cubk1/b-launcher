#include "Redirect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

namespace blb = b_launcher_base;

namespace {
    constexpr size_t REQUEST_LIMIT = 16 * 1024;

    bool startWinsock() {
        static bool started = [] {
            WSADATA data;
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        return started;
    }

    SOCKET listenOn(int family, int port) {
        SOCKET s = socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            return INVALID_SOCKET;
        }
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*) &exclusive, sizeof(exclusive));
        int bound;
        if (family == AF_INET6) {
            DWORD only = 1;
            setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*) &only, sizeof(only));
            sockaddr_in6 address = {};
            address.sin6_family = AF_INET6;
            address.sin6_addr = in6addr_loopback;
            address.sin6_port = htons((u_short) port);
            bound = bind(s, (const sockaddr*) &address, sizeof(address));
        } else {
            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons((u_short) port);
            bound = bind(s, (const sockaddr*) &address, sizeof(address));
        }
        if (bound != 0 || listen(s, 8) != 0) {
            closesocket(s);
            return INVALID_SOCKET;
        }
        return s;
    }

    int hexValue(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    std::string urlDecode(const std::string& text) {
        std::string out;
        for (size_t i = 0; i < text.size(); i++) {
            char c = text[i];
            if (c == '+') {
                out += ' ';
            } else if (c == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0) {
                out += (char) (hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
                i += 2;
            } else {
                out += c;
            }
        }
        return out;
    }

    Redirect::Query parseQuery(const std::string& text) {
        Redirect::Query out;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('&', start);
            std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!item.empty()) {
                size_t equals = item.find('=');
                out[urlDecode(item.substr(0, equals))] =
                        equals == std::string::npos ? std::string() : urlDecode(item.substr(equals + 1));
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return out;
    }

    void respond(SOCKET client, const char* status, const std::string& body) {
        std::string reply = std::string("HTTP/1.1 ") + status +
                "\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n"
                "Content-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
        size_t sent = 0;
        while (sent < reply.size()) {
            int n = send(client, reply.data() + sent, (int) (reply.size() - sent), 0);
            if (n <= 0) {
                break;
            }
            sent += (size_t) n;
        }
        shutdown(client, SD_SEND);
    }

    bool readRequest(SOCKET client, std::string& target) {
        DWORD timeout = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout, sizeof(timeout));
        std::string head;
        char buffer[2048];
        while (head.find("\r\n\r\n") == std::string::npos && head.size() < REQUEST_LIMIT) {
            int n = recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                break;
            }
            head.append(buffer, (size_t) n);
        }
        size_t lineEnd = head.find("\r\n");
        std::string line = head.substr(0, lineEnd);
        if (line.rfind("GET ", 0) != 0) {
            return false;
        }
        size_t targetEnd = line.find(' ', 4);
        target = line.substr(4, targetEnd == std::string::npos ? std::string::npos : targetEnd - 4);
        return true;
    }
}

Redirect::~Redirect() {
    close();
}

blb::result<void> Redirect::open(int port) {
    close();
    if (!startWinsock()) {
        return blb::make_error(blb::errc::network, "无法初始化网络");
    }
    SOCKET v4 = listenOn(AF_INET, port);
    SOCKET v6 = listenOn(AF_INET6, port);
    if (v4 == INVALID_SOCKET) {
        if (v6 != INVALID_SOCKET) {
            closesocket(v6);
        }
        return blb::make_error(blb::errc::network, "端口 " + std::to_string(port) + " 被其他程序占用");
    }
    sockets[0] = (unsigned long long) v4;
    sockets[1] = (unsigned long long) v6;
    return {};
}

void Redirect::close() {
    for (unsigned long long& s : sockets) {
        if ((SOCKET) s != INVALID_SOCKET) {
            closesocket((SOCKET) s);
            s = ~0ull;
        }
    }
}

blb::result<Redirect::Query> Redirect::wait(const std::string& path, blb::cancellation_token token,
                                            std::chrono::seconds timeout,
                                            const std::function<std::string(const Query&)>& page) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (token.is_cancelled()) {
            return blb::make_error(blb::errc::cancelled, "已取消");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return blb::make_error(blb::errc::auth_expired, "等待浏览器登录超时");
        }
        fd_set readable;
        FD_ZERO(&readable);
        for (unsigned long long s : sockets) {
            if ((SOCKET) s != INVALID_SOCKET) {
                FD_SET((SOCKET) s, &readable);
            }
        }
        if (readable.fd_count == 0) {
            return blb::make_error(blb::errc::network, "没有在等待登录");
        }
        timeval step = {0, 200 * 1000};
        if (select(0, &readable, nullptr, nullptr, &step) <= 0) {
            continue;
        }
        for (unsigned long long s : sockets) {
            if ((SOCKET) s == INVALID_SOCKET || !FD_ISSET((SOCKET) s, &readable)) {
                continue;
            }
            SOCKET client = accept((SOCKET) s, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                continue;
            }
            std::string target;
            if (!readRequest(client, target)) {
                closesocket(client);
                continue;
            }
            size_t mark = target.find('?');
            std::string requested = target.substr(0, mark);
            if (requested != path) {
                respond(client, "404 Not Found", "");
                closesocket(client);
                continue;
            }
            Query query = mark == std::string::npos ? Query() : parseQuery(target.substr(mark + 1));
            respond(client, "200 OK", page ? page(query) : std::string());
            closesocket(client);
            return query;
        }
    }
}

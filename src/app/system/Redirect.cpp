#include "app/system/Redirect.h"
#include "app/system/About.h"
#include "app/system/Interface.h"
#include "app/system/Sockets.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace blb = b_launcher_base;

namespace {
    constexpr size_t REQUEST_LIMIT = 16 * 1024;
    constexpr size_t CLIENT_LIMIT = 16;
    using Clock = std::chrono::steady_clock;

    struct Client {
        explicit Client(SOCKET value) : socket(value), deadline(Clock::now() + std::chrono::seconds(5)) {}
        ~Client() { closesocket(socket); }
        SOCKET socket;
        Clock::time_point deadline;
        std::string request;
        std::string reply;
        size_t sent = 0;
        std::optional<Redirect::Query> accepted;
        bool done = false;
    };

    bool nonBlocking(SOCKET socket) {
#if defined(_WIN32)
        u_long enabled = 1;
        return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
        int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    }

    bool startWinsock() {
#if defined(_WIN32)
        static bool started = [] {
            WSADATA data;
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        return started;
#else
        return true;
#endif
    }

    SOCKET listenOn(int family, int port, int* failure = nullptr) {
        if (failure) *failure = 0;
        SOCKET s = socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            if (failure) *failure = WSAGetLastError();
            return INVALID_SOCKET;
        }
#if defined(_WIN32)
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*) &exclusive, sizeof(exclusive));
#else
        int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#if defined(SO_NOSIGPIPE)
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &reuse, sizeof(reuse));
#endif
#endif
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
            if (failure) *failure = WSAGetLastError();
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

    std::string response(const char* status, const std::string& body) {
        return std::string("HTTP/1.1 ") + status +
                "\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n"
                "Content-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
    }

    std::string closingPage() {
        return std::string("<!doctype html><html><head><meta charset=\"utf-8\"><title>") + About::NAME +
                "</title></head><body><script>window.close()</script></body></html>";
    }

    bool requestTarget(const std::string& head, std::string& target) {
        size_t lineEnd = head.find("\r\n");
        std::string line = head.substr(0, lineEnd);
        if (line.rfind("GET ", 0) != 0) {
            return false;
        }
        size_t targetEnd = line.find(' ', 4);
        if (targetEnd == std::string::npos) {
            return false;
        }
        target = line.substr(4, targetEnd - 4);
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
    int v6Failure = 0;
    SOCKET v6 = listenOn(AF_INET6, port, &v6Failure);
    bool v6Unavailable = v6Failure == WSAEAFNOSUPPORT || v6Failure == WSAEADDRNOTAVAIL;
    if (v4 == INVALID_SOCKET || (v6 == INVALID_SOCKET && !v6Unavailable)) {
        if (v4 != INVALID_SOCKET) {
            closesocket(v4);
        }
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
                                            const std::function<bool(const Query&)>& accept) {
    auto deadline = Clock::now() + timeout;
    for (auto socket : sockets) {
        if ((SOCKET) socket != INVALID_SOCKET && !nonBlocking((SOCKET) socket)) {
            return blb::make_error(blb::errc::network, "无法配置登录端口");
        }
    }
    std::vector<std::unique_ptr<Client>> clients;
    while (true) {
        if (token.is_cancelled()) {
            return blb::make_error(blb::errc::cancelled, "已取消");
        }
        if (Clock::now() >= deadline) {
            return blb::make_error(blb::errc::auth_expired, "等待浏览器登录超时");
        }
        fd_set readable;
        fd_set writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        int listening = 0;
        SOCKET highest = 0;
        for (unsigned long long s : sockets) {
            if ((SOCKET) s != INVALID_SOCKET) {
                FD_SET((SOCKET) s, &readable);
                highest = std::max(highest, (SOCKET) s);
                listening++;
            }
        }
        if (listening == 0 && clients.empty()) {
            return blb::make_error(blb::errc::network, "没有在等待登录");
        }
        for (const auto& client : clients) {
            if (client->reply.empty()) FD_SET(client->socket, &readable);
            else FD_SET(client->socket, &writable);
            highest = std::max(highest, client->socket);
        }
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
        timeval step = {};
        step.tv_usec = (decltype(step.tv_usec)) std::clamp<long long>(remaining, 0, 200000);
        int ready = select((int) highest + 1, &readable, &writable, nullptr, &step);
        if (ready < 0 && WSAGetLastError() != WSAEINTR) {
            return blb::make_error(blb::errc::network, "无法等待浏览器登录");
        }
        if (ready < 0) {
            continue;
        }
        if (token.is_cancelled()) {
            return blb::make_error(blb::errc::cancelled, "已取消");
        }
        for (unsigned long long s : sockets) {
            if ((SOCKET) s == INVALID_SOCKET || !FD_ISSET((SOCKET) s, &readable)) {
                continue;
            }
            for (size_t accepted = 0; accepted < CLIENT_LIMIT; ++accepted) {
                SOCKET socket = ::accept((SOCKET) s, nullptr, nullptr);
                if (socket == INVALID_SOCKET) break;
                if (clients.size() >= CLIENT_LIMIT || !nonBlocking(socket)) {
                    closesocket(socket);
                } else {
                    clients.push_back(std::make_unique<Client>(socket));
                }
            }
        }
        for (auto& client : clients) {
            if (Clock::now() >= client->deadline) {
                if (client->accepted) return std::move(*client->accepted);
                client->done = true;
                continue;
            }
            if (client->reply.empty() && FD_ISSET(client->socket, &readable)) {
                char buffer[2048];
                int n = (int) recv(client->socket, buffer, sizeof(buffer), 0);
                if (n <= 0) {
                    client->done = n == 0 || WSAGetLastError() != WSAEWOULDBLOCK;
                    continue;
                }
                client->request.append(buffer, (size_t) n);
                auto end = client->request.find("\r\n\r\n");
                if ((end == std::string::npos && client->request.size() >= REQUEST_LIMIT) ||
                    (end != std::string::npos && end + 4 > REQUEST_LIMIT)) {
                    client->reply = response("431 Request Header Fields Too Large", "");
                } else if (end != std::string::npos) {
                    std::string target;
                    if (!requestTarget(client->request, target)) {
                        client->reply = response("400 Bad Request", "");
                    } else {
                        size_t mark = target.find('?');
                        if (target.substr(0, mark) != path) {
                            client->reply = response("404 Not Found", "");
                        } else {
                            Query query = mark == std::string::npos ? Query() : parseQuery(target.substr(mark + 1));
                            if (accept && !accept(query)) {
                                client->reply = response("400 Bad Request", "");
                            } else {
                                client->reply = response("200 OK", closingPage());
                                client->accepted = std::move(query);
                                Interface::summon();
                            }
                        }
                    }
                }
            }
            if (!client->reply.empty() && (client->sent == 0 || FD_ISSET(client->socket, &writable))) {
                int amount = (int) std::min<size_t>(16384, client->reply.size() - client->sent);
#if defined(MSG_NOSIGNAL)
                int n = (int) send(client->socket, client->reply.data() + client->sent, amount, MSG_NOSIGNAL);
#else
                int n = (int) send(client->socket, client->reply.data() + client->sent, amount, 0);
#endif
                if (n > 0) client->sent += (size_t) n;
                bool failed = n <= 0 && (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK);
                if (failed || client->sent == client->reply.size()) {
                    if (client->accepted) return std::move(*client->accepted);
                    client->done = true;
                }
            }
        }
        std::erase_if(clients, [](const auto& client) { return client->done; });
    }
}

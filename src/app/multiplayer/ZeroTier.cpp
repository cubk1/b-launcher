#include "app/multiplayer/ZeroTier.h"
#include "app/system/Platform.h"

#if defined(BLB_ZEROTIER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#define ADD_EXPORTS
#include <ZeroTierSockets.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace ZeroTier {

    namespace blb = b_launcher_base;

#if defined(BLB_ZEROTIER)
    namespace {
        using Clock = std::chrono::steady_clock;

        const char* const IDENTITY = "ZeroTierIdentity";
        const char* const PORT = "ZeroTierPort";
        constexpr int SLICE = 250;
        constexpr size_t BUFFER = 32 * 1024;
        constexpr int CONNECT_LIMIT = 10000;
        constexpr int FIRST_DATA_LIMIT = 20000;
        constexpr int LOCAL_CONNECT_LIMIT = 3000;
        constexpr int STALL_LIMIT = 100;

        struct Link {
            int remote = -1;
            SOCKET local = INVALID_SOCKET;
            std::atomic<bool> over{false};
            std::atomic<bool> finished{false};
            std::thread thread;
        };

        std::mutex startLock;
        bool started = false;
        bool fresh = false;
        int startError = 0;
        int rememberedPort = 0;

        std::mutex moonsLock;
        std::vector<std::pair<uint64_t, uint64_t>> wantedMoons;
        std::vector<uint64_t> orbitedMoons;

        std::mutex linksLock;
        std::vector<std::unique_ptr<Link>> linked;
        std::atomic<bool> halting{false};
        std::atomic<int> active{0};
        std::thread acceptor;

        void startSockets() {
            static std::once_flag once;
            std::call_once(once, [] {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
            });
        }

        uint64_t hexValue(const std::string& text) {
            if (text.empty() || text.size() > 16) {
                return 0;
            }
            uint64_t value = 0;
            for (char c : text) {
                int digit = c >= '0' && c <= '9' ? c - '0'
                        : c >= 'a' && c <= 'f'   ? c - 'a' + 10
                        : c >= 'A' && c <= 'F'   ? c - 'A' + 10
                                                 : -1;
                if (digit < 0) {
                    return 0;
                }
                value = value << 4 | (uint64_t) digit;
            }
            return value;
        }

        uint64_t networkId(const std::string& network) {
            return network.size() == 16 ? hexValue(network) : 0;
        }

        int loadPort() {
            std::optional<std::string> text = Platform::loadValue(PORT);
            int port = text ? std::atoi(text->c_str()) : 0;
            return port >= 1024 && port <= 65535 ? port : 0;
        }

        bool portFree(int port) {
            SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (probe == INVALID_SOCKET) {
                return false;
            }
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons((u_short) port);
            bool free = bind(probe, (const sockaddr*) &address, sizeof(address)) == 0;
            closesocket(probe);
            return free;
        }

        void syncMoons() {
            std::lock_guard<std::mutex> guard(moonsLock);
            if (zts_node_is_online() != 1) {
                return;
            }
            for (const auto& [id, seed] : wantedMoons) {
                if (std::find(orbitedMoons.begin(), orbitedMoons.end(), id) == orbitedMoons.end() &&
                    zts_moon_orbit(id, seed) == ZTS_ERR_OK) {
                    orbitedMoons.push_back(id);
                }
            }
            std::erase_if(orbitedMoons, [](uint64_t id) {
                bool wanted = std::any_of(wantedMoons.begin(), wantedMoons.end(),
                                          [id](const auto& moon) { return moon.first == id; });
                return !wanted && zts_moon_deorbit(id) == ZTS_ERR_OK;
            });
        }

        bool ended(const Link& link) {
            return link.over.load() || halting.load();
        }

        void setNonBlocking(SOCKET socket) {
            u_long on = 1;
            ioctlsocket(socket, FIONBIO, &on);
        }

        void setNoDelay(SOCKET socket) {
            BOOL on = TRUE;
            setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*) &on, sizeof(on));
        }

        bool waitLocal(SOCKET socket, bool write, int ms) {
            fd_set ready;
            fd_set failed;
            FD_ZERO(&ready);
            FD_ZERO(&failed);
            FD_SET(socket, &ready);
            FD_SET(socket, &failed);
            timeval limit{ms / 1000, (ms % 1000) * 1000};
            return select(0, write ? nullptr : &ready, write ? &ready : nullptr, &failed, &limit) > 0;
        }

        short waitRemote(int fd, short events, int ms) {
            zts_pollfd entry{fd, events, 0};
            int ready = zts_bsd_poll(&entry, 1, ms);
            if (ready < 0) {
                return ZTS_POLLERR;
            }
            return ready > 0 ? entry.revents : 0;
        }

        bool broken(short events) {
            return (events & (ZTS_POLLERR | ZTS_POLLNVAL)) != 0;
        }

        bool writeLocal(Link& link, const char* data, int size) {
            while (size > 0) {
                int sent = ::send(link.local, data, size, 0);
                if (sent > 0) {
                    data += sent;
                    size -= sent;
                    continue;
                }
                if (sent == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                    return false;
                }
                while (!ended(link) && !waitLocal(link.local, true, SLICE)) {
                }
                if (ended(link)) {
                    return false;
                }
            }
            return true;
        }

        bool writeRemote(Link& link, const char* data, size_t size) {
            int stalls = 0;
            while (size > 0) {
                ssize_t sent = zts_bsd_send(link.remote, data, size, 0);
                if (sent > 0) {
                    data += sent;
                    size -= (size_t) sent;
                    stalls = 0;
                    continue;
                }
                if (zts_get_last_socket_error(link.remote) != 0 || ++stalls > STALL_LIMIT) {
                    return false;
                }
                short events = 0;
                while (!ended(link) && (events = waitRemote(link.remote, ZTS_POLLOUT, SLICE)) == 0) {
                }
                if (ended(link) || broken(events)) {
                    return false;
                }
            }
            return true;
        }

        void localToRemote(Link& link) {
            std::vector<char> buffer(BUFFER);
            while (!ended(link)) {
                if (!waitLocal(link.local, false, SLICE)) {
                    continue;
                }
                int got = ::recv(link.local, buffer.data(), (int) buffer.size(), 0);
                if (got > 0) {
                    if (!writeRemote(link, buffer.data(), (size_t) got)) {
                        break;
                    }
                    continue;
                }
                if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
                    continue;
                }
                break;
            }
            link.over = true;
        }

        void remoteToLocal(Link& link) {
            std::vector<char> buffer(BUFFER);
            int stalls = 0;
            while (!ended(link)) {
                short events = waitRemote(link.remote, ZTS_POLLIN, SLICE);
                if (events == 0) {
                    continue;
                }
                ssize_t got = zts_bsd_recv(link.remote, buffer.data(), buffer.size(), 0);
                if (got > 0) {
                    stalls = 0;
                    if (!writeLocal(link, buffer.data(), (int) got)) {
                        break;
                    }
                    continue;
                }
                if (got == 0 || broken(events) || ++stalls > STALL_LIMIT) {
                    break;
                }
            }
            link.over = true;
        }

        void pipe(Link& link) {
            active++;
            std::thread up([&link] { localToRemote(link); });
            remoteToLocal(link);
            up.join();
            active--;
        }

        void finish(Link& link) {
            if (link.local != INVALID_SOCKET) {
                closesocket(link.local);
                link.local = INVALID_SOCKET;
            }
            if (link.remote >= 0) {
                zts_bsd_close(link.remote);
                link.remote = -1;
            }
            link.finished = true;
        }

        void reap(bool all) {
            std::vector<std::unique_ptr<Link>> done;
            {
                std::lock_guard<std::mutex> guard(linksLock);
                for (auto it = linked.begin(); it != linked.end();) {
                    if (all || (*it)->finished) {
                        done.push_back(std::move(*it));
                        it = linked.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            for (const std::unique_ptr<Link>& link : done) {
                if (link->thread.joinable()) {
                    link->thread.join();
                }
            }
        }

        template <typename Run>
        void spawn(std::unique_ptr<Link> link, Run run) {
            Link* raw = link.get();
            raw->thread = std::thread([raw, run] { run(*raw); });
            std::lock_guard<std::mutex> guard(linksLock);
            linked.push_back(std::move(link));
        }

        SOCKET connectLocal(int port) {
            SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket == INVALID_SOCKET) {
                return socket;
            }
            setNonBlocking(socket);
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons((u_short) port);
            target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            bool connected = false;
            if (connect(socket, (const sockaddr*) &target, sizeof(target)) == 0) {
                connected = true;
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set writable;
                fd_set failed;
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET(socket, &writable);
                FD_SET(socket, &failed);
                timeval limit{LOCAL_CONNECT_LIMIT / 1000, (LOCAL_CONNECT_LIMIT % 1000) * 1000};
                if (select(0, nullptr, &writable, &failed, &limit) > 0 && !FD_ISSET(socket, &failed)) {
                    int error = 0;
                    int length = sizeof(error);
                    connected = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char*) &error, &length) == 0 && error == 0;
                }
            }
            if (!connected) {
                closesocket(socket);
                return INVALID_SOCKET;
            }
            setNoDelay(socket);
            return socket;
        }

        int openRemote(const std::string& host, int port) {
            int fd = zts_bsd_socket(ZTS_AF_INET, ZTS_SOCK_STREAM, 0);
            if (fd < 0) {
                return -1;
            }
            zts_set_blocking(fd, 0);
            zts_sockaddr_in target{};
            target.sin_len = sizeof(target);
            target.sin_family = ZTS_AF_INET;
            target.sin_port = htons((u_short) port);
            if (zts_inet_pton(ZTS_AF_INET, host.c_str(), &target.sin_addr) != 1) {
                zts_bsd_close(fd);
                return -1;
            }
            zts_bsd_connect(fd, (const zts_sockaddr*) &target, sizeof(target));
            return fd;
        }

        bool connected(int fd, short events) {
            return (events & ZTS_POLLOUT) != 0 && !broken(events) && zts_get_last_socket_error(fd) == 0;
        }

        bool sending(int fd) {
            char first = 0;
            return zts_bsd_recv(fd, &first, 1, ZTS_MSG_PEEK) > 0;
        }

        void hostLink(Link& link, const std::function<int()>& target) {
            zts_set_blocking(link.remote, 0);
            short events = 0;
            auto deadline = Clock::now() + std::chrono::milliseconds(FIRST_DATA_LIMIT);
            while (!halting && Clock::now() < deadline && (events = waitRemote(link.remote, ZTS_POLLIN, SLICE)) == 0) {
            }
            int port = target();
            if (!halting && port > 0 && (events & ZTS_POLLIN) != 0 && !broken(events) && sending(link.remote)) {
                link.local = connectLocal(port);
                if (link.local != INVALID_SOCKET) {
                    zts_set_no_delay(link.remote, 1);
                    pipe(link);
                }
            }
            finish(link);
        }

        void guestLink(Link& link, const std::string& host, int port) {
            link.remote = openRemote(host, port);
            if (link.remote >= 0) {
                short events = 0;
                auto deadline = Clock::now() + std::chrono::milliseconds(CONNECT_LIMIT);
                while (!halting && Clock::now() < deadline &&
                       (events = waitRemote(link.remote, ZTS_POLLOUT, SLICE)) == 0) {
                }
                if (!halting && connected(link.remote, events)) {
                    zts_set_no_delay(link.remote, 1);
                    pipe(link);
                }
            }
            finish(link);
        }
    }

    bool available() {
        return true;
    }

    Start start(const blb::cancellation_token& token) {
        startSockets();
        {
            std::lock_guard<std::mutex> guard(startLock);
            if (!started) {
                std::optional<std::string> saved = Platform::loadValue(IDENTITY);
                char key[ZTS_ID_STR_BUF_LEN] = {};
                bool restored = false;
                if (saved && !saved->empty() && saved->size() < sizeof(key)) {
                    std::memcpy(key, saved->data(), saved->size());
                    restored = zts_id_pair_is_valid(key, sizeof(key)) == 1 &&
                            zts_init_from_memory(key, sizeof(key)) == ZTS_ERR_OK;
                }
                rememberedPort = loadPort();
                if (rememberedPort > 0 && portFree(rememberedPort)) {
                    zts_init_set_port((unsigned short) rememberedPort);
                }
                startError = zts_node_start();
                if (startError != ZTS_ERR_OK) {
                    return Start::Failed;
                }
                started = true;
                fresh = !restored;
            }
        }
        auto deadline = Clock::now() + std::chrono::seconds(ONLINE_SECONDS);
        while (zts_node_is_online() != 1) {
            if (token.wait_for(std::chrono::milliseconds(SLICE))) {
                return Start::Cancelled;
            }
            if (Clock::now() >= deadline) {
                return Start::Offline;
            }
        }
        {
            std::lock_guard<std::mutex> guard(startLock);
            if (fresh) {
                char key[ZTS_ID_STR_BUF_LEN] = {};
                unsigned int length = sizeof(key);
                if (zts_node_get_id_pair(key, &length) == ZTS_ERR_OK && length > 0 && length <= sizeof(key)) {
                    Platform::storeValue(IDENTITY, std::string(key, length), false);
                }
                fresh = false;
            }
            int port = zts_node_get_port();
            if (port > 0 && port != rememberedPort && Platform::storeValue(PORT, std::to_string(port), true)) {
                rememberedPort = port;
            }
        }
        syncMoons();
        return Start::Online;
    }

    void orbit(const std::vector<Moon>& moons) {
        {
            std::lock_guard<std::mutex> guard(moonsLock);
            wantedMoons.clear();
            for (const Moon& moon : moons) {
                uint64_t id = hexValue(moon.id);
                uint64_t seed = hexValue(moon.seed);
                if (id != 0 && seed != 0) {
                    wantedMoons.emplace_back(id, seed);
                }
            }
        }
        syncMoons();
    }

    std::string details() {
        std::lock_guard<std::mutex> guard(startLock);
        if (!started) {
            return startError != 0 ? "libzt 启动失败，返回 " + std::to_string(startError)
                                   : std::string("ZeroTier 没有启动");
        }
        char id[24] = {};
        std::snprintf(id, sizeof(id), "%010llx", (unsigned long long) zts_node_get_id());
        return std::string("节点 ") + id + "，UDP 端口 " + std::to_string(zts_node_get_port()) + "，" +
                (zts_node_is_online() == 1 ? "在线" : "离线");
    }

    std::string networkStatus(const std::string& network) {
        uint64_t id = networkId(network);
        int status = id != 0 ? zts_net_get_status(id) : -1;
        switch (status) {
            case ZTS_NETWORK_STATUS_REQUESTING_CONFIGURATION: return "正在获取网络配置";
            case ZTS_NETWORK_STATUS_OK: return "已连接";
            case ZTS_NETWORK_STATUS_ACCESS_DENIED: return "没有访问权限";
            case ZTS_NETWORK_STATUS_NOT_FOUND: return "网络不存在";
            case ZTS_NETWORK_STATUS_PORT_ERROR: return "端口出错";
            case ZTS_NETWORK_STATUS_CLIENT_TOO_OLD: return "客户端版本过旧";
            default: return "未知（" + std::to_string(status) + "）";
        }
    }

    std::string device() {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%010llx", (unsigned long long) zts_node_get_id());
        return text;
    }

    bool join(const std::string& network) {
        uint64_t id = networkId(network);
        return id != 0 && zts_net_join(id) == ZTS_ERR_OK;
    }

    void leave(const std::string& network) {
        uint64_t id = networkId(network);
        if (id != 0) {
            zts_net_leave(id);
        }
    }

    std::string address(const std::string& network) {
        uint64_t id = networkId(network);
        if (id == 0 || zts_net_transport_is_ready(id) != 1 || zts_addr_is_assigned(id, ZTS_AF_INET) != 1) {
            return std::string();
        }
        char text[ZTS_IP_MAX_STR_LEN] = {};
        if (zts_addr_get_str(id, ZTS_AF_INET, text, ZTS_IP_MAX_STR_LEN) != ZTS_ERR_OK) {
            return std::string();
        }
        return text;
    }

    bool refused(const std::string& network) {
        uint64_t id = networkId(network);
        if (id == 0) {
            return true;
        }
        int status = zts_net_get_status(id);
        return status == ZTS_NETWORK_STATUS_ACCESS_DENIED || status == ZTS_NETWORK_STATUS_NOT_FOUND ||
                status == ZTS_NETWORK_STATUS_CLIENT_TOO_OLD;
    }

    bool serve(int port, std::function<int()> target) {
        halt();
        int listener = zts_bsd_socket(ZTS_AF_INET, ZTS_SOCK_STREAM, 0);
        if (listener < 0) {
            return false;
        }
        if (zts_bind(listener, "0.0.0.0", (unsigned short) port) != ZTS_ERR_OK ||
            zts_listen(listener, 16) != ZTS_ERR_OK) {
            zts_bsd_close(listener);
            return false;
        }
        halting = false;
        acceptor = std::thread([listener, target = std::move(target)] {
            while (!halting) {
                reap(false);
                short events = waitRemote(listener, ZTS_POLLIN, SLICE);
                if ((events & ZTS_POLLIN) == 0) {
                    continue;
                }
                int client = zts_bsd_accept(listener, nullptr, nullptr);
                if (client < 0) {
                    continue;
                }
                auto link = std::make_unique<Link>();
                link->remote = client;
                spawn(std::move(link), [target](Link& running) { hostLink(running, target); });
            }
            zts_bsd_close(listener);
        });
        return true;
    }

    int expose(const std::string& host, int port, int preferred) {
        halt();
        startSockets();
        SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
            return 0;
        }
        BOOL exclusive = TRUE;
        setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*) &exclusive, sizeof(exclusive));
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local.sin_port = htons((u_short) preferred);
        if (bind(listener, (const sockaddr*) &local, sizeof(local)) == SOCKET_ERROR) {
            local.sin_port = 0;
            if (bind(listener, (const sockaddr*) &local, sizeof(local)) == SOCKET_ERROR) {
                closesocket(listener);
                return 0;
            }
        }
        sockaddr_in bound{};
        int length = sizeof(bound);
        if (listen(listener, 16) == SOCKET_ERROR ||
            getsockname(listener, (sockaddr*) &bound, &length) == SOCKET_ERROR) {
            closesocket(listener);
            return 0;
        }
        setNonBlocking(listener);
        halting = false;
        acceptor = std::thread([listener, host, port] {
            while (!halting) {
                reap(false);
                if (!waitLocal(listener, false, SLICE)) {
                    continue;
                }
                SOCKET client = accept(listener, nullptr, nullptr);
                if (client == INVALID_SOCKET) {
                    continue;
                }
                setNonBlocking(client);
                setNoDelay(client);
                auto link = std::make_unique<Link>();
                link->local = client;
                spawn(std::move(link), [host, port](Link& running) { guestLink(running, host, port); });
            }
            closesocket(listener);
        });
        return ntohs(bound.sin_port);
    }

    int links() {
        return active.load();
    }

    void halt() {
        halting = true;
        if (acceptor.joinable()) {
            acceptor.join();
        }
        reap(true);
    }

    void stop() {
        halt();
        std::lock_guard<std::mutex> guard(startLock);
        if (started) {
            zts_node_stop();
            started = false;
        }
    }
#else
    bool available() {
        return false;
    }

    Start start(const blb::cancellation_token&) {
        return Start::Failed;
    }

    void orbit(const std::vector<Moon>&) {}

    std::string details() {
        return "这个版本没有包含联机组件";
    }

    std::string networkStatus(const std::string&) {
        return std::string();
    }

    std::string device() {
        return std::string();
    }

    bool join(const std::string&) {
        return false;
    }

    void leave(const std::string&) {}

    std::string address(const std::string&) {
        return std::string();
    }

    bool refused(const std::string&) {
        return true;
    }

    bool serve(int, std::function<int()>) {
        return false;
    }

    int expose(const std::string&, int, int) {
        return 0;
    }

    int links() {
        return 0;
    }

    void halt() {}

    void stop() {}
#endif
}

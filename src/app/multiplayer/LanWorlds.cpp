#include "app/multiplayer/LanWorlds.h"

#include <core/network/server_ping.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

namespace LanWorlds {

    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr std::chrono::milliseconds PING_LIMIT(1500);
        constexpr std::chrono::seconds RECHECK(30);
        constexpr std::chrono::milliseconds ANNOUNCE_EVERY(1500);
        const char* const LOCAL_BROADCAST = "127.255.255.255";
        constexpr u_short ANNOUNCE_PORT = 4445;

        struct Known {
            std::optional<World> world;
            Clock::time_point checked;
        };

        std::map<DWORD, bool> javaPids;
        std::map<std::pair<DWORD, int>, Known> known;

        std::mutex announceLock;
        std::condition_variable announceWake;
        std::thread announcer;
        bool announcing = false;

        void startSockets() {
            static std::once_flag once;
            std::call_once(once, [] {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
            });
        }

        bool javaProcess(DWORD pid) {
            auto cached = javaPids.find(pid);
            if (cached != javaPids.end()) {
                return cached->second;
            }
            bool java = false;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process != nullptr) {
                wchar_t path[1024];
                DWORD size = (DWORD) std::size(path);
                if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                    std::wstring name(path, size);
                    size_t slash = name.find_last_of(L"\\/");
                    if (slash != std::wstring::npos) {
                        name.erase(0, slash + 1);
                    }
                    std::transform(name.begin(), name.end(), name.begin(),
                                   [](wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t) (c - L'A' + L'a') : c; });
                    java = name.starts_with(L"java") && name.ends_with(L".exe");
                }
                CloseHandle(process);
            }
            javaPids[pid] = java;
            return java;
        }

        template <typename Table, typename Row>
        void collect(ULONG family, std::set<std::pair<DWORD, int>>& out) {
            std::vector<unsigned char> buffer(16 * 1024);
            DWORD size = (DWORD) buffer.size();
            DWORD status = ERROR_INSUFFICIENT_BUFFER;
            for (int attempt = 0; attempt < 4 && status == ERROR_INSUFFICIENT_BUFFER; attempt++) {
                status = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
                if (status == ERROR_INSUFFICIENT_BUFFER) {
                    buffer.resize(size + 1024);
                    size = (DWORD) buffer.size();
                }
            }
            if (status != NO_ERROR) {
                return;
            }
            const Table* table = (const Table*) buffer.data();
            for (DWORD i = 0; i < table->dwNumEntries; i++) {
                const Row& row = table->table[i];
                int port = ntohs((u_short) row.dwLocalPort);
                if (port > 0 && row.dwOwningPid != 0) {
                    out.insert({row.dwOwningPid, port});
                }
            }
        }

        std::optional<World> ping(int port, const b_launcher_base::cancellation_token& token) {
            auto status = b_launcher_base::net::ping_server("127.0.0.1:" + std::to_string(port), token, PING_LIMIT);
            if (!status) {
                return std::nullopt;
            }
            World world;
            world.port = port;
            for (char c : status->motd) {
                bool space = c == '\n' || c == '\r' || c == '\t';
                if (!space) {
                    world.name.push_back(c);
                } else if (!world.name.empty() && world.name.back() != ' ') {
                    world.name.push_back(' ');
                }
            }
            while (!world.name.empty() && world.name.back() == ' ') {
                world.name.pop_back();
            }
            world.version = status->version;
            return world;
        }

        void announceLoop(std::string payload) {
            startSockets();
            SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket == INVALID_SOCKET) {
                return;
            }
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            // Discovery uses the packet's source as the game server address.
            // Loopback broadcast reaches all local discovery sockets and keeps
            // that address compatible with the loopback-only forwarding port.
            BOOL broadcast = TRUE;
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(ANNOUNCE_PORT);
            bool ready = bind(socket, (const sockaddr*) &local, sizeof(local)) == 0 &&
                    setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*) &broadcast, sizeof(broadcast)) == 0 &&
                    inet_pton(AF_INET, LOCAL_BROADCAST, &target.sin_addr) == 1;
            std::unique_lock<std::mutex> guard(announceLock);
            while (ready && announcing) {
                guard.unlock();
                sendto(socket, payload.data(), (int) payload.size(), 0, (const sockaddr*) &target, sizeof(target));
                guard.lock();
                announceWake.wait_for(guard, ANNOUNCE_EVERY, [] { return !announcing; });
            }
            guard.unlock();
            closesocket(socket);
        }
    }

    std::vector<World> scan(const b_launcher_base::cancellation_token& token) {
        std::set<std::pair<DWORD, int>> listening;
        collect<MIB_TCPTABLE_OWNER_PID, MIB_TCPROW_OWNER_PID>(AF_INET, listening);
        collect<MIB_TCP6TABLE_OWNER_PID, MIB_TCP6ROW_OWNER_PID>(AF_INET6, listening);
        std::set<DWORD> pids;
        for (const auto& [pid, port] : listening) {
            pids.insert(pid);
        }
        for (auto it = javaPids.begin(); it != javaPids.end();) {
            it = pids.count(it->first) == 0 ? javaPids.erase(it) : std::next(it);
        }
        for (auto it = known.begin(); it != known.end();) {
            it = listening.count(it->first) == 0 ? known.erase(it) : std::next(it);
        }
        std::vector<World> out;
        std::set<int> ports;
        Clock::time_point now = Clock::now();
        for (const auto& entry : listening) {
            if (token.is_cancelled()) {
                break;
            }
            if (!javaProcess(entry.first) || ports.count(entry.second) != 0) {
                continue;
            }
            Known& seen = known[entry];
            if (!seen.world && (seen.checked == Clock::time_point() || now - seen.checked >= RECHECK)) {
                seen.world = ping(entry.second, token);
                seen.checked = Clock::now();
            }
            if (seen.world) {
                ports.insert(entry.second);
                out.push_back(*seen.world);
            }
        }
        return out;
    }

    void announce(const std::string& motd, int port) {
        silence();
        std::string payload = "[MOTD]" + motd + "[/MOTD][AD]" + std::to_string(port) + "[/AD]";
        {
            std::lock_guard<std::mutex> guard(announceLock);
            announcing = true;
        }
        announcer = std::thread(announceLoop, std::move(payload));
    }

    void silence() {
        {
            std::lock_guard<std::mutex> guard(announceLock);
            announcing = false;
        }
        announceWake.notify_all();
        if (announcer.joinable()) {
            announcer.join();
        }
    }
}

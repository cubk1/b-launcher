#include "app/multiplayer/LanWorlds.h"

#include <core/network/server_ping.hpp>

#include "app/system/Sockets.h"

#if defined(_WIN32)
#include <windows.h>
#include <iphlpapi.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#else
#include <dirent.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#endif

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
#if defined(_WIN32)
            static std::once_flag once;
            std::call_once(once, [] {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
            });
#endif
        }

#if defined(_WIN32)
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
#elif defined(__APPLE__)
        bool javaProcess(DWORD pid) {
            auto cached = javaPids.find(pid);
            if (cached != javaPids.end()) {
                return cached->second;
            }
            char path[PROC_PIDPATHINFO_MAXSIZE];
            int length = proc_pidpath((int) pid, path, sizeof(path));
            std::string name = length > 0 ? std::string(path, (size_t) length) : std::string();
            size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) {
                name.erase(0, slash + 1);
            }
            bool java = name.starts_with("java");
            javaPids[pid] = java;
            return java;
        }

        void listeners(std::set<std::pair<DWORD, int>>& out) {
            int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
            if (bytes <= 0) {
                return;
            }
            std::vector<pid_t> pids((size_t) bytes / sizeof(pid_t) + 64);
            bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int) (pids.size() * sizeof(pid_t)));
            pids.resize(bytes > 0 ? (size_t) bytes / sizeof(pid_t) : 0);
            for (pid_t pid : pids) {
                if (pid <= 0 || !javaProcess((DWORD) pid)) {
                    continue;
                }
                int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
                if (size <= 0) {
                    continue;
                }
                std::vector<proc_fdinfo> fds((size_t) size / sizeof(proc_fdinfo));
                size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), (int) (fds.size() * sizeof(proc_fdinfo)));
                fds.resize(size > 0 ? (size_t) size / sizeof(proc_fdinfo) : 0);
                for (const proc_fdinfo& fd : fds) {
                    if (fd.proc_fdtype != PROX_FDTYPE_SOCKET) {
                        continue;
                    }
                    socket_fdinfo info{};
                    if (proc_pidfdinfo(pid, fd.proc_fd, PROC_PIDFDSOCKETINFO, &info, sizeof(info)) != sizeof(info) ||
                        info.psi.soi_kind != SOCKINFO_TCP || info.psi.soi_proto.pri_tcp.tcpsi_state != TSI_S_LISTEN) {
                        continue;
                    }
                    int port = ntohs((u_short) info.psi.soi_proto.pri_tcp.tcpsi_ini.insi_lport);
                    if (port > 0) {
                        out.insert({(DWORD) pid, port});
                    }
                }
            }
        }
#else
        bool javaProcess(DWORD pid) {
            auto cached = javaPids.find(pid);
            if (cached != javaPids.end()) {
                return cached->second;
            }
            std::error_code error;
            std::filesystem::path exe = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", error);
            bool java = !error && exe.filename().string().starts_with("java");
            javaPids[pid] = java;
            return java;
        }

        void listeners(std::set<std::pair<DWORD, int>>& out) {
            std::map<unsigned long, int> sockets;
            for (const char* table : {"/proc/net/tcp", "/proc/net/tcp6"}) {
                std::ifstream file(table);
                std::string line;
                std::getline(file, line);
                while (std::getline(file, line)) {
                    std::istringstream fields(line);
                    std::string slot, local, remote, state, queues, timer, retransmits, uid, timeout;
                    unsigned long inode = 0;
                    fields >> slot >> local >> remote >> state >> queues >> timer >> retransmits >> uid >> timeout >>
                            inode;
                    size_t colon = local.find_last_of(':');
                    if (state != "0A" || colon == std::string::npos || inode == 0) {
                        continue;
                    }
                    int port = (int) std::strtol(local.c_str() + colon + 1, nullptr, 16);
                    if (port > 0) {
                        sockets[inode] = port;
                    }
                }
            }
            if (sockets.empty()) {
                return;
            }
            DIR* processes = opendir("/proc");
            if (processes == nullptr) {
                return;
            }
            while (dirent* entry = readdir(processes)) {
                char* end = nullptr;
                unsigned long pid = std::strtoul(entry->d_name, &end, 10);
                if (pid == 0 || *end != 0 || !javaProcess((DWORD) pid)) {
                    continue;
                }
                std::error_code error;
                for (std::filesystem::directory_iterator it("/proc/" + std::to_string(pid) + "/fd", error), stop;
                     !error && it != stop; it.increment(error)) {
                    std::error_code broken;
                    std::string target = std::filesystem::read_symlink(it->path(), broken).string();
                    if (broken || !target.starts_with("socket:[")) {
                        continue;
                    }
                    auto found = sockets.find(std::strtoul(target.c_str() + 8, nullptr, 10));
                    if (found != sockets.end()) {
                        out.insert({(DWORD) pid, found->second});
                    }
                }
            }
            closedir(processes);
        }
#endif

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
#if defined(_WIN32)
        collect<MIB_TCPTABLE_OWNER_PID, MIB_TCPROW_OWNER_PID>(AF_INET, listening);
        collect<MIB_TCP6TABLE_OWNER_PID, MIB_TCP6ROW_OWNER_PID>(AF_INET6, listening);
#else
        listeners(listening);
#endif
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

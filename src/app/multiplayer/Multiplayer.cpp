#include "app/multiplayer/Multiplayer.h"
#include "app/accounts/Accounts.h"
#include "app/multiplayer/RoomApi.h"
#include "app/multiplayer/ZeroTier.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace Multiplayer {

    namespace blb = b_launcher_base;

    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr int GAME_PORT = 25565;
        constexpr std::chrono::milliseconds TICK(250);
        constexpr std::chrono::seconds KEEPALIVE_EVERY(60);
        constexpr std::chrono::seconds KEEPALIVE_RETRY(15);
        constexpr std::chrono::seconds REFRESH_EVERY(5);
        constexpr std::chrono::seconds SEARCH_EVERY(2);
        constexpr std::chrono::seconds EXISTS_EVERY(10);
        constexpr std::chrono::seconds ADDRESS_LIMIT(60);
        constexpr std::chrono::seconds REFUSED_LIMIT(20);
        constexpr std::chrono::seconds AUTHORIZE_PAUSE(2);
        constexpr std::chrono::seconds BIND_RETRY(5);
        constexpr std::chrono::seconds NAME_RETRY(15);
        constexpr std::chrono::seconds CLOSE_LIMIT(5);
        constexpr std::chrono::seconds SCAN_EVERY(2);
        constexpr std::chrono::seconds SCAN_LINGER(4);
        constexpr std::chrono::seconds SERVICE_EVERY(30);
        constexpr std::chrono::seconds SERVICE_RETRY(10);
        constexpr std::chrono::seconds WARM_RETRY(60);
        constexpr int AUTHORIZE_TRIES = 3;
        constexpr size_t NAME_LIMIT = 32;
        constexpr long long ONLINE_WINDOW = 300;

        struct Session {
            Role role = Role::None;
            std::string code;
            std::string network;
            std::string token;
            std::string device;
            std::string host;
            int port = GAME_PORT;
            std::string named;
            std::map<std::string, std::string> names;
            bool bound = false;
            bool joined = false;
            bool gone = false;
            bool listed = false;
            int removed = 0;
            Clock::time_point nextBind;
            Clock::time_point nextName;
        };

        struct Action {
            std::string device;
            bool ban = false;
        };

        struct Shared {
            Stage stage = Stage::Starting;
            std::string code;
            std::string error;
            std::string detail;
            std::string notice;
            std::string address;
            std::string hostName;
            bool published = false;
            std::vector<Member> members;
            int serial = 0;
            bool done = false;
            bool cancelled = false;
        };

        std::mutex lock;
        Shared shared;
        std::string wantedName;
        std::vector<Action> actions;
        std::vector<std::pair<bool, std::string>> toasts;
        std::thread worker;
        blb::cancellation_source cancel;

        State current;
        int seenSerial = -1;
        std::string lastWanted;
        std::atomic<int> forward{0};
        int pickedPort = 0;
        int typedPort = 0;

        std::mutex scanLock;
        std::condition_variable scanWake;
        std::thread scanner;
        blb::cancellation_source scanCancel;
        bool scanning = false;
        bool scanStop = false;
        Clock::time_point scanUntil;
        std::vector<LanWorlds::World> scanned;
        bool scannedFresh = false;
        std::vector<LanWorlds::World> shownWorlds;
        int shownSerial = 0;

        std::mutex serviceLock;
        std::thread serviceWorker;
        blb::cancellation_source serviceCancel;
        Service serviceFetched;
        bool serviceFresh = false;
        Service serviceShown;
        Clock::time_point serviceAt;

        std::thread warmer;
        blb::cancellation_source warmCancel;
        std::atomic<bool> warmDone{true};
        std::atomic<bool> warmOnline{false};
        Clock::time_point warmAt;

        std::mutex lookupLock;
        std::thread lookupWorker;
        blb::cancellation_source lookupCancel;
        std::atomic<bool> lookupDone{true};
        std::optional<Lookup> lookupLanded;
        int lookupLandedSerial = 0;
        int lookupSerial = 0;
        Lookup lookupShown;
        std::optional<std::string> lookupPending;

        template <typename Change>
        void update(Change change) {
            std::lock_guard<std::mutex> guard(lock);
            change(shared);
            shared.serial++;
        }

        void setStage(Stage stage) {
            update([stage](Shared& s) { s.stage = stage; });
        }

        std::string cleanName(const std::string& name) {
            std::string out;
            size_t points = 0;
            for (size_t i = 0; i < name.size();) {
                unsigned char lead = (unsigned char) name[i];
                size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
                length = std::min(length, name.size() - i);
                if (lead >= 0x20 && lead != 0x7F && points < NAME_LIMIT) {
                    out.append(name, i, length);
                    points++;
                }
                i += length;
            }
            size_t first = out.find_first_not_of(' ');
            size_t last = out.find_last_not_of(' ');
            return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
        }

        int rank(const Member& member) {
            return member.host ? 0 : member.self ? 1 : 2;
        }

        RoomApi::Reply refresh(Session& session, const blb::cancellation_token& token) {
            RoomApi::Room room;
            RoomApi::Reply reply = RoomApi::room(session.code, room, token);
            if (!reply.ok) {
                session.gone = session.gone || RoomApi::gone(reply);
                return reply;
            }
            long long now = (long long) std::time(nullptr);
            std::vector<Member> list;
            std::string hostAddress;
            std::string hostName;
            bool listed = false;
            for (const RoomApi::Member& item : room.members) {
                Member member;
                member.device = item.device;
                member.name = item.name;
                member.address = item.addresses.empty() ? std::string() : item.addresses.front();
                member.self = item.device == session.device;
                member.host = session.role == Role::Host ? member.self : item.host;
                member.online = member.self || (item.seen > 0 && now - item.seen < ONLINE_WINDOW);
                if (member.host && !member.self) {
                    hostAddress = member.address;
                    hostName = member.name;
                }
                listed = listed || member.self;
                session.names[item.device] = item.name;
                list.push_back(std::move(member));
            }
            if (session.role == Role::Guest) {
                if (std::find(room.banned.begin(), room.banned.end(), session.device) != room.banned.end()) {
                    session.removed = 2;
                } else if (listed) {
                    session.listed = true;
                } else if (session.listed) {
                    session.removed = 1;
                }
            }
            std::stable_sort(list.begin(), list.end(),
                             [](const Member& a, const Member& b) { return rank(a) < rank(b); });
            if (session.role == Role::Guest && session.host.empty() && !hostAddress.empty()) {
                session.host = hostAddress;
                session.port = room.port > 0 ? room.port : GAME_PORT;
            }
            update([&list, &hostName](Shared& s) {
                s.members = std::move(list);
                s.hostName = hostName;
            });
            return reply;
        }

        void syncName(Session& session, const blb::cancellation_token& token) {
            std::string wanted;
            {
                std::lock_guard<std::mutex> guard(lock);
                wanted = cleanName(wantedName);
            }
            Clock::time_point now = Clock::now();
            if (wanted.empty() || wanted == session.named || now < session.nextName) {
                return;
            }
            RoomApi::Reply reply = RoomApi::bindName(session.code, session.device, wanted, token);
            session.gone = session.gone || RoomApi::gone(reply);
            if (reply.ok || (reply.status >= 400 && reply.status < 500)) {
                session.named = wanted;
            } else {
                session.nextName = now + NAME_RETRY;
            }
        }

        void publishPort(Session& session, const blb::cancellation_token& token) {
            Clock::time_point now = Clock::now();
            if (session.bound || forward.load() <= 0 || now < session.nextBind) {
                return;
            }
            RoomApi::Reply reply = RoomApi::bindPort(session.code, session.token, GAME_PORT, session.device, token);
            session.gone = session.gone || RoomApi::gone(reply);
            if (!reply.ok) {
                session.nextBind = now + BIND_RETRY;
                return;
            }
            session.bound = true;
            update([](Shared& s) { s.published = true; });
        }

        RoomApi::Reply authorize(Session& session, const blb::cancellation_token& token) {
            RoomApi::Reply reply;
            for (int attempt = 0; attempt < AUTHORIZE_TRIES; attempt++) {
                reply = RoomApi::authorize(session.code, session.device, token);
                if (reply.ok || token.is_cancelled() || (reply.status != 0 && reply.status < 500)) {
                    break;
                }
                if (token.wait_for(AUTHORIZE_PAUSE)) {
                    break;
                }
            }
            session.gone = session.gone || RoomApi::gone(reply);
            return reply;
        }

        struct Failure {
            std::string title;
            std::string detail;
            bool closed = false;
        };

        Failure failure(std::string title, std::string detail) {
            return Failure{std::move(title), std::move(detail), false};
        }

        Failure closedByHost() {
            return Failure{"房主已关闭房间，已退出", std::string(), true};
        }

        Failure removedFromRoom(const Session& session) {
            return Failure{session.removed == 2 ? "你已被房主封禁" : "你已被房主移出房间", std::string(), true};
        }

        std::string nameOf(const Session& session, const std::string& device) {
            auto found = session.names.find(device);
            return found != session.names.end() && !found->second.empty() ? found->second : "设备 " + device;
        }

        bool runActions(Session& session, const blb::cancellation_token& token) {
            std::vector<Action> todo;
            {
                std::lock_guard<std::mutex> guard(lock);
                todo.swap(actions);
            }
            bool changed = false;
            for (const Action& action : todo) {
                RoomApi::Reply reply = action.ban ? RoomApi::ban(session.code, session.token, action.device, token)
                                                  : RoomApi::kick(session.code, session.token, action.device, token);
                session.gone = session.gone || RoomApi::gone(reply);
                std::string who = nameOf(session, action.device);
                std::string text;
                if (reply.ok) {
                    text = action.ban ? "已封禁 " + who : "已将 " + who + " 移出房间";
                    changed = true;
                } else {
                    text = (action.ban ? "封禁 " : "移出 ") + who + " 失败：" + RoomApi::describe(reply);
                }
                std::lock_guard<std::mutex> guard(lock);
                toasts.emplace_back(!reply.ok, text);
            }
            return changed;
        }

        bool roomExists(Session& session, const blb::cancellation_token& token) {
            bool exists = true;
            RoomApi::Reply reply = RoomApi::exists(session.code, exists, token);
            if ((reply.ok && !exists) || RoomApi::gone(reply)) {
                session.gone = true;
            }
            return !session.gone;
        }

        std::string technical(const RoomApi::Reply& reply) {
            if (reply.status == 0) {
                return reply.message.empty() ? std::string("网络请求没有完成") : "网络请求失败：" + reply.message;
            }
            std::string text = "HTTP " + std::to_string(reply.status);
            if (!reply.error.empty()) {
                text += "，错误 " + reply.error;
            }
            if (!reply.message.empty()) {
                text += "：" + reply.message;
            }
            return text;
        }

        Failure apiFailure(const std::string& action, const RoomApi::Reply& reply) {
            std::string advice;
            if (reply.status == 0) {
                advice = "请检查网络连接、代理或 VPN 设置后重试。";
            } else if (reply.status >= 500) {
                advice = "联机服务器暂时出了问题，稍后重试。";
            }
            return failure(RoomApi::describe(reply), action + "时出错。" + advice + "\n" + technical(reply));
        }

        std::string waitAddress(const Session& session, const blb::cancellation_token& token, Failure& error) {
            Clock::time_point deadline = Clock::now() + ADDRESS_LIMIT;
            std::optional<Clock::time_point> refusedSince;
            while (!token.wait_for(TICK)) {
                std::string own = ZeroTier::address(session.network);
                if (!own.empty()) {
                    return own;
                }
                Clock::time_point now = Clock::now();
                if (!ZeroTier::refused(session.network)) {
                    refusedSince.reset();
                } else if (!refusedSince) {
                    refusedSince = now;
                } else if (now - *refusedSince >= REFUSED_LIMIT) {
                    error = failure("虚拟网络没有接受这台设备",
                                    "已经给设备 " + session.device + " 授权，但虚拟网络一直拒绝它，状态：" +
                                            ZeroTier::networkStatus(session.network) +
                                            "。退出后重新进入房间通常能解决。\n" + ZeroTier::details());
                    return std::string();
                }
                if (now >= deadline) {
                    error = failure("一直没有分到虚拟地址",
                                    "加入虚拟网络 " + session.network + " 已经 " +
                                            std::to_string(ADDRESS_LIMIT.count()) + " 秒，还没有分到 IP 地址，状态：" +
                                            ZeroTier::networkStatus(session.network) + "。网络较差时稍后重试。\n" +
                                            ZeroTier::details());
                    return std::string();
                }
            }
            return std::string();
        }

        Failure enter(Session& session, const blb::cancellation_token& token) {
            RoomApi::Reply reply = authorize(session, token);
            if (!reply.ok) {
                return apiFailure("给这台设备授权", reply);
            }
            syncName(session, token);
            if (!ZeroTier::join(session.network)) {
                return failure("无法加入虚拟网络",
                               "ZeroTier 没有接受加入网络 " + session.network + " 的请求。\n" + ZeroTier::details());
            }
            session.joined = true;
            setStage(Stage::Linking);
            Failure error;
            waitAddress(session, token, error);
            return error;
        }

        Failure boot(Session& session, const blb::cancellation_token& token) {
            setStage(Stage::Starting);
            ZeroTier::Start started = ZeroTier::start(token);
            if (started == ZeroTier::Start::Online) {
                session.device = blb::to_lower(ZeroTier::device());
                return Failure();
            }
            if (started == ZeroTier::Start::Cancelled) {
                return failure("已取消", std::string());
            }
            if (started == ZeroTier::Start::Failed) {
                return failure("无法启动 ZeroTier",
                               "ZeroTier 组件没能启动，重启启动器后再试一次。\n" + ZeroTier::details());
            }
            RoomApi::Service ignored;
            RoomApi::Reply probe = RoomApi::service(ignored, token);
            std::string why = probe.ok ? "联机服务器可以访问，说明网络本身是通的，多半是 UDP 连接被防火墙或所在网络"
                                         "（校园网、公司网络常见）拦截了。可以在 Windows 防火墙里允许 b-launcher "
                                         "通信，或者换一个网络后重试。"
                                       : "联机服务器也连不上，请先检查网络连接、代理或 VPN 设置。";
            return failure("ZeroTier 连不上网络",
                           "等了 " + std::to_string(ZeroTier::ONLINE_SECONDS) + " 秒仍没有连上 ZeroTier 的根服务器。" +
                                   why + "\n" + ZeroTier::details());
        }

        Failure hostRoom(Session& session, const blb::cancellation_token& token) {
            if (Failure problem = boot(session, token); !problem.title.empty()) {
                return problem;
            }
            setStage(Stage::Opening);
            RoomApi::Created created;
            RoomApi::Reply reply = RoomApi::create(created, token);
            if (!reply.ok) {
                return apiFailure("创建房间", reply);
            }
            session.code = created.code;
            session.network = created.network;
            session.token = created.token;
            update([&created](Shared& s) { s.code = created.code; });
            if (Failure problem = enter(session, token); !problem.title.empty()) {
                return problem;
            }
            if (!ZeroTier::serve(GAME_PORT, [] { return forward.load(); })) {
                return failure("无法在虚拟网络上开放端口",
                               "在虚拟网络上监听 " + std::to_string(GAME_PORT) + " 端口失败。\n" + ZeroTier::details());
            }
            setStage(Stage::Ready);
            publishPort(session, token);
            refresh(session, token);
            Clock::time_point nextKeepalive = Clock::now() + KEEPALIVE_EVERY;
            Clock::time_point nextRefresh = Clock::now() + REFRESH_EVERY;
            while (!token.wait_for(TICK)) {
                bool wasBound = session.bound;
                publishPort(session, token);
                syncName(session, token);
                if (runActions(session, token)) {
                    nextRefresh = Clock::time_point();
                }
                Clock::time_point now = Clock::now();
                if (now >= nextKeepalive) {
                    reply = RoomApi::keepalive(session.code, session.token, token);
                    session.gone = session.gone || RoomApi::gone(reply);
                    nextKeepalive = now + (reply.ok ? KEEPALIVE_EVERY : KEEPALIVE_RETRY);
                }
                if (now >= nextRefresh || session.bound != wasBound) {
                    refresh(session, token);
                    nextRefresh = now + REFRESH_EVERY;
                }
                if (session.gone) {
                    return failure("房间已被关闭",
                                   "联机服务器上已经没有这个房间：可能超过 5 分钟没有续期，"
                                   "或者同一个公网 IP 下有人新建了房间（同一网络只能有一个房间）。");
                }
            }
            return Failure();
        }

        Failure joinRoom(Session& session, const blb::cancellation_token& token) {
            if (Failure problem = boot(session, token); !problem.title.empty()) {
                return problem;
            }
            setStage(Stage::Opening);
            RoomApi::Reply reply = RoomApi::join(session.code, session.network, token);
            if (!reply.ok) {
                return apiFailure("查找房间 " + session.code, reply);
            }
            update([&session](Shared& s) { s.code = session.code; });
            if (Failure problem = enter(session, token); !problem.title.empty()) {
                return problem;
            }
            setStage(Stage::Searching);
            while (session.host.empty()) {
                refresh(session, token);
                syncName(session, token);
                if (session.gone) {
                    return closedByHost();
                }
                if (session.removed != 0) {
                    return removedFromRoom(session);
                }
                if (!session.host.empty() || token.wait_for(SEARCH_EVERY)) {
                    break;
                }
            }
            if (token.is_cancelled()) {
                return Failure();
            }
            int local = ZeroTier::expose(session.host, session.port, GAME_PORT);
            if (local == 0) {
                return failure("无法在本机开放端口",
                               "本机 127.0.0.1 上的 " + std::to_string(GAME_PORT) +
                                       " 端口和随机端口都无法监听，可能被安全软件拦截了。");
            }
            std::string address = local == GAME_PORT ? "127.0.0.1" : "127.0.0.1:" + std::to_string(local);
            update([&address](Shared& s) { s.address = address; });
            LanWorlds::announce("联机房间 " + session.code, local);
            setStage(Stage::Ready);
            Clock::time_point nextRefresh = Clock::now() + REFRESH_EVERY;
            Clock::time_point nextExists = Clock::now() + EXISTS_EVERY;
            while (!token.wait_for(TICK)) {
                syncName(session, token);
                Clock::time_point now = Clock::now();
                if (now >= nextExists) {
                    roomExists(session, token);
                    nextExists = now + EXISTS_EVERY;
                }
                if (!session.gone && now >= nextRefresh) {
                    refresh(session, token);
                    nextRefresh = now + REFRESH_EVERY;
                }
                if (session.gone) {
                    return closedByHost();
                }
                if (session.removed != 0) {
                    return removedFromRoom(session);
                }
            }
            return Failure();
        }

        void run(Session session, blb::cancellation_token token) {
            Failure error = session.role == Role::Host ? hostRoom(session, token) : joinRoom(session, token);
            ZeroTier::halt();
            LanWorlds::silence();
            if (session.role == Role::Host && !session.token.empty() && !session.gone) {
                RoomApi::close(session.code, session.token, CLOSE_LIMIT);
            }
            if (session.joined) {
                ZeroTier::leave(session.network);
            }
            bool cancelled = token.is_cancelled();
            bool ended = !cancelled && error.closed;
            update([&error, cancelled, ended](Shared& s) {
                s.done = true;
                s.cancelled = cancelled || ended;
                s.notice = ended ? error.title : std::string();
                if (!cancelled && !ended) {
                    s.stage = Stage::Failed;
                    s.error = error.title.empty() ? std::string("联机已断开") : error.title;
                    s.detail = error.title.empty() ? std::string("联机意外结束了，可以重试一次。") : error.detail;
                }
            });
        }

        void wantName();

        void begin(Role role, const std::string& code) {
            if (current.role != Role::None || worker.joinable()) {
                return;
            }
            cancel = blb::cancellation_source();
            {
                std::lock_guard<std::mutex> guard(lock);
                shared = Shared();
                shared.code = code;
                actions.clear();
            }
            current = State();
            current.role = role;
            current.code = code;
            seenSerial = -1;
            lastWanted.clear();
            wantName();
            Session session;
            session.role = role;
            session.code = code;
            worker = std::thread(run, std::move(session), cancel.token());
        }

        void scanLoop() {
            std::unique_lock<std::mutex> guard(scanLock);
            blb::cancellation_token token = scanCancel.token();
            while (!scanStop && Clock::now() < scanUntil) {
                guard.unlock();
                std::vector<LanWorlds::World> found = LanWorlds::scan(token);
                guard.lock();
                scanned = std::move(found);
                scannedFresh = true;
                scanWake.wait_for(guard, SCAN_EVERY, [] { return scanStop; });
            }
            scanning = false;
        }

        int chooseTarget() {
            if (typedPort > 0) {
                return typedPort;
            }
            for (const LanWorlds::World& world : shownWorlds) {
                if (world.port == pickedPort) {
                    return pickedPort;
                }
            }
            if (pickedPort > 0) {
                return 0;
            }
            return shownWorlds.empty() ? 0 : shownWorlds.front().port;
        }

        void wantName() {
            const blb::auth::account* account = Accounts::current();
            std::string name = account != nullptr ? account->name : std::string();
            if (name == lastWanted) {
                return;
            }
            lastWanted = name;
            std::lock_guard<std::mutex> guard(lock);
            wantedName = name;
        }

        void startLookup() {
            if (lookupWorker.joinable() || !lookupPending) {
                return;
            }
            std::string code = std::move(*lookupPending);
            lookupPending.reset();
            lookupCancel = blb::cancellation_source();
            int serial = lookupSerial;
            lookupDone = false;
            lookupWorker = std::thread([code, serial, token = lookupCancel.token()] {
                bool exists = false;
                RoomApi::Reply reply = RoomApi::exists(code, exists, token);
                Lookup out;
                out.code = code;
                out.done = true;
                out.exists = exists;
                if (!reply.ok && !RoomApi::gone(reply)) {
                    out.error = RoomApi::describe(reply);
                }
                {
                    std::lock_guard<std::mutex> guard(lookupLock);
                    lookupLanded = std::move(out);
                    lookupLandedSerial = serial;
                }
                lookupDone = true;
            });
        }

        void landLookup() {
            {
                std::lock_guard<std::mutex> guard(lookupLock);
                if (lookupLanded && lookupLandedSerial == lookupSerial) {
                    lookupShown = std::move(*lookupLanded);
                }
                lookupLanded.reset();
            }
            if (lookupWorker.joinable() && lookupDone.load()) {
                lookupWorker.join();
            }
            startLookup();
        }

        void landService() {
            std::lock_guard<std::mutex> guard(serviceLock);
            if (serviceFresh) {
                serviceShown = std::move(serviceFetched);
                serviceFresh = false;
                serviceAt = Clock::now();
            }
        }
    }

    bool available() {
        return ZeroTier::available();
    }

    void warmUp() {
        if (!ZeroTier::available() || !warmDone.load()) {
            return;
        }
        if (warmer.joinable()) {
            warmer.join();
        }
        if (warmOnline.load() || (warmAt != Clock::time_point() && Clock::now() - warmAt < WARM_RETRY)) {
            return;
        }
        warmAt = Clock::now();
        warmDone = false;
        warmer = std::thread([token = warmCancel.token()] {
            warmOnline = ZeroTier::start(token) == ZeroTier::Start::Online;
            warmDone = true;
        });
    }

    const State& state() {
        return current;
    }

    bool active() {
        return current.role != Role::None && current.stage != Stage::Failed;
    }

    std::string stageText() {
        bool hosting = current.role == Role::Host;
        switch (current.stage) {
            case Stage::Starting: return "正在启动 ZeroTier";
            case Stage::Opening: return hosting ? "正在创建房间" : "正在查找房间";
            case Stage::Linking: return "正在加入虚拟网络";
            case Stage::Searching: return "等待房主开放世界";
            case Stage::Ready:
                if (hosting) {
                    return current.published ? "房间已开放" : "等待你在游戏里对局域网开放";
                }
                return current.hostName.empty() ? std::string("已连接到房主")
                                                : "已连接到 " + current.hostName + " 的房间";
            case Stage::Closing: return hosting ? "正在关闭房间" : "正在退出房间";
            case Stage::Failed: return current.error;
        }
        return std::string();
    }

    const Service& service() {
        return serviceShown;
    }

    void watchService() {
        if (serviceShown.checking) {
            return;
        }
        bool fine = serviceShown.reachable && !serviceShown.maintenance;
        if (serviceShown.known && Clock::now() - serviceAt < (fine ? SERVICE_EVERY : SERVICE_RETRY)) {
            return;
        }
        if (serviceWorker.joinable()) {
            serviceWorker.join();
        }
        serviceShown.checking = true;
        serviceWorker = std::thread([token = serviceCancel.token()] {
            RoomApi::Service status;
            RoomApi::Reply reply = RoomApi::service(status, token);
            if (reply.ok) {
                ZeroTier::orbit(status.moons);
            }
            Service out;
            out.known = true;
            out.reachable = reply.ok;
            out.maintenance = status.maintenance;
            out.message = status.message;
            out.available = status.available;
            out.total = status.total;
            std::lock_guard<std::mutex> guard(serviceLock);
            serviceFetched = std::move(out);
            serviceFresh = true;
        });
    }

    bool canHost() {
        const Service& s = serviceShown;
        return !s.known || !s.reachable || (!s.maintenance && s.available > 0);
    }

    const Lookup& lookup() {
        return lookupShown;
    }

    void check(const std::string& code) {
        if (!RoomApi::validCode(code)) {
            return;
        }
        lookupCancel.cancel();
        lookupSerial++;
        lookupShown = Lookup();
        lookupShown.code = code;
        lookupShown.checking = true;
        lookupPending = code;
        landLookup();
    }

    void forgetLookup() {
        lookupCancel.cancel();
        lookupSerial++;
        lookupPending.reset();
        lookupShown = Lookup();
    }

    const std::vector<LanWorlds::World>& worlds() {
        return shownWorlds;
    }

    int worldsSerial() {
        return shownSerial;
    }

    void watchWorlds() {
        std::unique_lock<std::mutex> guard(scanLock);
        scanUntil = Clock::now() + SCAN_LINGER;
        if (scanning || scanStop) {
            return;
        }
        guard.unlock();
        if (scanner.joinable()) {
            scanner.join();
        }
        guard.lock();
        scanning = true;
        scanner = std::thread(scanLoop);
    }

    int picked() {
        return pickedPort;
    }

    void pick(int port) {
        pickedPort = port;
    }

    int typed() {
        return typedPort;
    }

    void type(int port) {
        typedPort = port > 0 && port <= 65535 ? port : 0;
    }

    void host() {
        if (canHost()) {
            begin(Role::Host, std::string());
        }
    }

    void join(const std::string& code) {
        if (RoomApi::validCode(code)) {
            begin(Role::Guest, code);
        }
    }

    bool canRetry() {
        return current.role != Role::None && current.stage == Stage::Failed && !worker.joinable();
    }

    void retry() {
        if (!canRetry()) {
            return;
        }
        Role role = current.role;
        std::string code = current.code;
        current = State();
        if (role == Role::Host) {
            host();
        } else {
            join(code);
        }
    }

    void kick(const std::string& device, bool ban) {
        if (current.role != Role::Host || current.stage != Stage::Ready || device.empty()) {
            return;
        }
        std::lock_guard<std::mutex> guard(lock);
        actions.push_back(Action{device, ban});
    }

    void leave() {
        if (current.role == Role::None) {
            return;
        }
        if (current.stage == Stage::Failed) {
            dismiss();
            return;
        }
        cancel.cancel();
        current.stage = Stage::Closing;
    }

    void dismiss() {
        if (current.stage == Stage::Failed && !worker.joinable()) {
            current = State();
        }
    }

    void poll() {
        landService();
        landLookup();
        if (current.role != Role::None && current.stage != Stage::Failed) {
            wantName();
        }
        bool finished = false;
        bool cancelled = false;
        bool published = false;
        std::string notice;
        if (current.role != Role::None) {
            std::lock_guard<std::mutex> guard(lock);
            if (shared.serial != seenSerial) {
                seenSerial = shared.serial;
                current.code = shared.code;
                current.error = shared.error;
                current.detail = shared.detail;
                current.address = shared.address;
                current.hostName = shared.hostName;
                current.members = shared.members;
            }
            current.stage = cancel.is_cancelled() && !shared.done ? Stage::Closing : shared.stage;
            finished = shared.done;
            cancelled = shared.cancelled;
            published = shared.published;
            notice = shared.notice;
        }
        std::vector<std::pair<bool, std::string>> messages;
        {
            std::lock_guard<std::mutex> guard(lock);
            messages.swap(toasts);
        }
        for (const auto& [failed, text] : messages) {
            Notices::add(failed ? Notices::Kind::Failed : Notices::Kind::Done, text, 3000);
        }
        if (finished && worker.joinable()) {
            worker.join();
            if (cancelled) {
                current = State();
                if (!notice.empty()) {
                    Notices::add(Notices::Kind::Info, notice, 5000);
                }
            } else {
                Notices::add(Notices::Kind::Failed, "联机已断开：" + current.error, 5000);
            }
        }
        current.links = ZeroTier::links();
        if (current.role == Role::Host && active()) {
            watchWorlds();
        }
        {
            std::lock_guard<std::mutex> guard(scanLock);
            if (scannedFresh) {
                shownWorlds = std::move(scanned);
                scannedFresh = false;
                shownSerial++;
            }
        }
        current.target = current.role == Role::Host ? chooseTarget() : 0;
        current.published = published && current.target > 0;
        forward = current.target;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> guard(scanLock);
            scanStop = true;
        }
        scanCancel.cancel();
        serviceCancel.cancel();
        scanWake.notify_all();
        cancel.cancel();
        if (worker.joinable()) {
            worker.join();
        }
        if (scanner.joinable()) {
            scanner.join();
        }
        if (serviceWorker.joinable()) {
            serviceWorker.join();
        }
        lookupCancel.cancel();
        lookupPending.reset();
        if (lookupWorker.joinable()) {
            lookupWorker.join();
        }
        warmCancel.cancel();
        if (warmer.joinable()) {
            warmer.join();
        }
        LanWorlds::silence();
        ZeroTier::stop();
    }
}

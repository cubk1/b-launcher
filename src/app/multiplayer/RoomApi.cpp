#include "app/multiplayer/RoomApi.h"
#include "app/system/Platform.h"

#include <core/network/http_client.hpp>

#include <core/base/json.hpp>

#include <cstdio>
#include <utility>

namespace RoomApi {

    namespace blb = b_launcher_base;

    namespace {
        const char* const BASE = "https://b-api.cubk.cn/api/v1";
        constexpr std::chrono::seconds TIMEOUT(15);

        long long number(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            if (it == doc.end()) {
                return 0;
            }
            if (it->is_number_integer()) {
                return it->get<long long>();
            }
            if (!it->is_string()) {
                return 0;
            }
            long long value = 0;
            for (char c : it->get<std::string>()) {
                if (c < '0' || c > '9' || value > 1000000000) {
                    return 0;
                }
                value = value * 10 + (c - '0');
            }
            return value;
        }

        bool flag(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_boolean() && it->get<bool>();
        }

        long long parseTime(const std::string& value) {
            int year = 0;
            int month = 0;
            int day = 0;
            int hour = 0;
            int minute = 0;
            int second = 0;
            if (std::sscanf(value.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
                return 0;
            }
            std::chrono::year_month_day date{std::chrono::year(year), std::chrono::month((unsigned) month),
                                             std::chrono::day((unsigned) day)};
            if (!date.ok()) {
                return 0;
            }
            long long days = std::chrono::sys_days(date).time_since_epoch().count();
            return days * 86400 + hour * 3600 + minute * 60 + second;
        }

        Reply send(const std::string& method, const std::string& path, const std::string& body,
                   const std::string& bearer, nlohmann::json& doc, const blb::cancellation_token& token,
                   std::chrono::milliseconds timeout = TIMEOUT) {
            blb::net::http_request request;
            request.method = method;
            request.url = std::string(BASE) + path;
            request.headers.push_back({"User-Agent", Platform::USER_AGENT});
            request.headers.push_back({"Accept", "application/json"});
            if (!body.empty()) {
                request.headers.push_back({"Content-Type", "application/json"});
                request.body = body;
            }
            if (!bearer.empty()) {
                request.headers.push_back({"Authorization", "Bearer " + bearer});
            }
            request.timeout = timeout;
            Reply reply;
            auto response = blb::net::http_send(request, token);
            if (!response) {
                reply.message = response.err().message;
                return reply;
            }
            reply.status = response->status;
            doc = nlohmann::json::parse(response->body, nullptr, false);
            if (!response->ok()) {
                if (doc.is_object()) {
                    reply.error = blb::json_text(doc, "error");
                    reply.message = blb::json_text(doc, "message");
                }
                return reply;
            }
            if (!doc.is_object()) {
                reply.error = "invalid_response";
                return reply;
            }
            reply.ok = true;
            return reply;
        }

        std::string lower(std::string value) {
            for (char& c : value) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return value;
        }

        std::string roomPath(const std::string& code, const char* tail) {
            return "/room/" + code + tail;
        }
    }

    bool validCode(const std::string& code) {
        if (code.size() != 6) {
            return false;
        }
        for (char c : code) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }

    bool gone(const Reply& reply) {
        return reply.status == 404 && reply.error == "room_not_found";
    }

    std::string describe(const Reply& reply) {
        if (reply.status == 0) {
            return "无法连接联机服务器，请检查网络";
        }
        if (reply.error == "room_not_found") {
            return "房间不存在或已关闭";
        }
        if (reply.error == "room_full") {
            return "房间已满，最多 10 台设备";
        }
        if (reply.error == "no_free_network") {
            return "联机服务器暂时没有空闲网络，请稍后再试";
        }
        if (reply.error == "upstream_error") {
            return "联机服务暂时出错，请稍后再试";
        }
        if (reply.error == "invalid_token" || reply.error == "token_required") {
            return "房间凭证无效";
        }
        if (reply.error == "invalid_device_id") {
            return "设备 ID 无效";
        }
        if (reply.error == "device_banned") {
            return "你已被房主封禁，不能加入这个房间";
        }
        if (reply.error == "device_not_found") {
            return "这台设备不在房间里";
        }
        if (reply.error == "invalid_port") {
            return "端口无效";
        }
        if (reply.error == "invalid_username") {
            return "用户名无效";
        }
        if (reply.error == "invalid_response") {
            return "联机服务器的响应无法识别";
        }
        if (!reply.message.empty()) {
            return reply.message;
        }
        return "联机服务出错（HTTP " + std::to_string(reply.status) + "）";
    }

    Reply service(Service& out, const blb::cancellation_token& token) {
        nlohmann::json doc;
        Reply reply = send("GET", "/server", std::string(), std::string(), doc, token);
        if (!reply.ok) {
            return reply;
        }
        out.maintenance = flag(doc, "maintenance");
        out.message = out.maintenance ? blb::json_text(doc, "message") : std::string();
        out.available = (int) number(doc, "available");
        out.total = (int) number(doc, "total");
        auto moons = doc.find("moons");
        if (moons != doc.end() && moons->is_array()) {
            for (const nlohmann::json& moon : *moons) {
                if (moon.is_object()) {
                    out.moons.push_back({blb::json_text(moon, "id"), blb::json_text(moon, "seed")});
                }
            }
        }
        return reply;
    }

    Reply create(Created& out, const blb::cancellation_token& token) {
        nlohmann::json doc;
        Reply reply = send("POST", "/room/create", std::string(), std::string(), doc, token);
        if (!reply.ok) {
            return reply;
        }
        out.code = blb::json_text(doc, "code");
        out.network = blb::json_text(doc, "networkId");
        out.token = blb::json_text(doc, "roomToken");
        if (!validCode(out.code) || out.network.size() != 16 || out.token.empty()) {
            reply.ok = false;
            reply.error = "invalid_response";
        }
        return reply;
    }

    Reply exists(const std::string& code, bool& out, const blb::cancellation_token& token) {
        nlohmann::json doc;
        Reply reply = send("GET", roomPath(code, "/exists"), std::string(), std::string(), doc, token);
        out = reply.ok && flag(doc, "exists");
        return reply;
    }

    Reply join(const std::string& code, std::string& network, const blb::cancellation_token& token) {
        nlohmann::json doc;
        Reply reply = send("GET", roomPath(code, "/join"), std::string(), std::string(), doc, token);
        if (!reply.ok) {
            return reply;
        }
        network = blb::json_text(doc, "networkId");
        if (network.size() != 16) {
            reply.ok = false;
            reply.error = "invalid_response";
        }
        return reply;
    }

    Reply authorize(const std::string& code, const std::string& device, const blb::cancellation_token& token) {
        nlohmann::json doc;
        nlohmann::json body = {{"deviceId", device}};
        return send("POST", roomPath(code, "/auth"), body.dump(), std::string(), doc, token);
    }

    Reply bindPort(const std::string& code, const std::string& roomToken, int port, const std::string& device,
                   const blb::cancellation_token& token) {
        nlohmann::json doc;
        nlohmann::json body = {{"port", port}, {"deviceId", device}};
        return send("POST", roomPath(code, "/port/bind"), body.dump(), roomToken, doc, token);
    }

    Reply bindName(const std::string& code, const std::string& device, const std::string& name,
                   const blb::cancellation_token& token) {
        nlohmann::json doc;
        nlohmann::json body = {{"deviceId", device}, {"username", name}};
        return send("POST", roomPath(code, "/username/bind"),
                    body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), std::string(), doc, token);
    }

    Reply kick(const std::string& code, const std::string& roomToken, const std::string& device,
               const blb::cancellation_token& token) {
        nlohmann::json doc;
        nlohmann::json body = {{"deviceId", device}};
        return send("POST", roomPath(code, "/kick"), body.dump(), roomToken, doc, token);
    }

    Reply ban(const std::string& code, const std::string& roomToken, const std::string& device,
              const blb::cancellation_token& token) {
        nlohmann::json doc;
        nlohmann::json body = {{"deviceId", device}};
        return send("POST", roomPath(code, "/ban"), body.dump(), roomToken, doc, token);
    }

    Reply keepalive(const std::string& code, const std::string& roomToken, const blb::cancellation_token& token) {
        nlohmann::json doc;
        return send("POST", roomPath(code, "/keepalive"), std::string(), roomToken, doc, token);
    }

    Reply close(const std::string& code, const std::string& roomToken, std::chrono::milliseconds timeout) {
        nlohmann::json doc;
        return send("POST", roomPath(code, "/close"), std::string(), roomToken, doc, blb::cancellation_token(),
                    timeout);
    }

    Reply room(const std::string& code, Room& out, const blb::cancellation_token& token) {
        nlohmann::json doc;
        Reply reply = send("GET", roomPath(code, ""), std::string(), std::string(), doc, token);
        if (!reply.ok) {
            return reply;
        }
        out = Room();
        long long port = number(doc, "port");
        out.port = port > 0 && port <= 65535 ? (int) port : 0;
        std::string hostDevice = lower(blb::json_text(doc, "hostDeviceId"));
        auto banned = doc.find("bannedDeviceIds");
        if (banned != doc.end() && banned->is_array()) {
            for (const nlohmann::json& device : *banned) {
                if (device.is_string()) {
                    out.banned.push_back(lower(device.get<std::string>()));
                }
            }
        }
        auto list = doc.find("members");
        if (list == doc.end() || !list->is_array()) {
            return reply;
        }
        for (const nlohmann::json& item : *list) {
            if (!item.is_object()) {
                continue;
            }
            Member member;
            member.device = lower(blb::json_text(item, "deviceId"));
            member.name = blb::json_text(item, "username");
            member.authorized = blb::json_text(item, "status") == "authorized";
            member.host = flag(item, "host") || (!hostDevice.empty() && member.device == hostDevice);
            member.seen = parseTime(blb::json_text(item, "lastSeen"));
            auto addresses = item.find("ipAssignments");
            if (addresses != item.end() && addresses->is_array()) {
                for (const nlohmann::json& address : *addresses) {
                    if (address.is_string()) {
                        member.addresses.push_back(address.get<std::string>());
                    }
                }
            }
            if (!member.device.empty()) {
                out.members.push_back(std::move(member));
            }
        }
        return reply;
    }
}

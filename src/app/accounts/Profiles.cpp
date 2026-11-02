#include "app/accounts/Profiles.h"
#include "app/accounts/Accounts.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/fs.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>

namespace Profiles {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;
        using json = nlohmann::json;

        const char* const BASE = "https://api.minecraftservices.com/minecraft/profile";
        const char* const JOIN = "https://sessionserver.mojang.com/session/minecraft/join";
        const char* const OPTIFINE_CAPES = "http://s.optifine.net/capes/";
        const char* const OPTIFINE_EDITOR = "https://optifine.net/capeChange";
        constexpr int CAPE_SCALE = 8;
        constexpr int RENAME_DAYS = 30;

        AsyncTasks tasks;
        std::map<std::string, Profile> profiles;
        std::map<std::string, uint64_t> loadVersions;
        std::map<std::string, NameCheck> checks;
        const NameCheck noCheck;

        struct Image {
            bool requested = false;
            std::shared_ptr<const Pixels> pixels;
            bool failed = false;
        };
        std::map<std::string, Image> images;

        struct Session {
            blb::auth::account account;
            std::shared_ptr<blb::auth::microsoft_authenticator> auth;
            bool renewed = false;
            bool expired = false;
        };

        std::optional<Session> sessionOf(const std::string& key) {
            const blb::auth::account* account = Accounts::find(key);
            if (account == nullptr || account->type != blb::auth::account_type::microsoft) {
                return std::nullopt;
            }
            return Session{*account, Accounts::authenticator(*account)};
        }

        std::optional<blb::error> renew(Session& session, blb::cancellation_token token) {
            if (session.auth == nullptr) {
                session.expired = true;
                return blb::make_error(blb::errc::auth_expired, "登录已失效");
            }
            auto fresh = session.auth->refresh(session.account, token);
            if (!fresh) {
                session.expired = fresh.err().code == blb::errc::auth_expired;
                return fresh.err();
            }
            session.account = *fresh;
            session.renewed = true;
            return std::nullopt;
        }

        blb::result<blb::net::http_response> call(Session& session, const std::string& method, const std::string& path,
                                                  const std::string& body, const std::string& type,
                                                  blb::cancellation_token token) {
            if (!session.renewed && session.account.needs_refresh()) {
                if (std::optional<blb::error> failed = renew(session, token)) {
                    return *failed;
                }
            }
            for (;;) {
                blb::net::http_request request;
                request.method = method;
                request.url = std::string(BASE) + path;
                request.headers.push_back({"Authorization", "Bearer " + session.account.access_token});
                request.headers.push_back({"Accept", "application/json"});
                if (!type.empty()) {
                    request.headers.push_back({"Content-Type", type});
                }
                request.body = body;
                auto response = blb::net::http_send(request, token);
                if (!response) {
                    return response.err();
                }
                if (response->status == 401 && !session.renewed) {
                    if (std::optional<blb::error> failed = renew(session, token)) {
                        return *failed;
                    }
                    continue;
                }
                if (response->status == 401) {
                    session.expired = true;
                }
                return response;
            }
        }

        std::string problemOf(const blb::net::http_response& response, const std::string& invalid,
                              const std::string& forbidden) {
            json doc = json::parse(response.body, nullptr, false);
            std::string detail;
            if (doc.is_object()) {
                if (auto details = doc.find("details"); details != doc.end() && details->is_object()) {
                    detail = blb::json_text(*details, "status");
                }
                if (detail.empty()) {
                    detail = blb::json_text(doc, "errorMessage");
                }
            }
            if (detail == "DUPLICATE") {
                return "这个用户名已经有人用了";
            }
            if (detail == "NOT_ALLOWED") {
                return "这个用户名不能使用";
            }
            switch (response.status) {
                case 400:
                    if (!invalid.empty()) {
                        return invalid;
                    }
                    break;
                case 401: return "登录已失效，请重新登录";
                case 403:
                    if (!forbidden.empty()) {
                        return forbidden;
                    }
                    break;
                case 404: return "这个账户没有 Minecraft";
                case 429: return "操作太频繁，请稍后再试";
                default: break;
            }
            return "服务器返回 HTTP " + std::to_string(response.status) + (detail.empty() ? "" : "（" + detail + "）");
        }

        std::string dayAfter(const std::string& stamp, int days) {
            int year = 0;
            int month = 0;
            int day = 0;
            if (std::sscanf(stamp.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
                return std::string();
            }
            std::chrono::year_month_day date{std::chrono::year(year), std::chrono::month((unsigned) month),
                                             std::chrono::day((unsigned) day)};
            if (!date.ok()) {
                return std::string();
            }
            std::chrono::year_month_day later{std::chrono::sys_days(date) + std::chrono::days(days)};
            char text[16];
            std::snprintf(text, sizeof(text), "%04d-%02u-%02u", (int) later.year(), (unsigned) later.month(),
                          (unsigned) later.day());
            return text;
        }

        std::string today() {
            std::chrono::year_month_day now{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
            char text[16];
            std::snprintf(text, sizeof(text), "%04d-%02u-%02u", (int) now.year(), (unsigned) now.month(),
                          (unsigned) now.day());
            return text;
        }

        bool parse(const std::string& body, Profile& out) {
            json doc = json::parse(body, nullptr, false);
            if (!doc.is_object() || blb::json_text(doc, "name").empty()) {
                return false;
            }
            out.name = blb::json_text(doc, "name");
            out.skinUrl.clear();
            out.slim = false;
            if (auto skins = doc.find("skins"); skins != doc.end() && skins->is_array()) {
                for (const json& skin : *skins) {
                    if (skin.is_object() && blb::json_text(skin, "state") == "ACTIVE") {
                        out.skinUrl = blb::json_text(skin, "url");
                        out.slim = blb::to_lower(blb::json_text(skin, "variant")) == "slim";
                    }
                }
            }
            out.capes.clear();
            if (auto capes = doc.find("capes"); capes != doc.end() && capes->is_array()) {
                for (const json& cape : *capes) {
                    if (cape.is_object() && !blb::json_text(cape, "id").empty()) {
                        out.capes.push_back(Cape{blb::json_text(cape, "id"), blb::json_text(cape, "alias"),
                                                 blb::json_text(cape, "url"),
                                                 blb::json_text(cape, "state") == "ACTIVE"});
                    }
                }
            }
            return true;
        }

        void settle(const std::string& key, const Session& session, const Profile* profile) {
            const blb::auth::account* account = Accounts::find(key);
            if (account == nullptr) {
                return;
            }
            if (session.expired) {
                Accounts::markExpired(key);
            }
            blb::auth::account updated = session.renewed ? session.account : *account;
            if (profile != nullptr) {
                updated.name = profile->name;
                updated.skin_url = profile->skinUrl;
            }
            if (session.renewed || updated.name != account->name || updated.skin_url != account->skin_url) {
                Accounts::refreshed(updated);
            }
        }

        void lookOptiFine(const std::string& key, const std::string& name);

        bool currentLoad(const std::string& key, uint64_t version) {
            auto found = loadVersions.find(key);
            return found != loadVersions.end() && found->second == version && Accounts::find(key) != nullptr;
        }

        void load(const std::string& key) {
            uint64_t version = ++loadVersions[key];
            Profile& entry = profiles[key];
            std::optional<Session> found = sessionOf(key);
            if (!found) {
                entry.status = Status::Failed;
                entry.error = "只有微软账户可以管理档案";
                return;
            }
            entry.status = Status::Loading;
            entry.error.clear();
            tasks.run(
                    [key, version, session = *found](blb::cancellation_token token) mutable -> Continuation {
                        Profile out;
                        std::string problem;
                        auto response = call(session, "GET", std::string(), std::string(), std::string(), token);
                        if (!response) {
                            problem = Accounts::describe(response.err());
                        } else if (!response->ok()) {
                            problem = problemOf(*response, std::string(), std::string());
                        } else if (!parse(response->body, out)) {
                            problem = "档案内容无效";
                        }
                        if (problem.empty()) {
                            auto change = call(session, "GET", "/namechange", std::string(), std::string(), token);
                            json doc = change && change->ok() ? json::parse(change->body, nullptr, false) : json();
                            if (doc.is_object()) {
                                auto allowed = doc.find("nameChangeAllowed");
                                out.renameAllowed =
                                        allowed == doc.end() || !allowed->is_boolean() || allowed->get<bool>();
                                if (!out.renameAllowed) {
                                    out.renameAfter = dayAfter(blb::json_text(doc, "changedAt"), RENAME_DAYS);
                                }
                            }
                        }
                        return [key, version, session, out, problem] {
                            if (!currentLoad(key, version)) return;
                            Profile& entry = profiles[key];
                            if (!problem.empty()) {
                                entry.status = Status::Failed;
                                entry.error = problem;
                                settle(key, session, nullptr);
                                return;
                            }
                            std::string working = entry.working;
                            entry = out;
                            entry.working = working;
                            entry.status = Status::Ready;
                            settle(key, session, &entry);
                            lookOptiFine(key, entry.name);
                        };
                    },
                    [key, version](const std::string& error) -> Continuation {
                        return [key, version, error] {
                            if (!currentLoad(key, version)) return;
                            profiles[key].status = Status::Failed;
                            profiles[key].error = error;
                        };
                    });
        }

        struct Change {
            std::string working;
            std::string method;
            std::string path;
            std::string body;
            std::string type;
            std::string invalid;
            std::string forbidden;
            std::string done;
            std::function<void(Profile&)> after;
        };

        void change(const std::string& key, Change request) {
            Profile& entry = profiles[key];
            std::optional<Session> found = sessionOf(key);
            if (!entry.working.empty() || entry.status != Status::Ready || !found) {
                return;
            }
            entry.working = request.working;
            // A read started before this mutation must not restore old profile data.
            ++loadVersions[key];
            tasks.run(
                    [key, request, session = *found](blb::cancellation_token token) mutable -> Continuation {
                        auto response = call(session, request.method, request.path, request.body, request.type, token);
                        std::string problem;
                        Profile out;
                        bool parsed = false;
                        if (!response) {
                            problem = Accounts::describe(response.err());
                        } else if (!response->ok()) {
                            problem = problemOf(*response, request.invalid, request.forbidden);
                        } else {
                            parsed = parse(response->body, out);
                        }
                        return [key, request, session, out, parsed, problem] {
                            Profile& entry = profiles[key];
                            entry.working.clear();
                            if (!problem.empty()) {
                                settle(key, session, nullptr);
                                Notices::add(Notices::Kind::Failed, problem, 5000);
                                return;
                            }
                            if (parsed) {
                                entry.name = out.name;
                                entry.skinUrl = out.skinUrl;
                                entry.slim = out.slim;
                                entry.capes = out.capes;
                            }
                            if (request.after) {
                                request.after(entry);
                            }
                            settle(key, session, parsed ? &entry : nullptr);
                            Notices::add(Notices::Kind::Done, request.done, 3000);
                            if (!parsed) {
                                load(key);
                            }
                        };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            profiles[key].working.clear();
                            Notices::add(Notices::Kind::Failed, error, 5000);
                        };
                    });
        }

        uint32_t unitOf(const Pixels& source) {
            if (source.width % 46 == 0 && source.height % 22 == 0) {
                return (uint32_t) std::max(1, source.width / 46);
            }
            return (uint32_t) std::max(1, source.width / 64);
        }

        const unsigned char* at(const Pixels& source, int x, int y) {
            return source.rgba.data() + ((size_t) y * source.width + x) * 4;
        }

        std::shared_ptr<const Pixels> enlarge(const std::vector<unsigned char>& rgba, int width, int height,
                                              int scale) {
            auto out = std::make_shared<Pixels>();
            out->width = width * scale;
            out->height = height * scale;
            out->rgba.resize((size_t) out->width * out->height * 4);
            for (int y = 0; y < out->height; y++) {
                for (int x = 0; x < out->width; x++) {
                    const unsigned char* from = rgba.data() + ((size_t) (y / scale) * width + x / scale) * 4;
                    std::copy(from, from + 4, out->rgba.data() + ((size_t) y * out->width + x) * 4);
                }
            }
            return out;
        }

        std::shared_ptr<const Pixels> capeImage(const Pixels& source) {
            if (source.width < 22 || source.height < 17) {
                return nullptr;
            }
            int unit = (int) unitOf(source);
            std::vector<unsigned char> front((size_t) 10 * 16 * 4);
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 10; x++) {
                    int sx = (1 + x) * unit;
                    int sy = (1 + y) * unit;
                    if (sx >= source.width || sy >= source.height) {
                        continue;
                    }
                    const unsigned char* from = at(source, sx, sy);
                    std::copy(from, from + 4, front.data() + ((size_t) y * 10 + x) * 4);
                }
            }
            return enlarge(front, 10, 16, CAPE_SCALE);
        }

        void lookOptiFine(const std::string& key, const std::string& name) {
            Profile& entry = profiles[key];
            std::string url = std::string(OPTIFINE_CAPES) + name + ".png";
            entry.optifine = OptiFine::Checking;
            entry.optifineUrl = url;
            tasks.run(
                    [key, url](blb::cancellation_token token) -> Continuation {
                        blb::net::http_request request;
                        request.url = url;
                        request.timeout = std::chrono::milliseconds(15000);
                        auto response = blb::net::http_send(request, token);
                        OptiFine state = OptiFine::Failed;
                        std::shared_ptr<const Pixels> made;
                        if (response && response->status == 404) {
                            state = OptiFine::None;
                        } else if (response && response->ok()) {
                            std::shared_ptr<const Pixels> decoded =
                                    Pixels::decode(response->body.data(), response->body.size());
                            made = decoded != nullptr ? capeImage(*decoded) : nullptr;
                            state = made != nullptr ? OptiFine::Owned : OptiFine::Failed;
                        }
                        return [key, url, state, made] {
                            Profile& entry = profiles[key];
                            if (entry.optifineUrl != url) {
                                return;
                            }
                            entry.optifine = state;
                            if (made != nullptr) {
                                images["cape:" + url] = Image{true, made};
                            }
                        };
                    },
                    [key, url](const std::string&) -> Continuation {
                        return [key, url] {
                            if (profiles[key].optifineUrl == url) {
                                profiles[key].optifine = OptiFine::Failed;
                            }
                        };
                    });
        }

        std::shared_ptr<const Pixels> image(const std::string& id, const std::string& url,
                                            std::function<std::shared_ptr<const Pixels>(const Pixels&)> make) {
            if (url.empty()) {
                return nullptr;
            }
            Image& entry = images[id];
            if (!entry.requested) {
                entry.requested = true;
                tasks.run(
                        [id, url, make](blb::cancellation_token token) -> Continuation {
                            auto data = blb::net::http_get(url, token);
                            std::shared_ptr<const Pixels> source =
                                    data ? Pixels::decode(data->data(), data->size()) : nullptr;
                            std::shared_ptr<const Pixels> made = source != nullptr ? make(*source) : nullptr;
                            return [id, made] { images[id] = Image{true, made, made == nullptr}; };
                        },
                        [id](const std::string&) -> Continuation {
                            return [id] { images[id].failed = true; };
                        });
            }
            return entry.pixels;
        }
    }

    bool validName(const std::string& name) {
        if (name.size() < 3 || name.size() > 16) {
            return false;
        }
        return std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        });
    }

    const Profile& of(const std::string& key) {
        if (profiles.find(key) == profiles.end()) {
            load(key);
        }
        return profiles[key];
    }

    void reload(const std::string& key) {
        if (profiles[key].working.empty()) {
            // Retry failures on an explicit reload; keep successful and in-flight images.
            std::erase_if(images, [](const auto& item) { return item.second.failed; });
            load(key);
        }
    }

    void uploadSkin(const std::string& key, const std::filesystem::path& file) {
        auto data = blb::fs::read_file(file);
        if (!data) {
            Notices::add(Notices::Kind::Failed, "无法读取皮肤图片：" + data.err().message, 5000);
            return;
        }
        std::shared_ptr<const Pixels> decoded = Pixels::decode(data->data(), data->size());
        if (decoded == nullptr || decoded->width != 64 || (decoded->height != 64 && decoded->height != 32)) {
            Notices::add(Notices::Kind::Failed, "皮肤图片需要是 64×64 或 64×32 的 PNG", 5000);
            return;
        }
        bool slim = decoded->height == 64;
        for (int y = 20; y < 32 && slim; y++) {
            for (int x = 54; x < 56 && slim; x++) {
                slim = at(*decoded, x, y)[3] == 0;
            }
        }
        std::string boundary = "----BLauncher" + blb::random_bytes_hex(12);
        std::string body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"variant\"\r\n\r\n" +
                (slim ? "slim" : "classic") + "\r\n--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\n"
                "Content-Type: image/png\r\n\r\n" +
                *data + "\r\n--" + boundary + "--\r\n";
        change(key,
               Change{"skin", "POST", "/skins", std::move(body), "multipart/form-data; boundary=" + boundary,
                      "皮肤图片不符合要求", std::string(), slim ? "已更换皮肤（纤细模型）" : "已更换皮肤（经典模型）",
                      nullptr});
    }

    void resetSkin(const std::string& key) {
        change(key,
               Change{"skin", "DELETE", "/skins/active", std::string(), std::string(), std::string(), std::string(),
                      "已恢复默认皮肤", nullptr});
    }

    void setCape(const std::string& key, const std::string& capeId) {
        if (capeId.empty()) {
            change(key,
                   Change{"cape:", "DELETE", "/capes/active", std::string(), std::string(), std::string(),
                          std::string(), "已隐藏披风", nullptr});
            return;
        }
        std::string alias;
        for (const Cape& cape : of(key).capes) {
            if (cape.id == capeId) {
                alias = cape.alias;
            }
        }
        json body = {{"capeId", capeId}};
        change(key,
               Change{"cape:" + capeId, "PUT", "/capes/active", body.dump(), "application/json", std::string(),
                      "这个账户不能使用这件披风", alias.empty() ? "已更换披风" : "已换上 " + alias + " 披风", nullptr});
    }

    const NameCheck& nameCheck(const std::string& key) {
        auto found = checks.find(key);
        return found != checks.end() ? found->second : noCheck;
    }

    void checkName(const std::string& key, const std::string& name) {
        NameCheck& check = checks[key];
        check.name = name;
        check.error.clear();
        if (!validName(name)) {
            check.state = Check::Invalid;
            return;
        }
        std::optional<Session> found = sessionOf(key);
        if (!found) {
            check.state = Check::Failed;
            check.error = "只有微软账户可以改名";
            return;
        }
        check.state = Check::Checking;
        tasks.run(
                [key, name, session = *found](blb::cancellation_token token) mutable -> Continuation {
                    auto response =
                            call(session, "GET", "/name/" + name + "/available", std::string(), std::string(), token);
                    Check state = Check::Failed;
                    std::string error;
                    if (!response) {
                        error = Accounts::describe(response.err());
                    } else if (!response->ok()) {
                        error = problemOf(*response, "用户名不符合要求", std::string());
                    } else {
                        json doc = json::parse(response->body, nullptr, false);
                        std::string status = doc.is_object() ? blb::json_text(doc, "status") : std::string();
                        state = status == "AVAILABLE"     ? Check::Available
                                : status == "DUPLICATE"   ? Check::Taken
                                : status == "NOT_ALLOWED" ? Check::Invalid
                                                          : Check::Failed;
                        if (state == Check::Failed) {
                            error = "无法确认这个用户名能否使用";
                        }
                    }
                    return [key, name, session, state, error] {
                        settle(key, session, nullptr);
                        NameCheck& check = checks[key];
                        if (check.name != name) {
                            return;
                        }
                        check.state = state;
                        check.error = error;
                    };
                },
                [key, name](const std::string& error) -> Continuation {
                    return [key, name, error] {
                        NameCheck& check = checks[key];
                        if (check.name == name) {
                            check.state = Check::Failed;
                            check.error = error;
                        }
                    };
                });
    }

    void forgetName(const std::string& key) {
        checks.erase(key);
    }

    void rename(const std::string& key, const std::string& name) {
        if (!validName(name)) {
            return;
        }
        change(key,
               Change{"name", "PUT", "/name/" + name, std::string(), std::string(), "用户名不符合要求",
                      "现在还不能改名，每 30 天只能改一次", "用户名已改为 " + name, [key](Profile& profile) {
                          profile.renameAllowed = false;
                          profile.renameAfter = dayAfter(today(), RENAME_DAYS);
                          checks.erase(key);
                          lookOptiFine(key, profile.name);
                      }});
    }

    void checkOptiFine(const std::string& key) {
        const Profile& profile = of(key);
        if (profile.status == Status::Ready && !profile.name.empty()) {
            lookOptiFine(key, profile.name);
        }
    }

    void manageOptiFine(const std::string& key) {
        Profile& entry = profiles[key];
        std::optional<Session> found = sessionOf(key);
        if (!entry.working.empty() || !found) {
            return;
        }
        entry.working = "optifine";
        std::string notice = Notices::live(Notices::Kind::Info, "正在向 Mojang 验证账户…");
        tasks.run(
                [key, notice, session = *found](blb::cancellation_token token) mutable -> Continuation {
                    std::string serverId = blb::random_bytes_hex(16);
                    std::string problem;
                    if (!session.renewed && session.account.needs_refresh()) {
                        if (std::optional<blb::error> failed = renew(session, token)) {
                            problem = Accounts::describe(*failed);
                        }
                    }
                    for (int attempt = 0; attempt < 2 && problem.empty(); attempt++) {
                        json body = {{"accessToken", session.account.access_token},
                                     {"selectedProfile", session.account.uuid},
                                     {"serverId", serverId}};
                        blb::net::http_request request;
                        request.method = "POST";
                        request.url = JOIN;
                        request.body = body.dump();
                        request.headers.push_back({"Content-Type", "application/json"});
                        request.timeout = std::chrono::milliseconds(15000);
                        auto response = blb::net::http_send(request, token);
                        if (!response) {
                            problem = Accounts::describe(response.err());
                        } else if (response->ok()) {
                            break;
                        } else if ((response->status == 401 || response->status == 403) && !session.renewed) {
                            if (std::optional<blb::error> failed = renew(session, token)) {
                                problem = Accounts::describe(*failed);
                            }
                        } else {
                            if (response->status == 401 || response->status == 403) {
                                session.expired = true;
                            }
                            problem = "Mojang 没有通过账户验证（HTTP " + std::to_string(response->status) + "）";
                        }
                    }
                    std::string url = std::string(OPTIFINE_EDITOR) + "?u=" + session.account.uuid +
                            "&n=" + session.account.name + "&s=" + serverId;
                    return [key, notice, session, problem, url] {
                        profiles[key].working.clear();
                        settle(key, session, nullptr);
                        if (!problem.empty()) {
                            Notices::finish(notice, Notices::Kind::Failed, "无法打开 OptiFine 披风管理：" + problem,
                                            5000);
                        } else if (!Platform::openUrl(url)) {
                            Notices::finish(notice, Notices::Kind::Failed, "无法打开浏览器", 4000);
                        } else {
                            Notices::finish(notice, Notices::Kind::Done, "已在浏览器中打开 OptiFine 披风管理", 3000);
                        }
                    };
                },
                [key, notice](const std::string& error) -> Continuation {
                    return [key, notice, error] {
                        profiles[key].working.clear();
                        Notices::finish(notice, Notices::Kind::Failed, "无法打开 OptiFine 披风管理：" + error, 5000);
                    };
                });
    }

    std::shared_ptr<const Pixels> capeFront(const std::string& url) {
        return image("cape:" + url, url, capeImage);
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}

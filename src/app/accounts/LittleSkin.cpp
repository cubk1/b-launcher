#include "app/accounts/LittleSkin.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Avatars.h"
#include "app/accounts/SkinRender.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "app/system/Redirect.h"

#include <core/auth/yggdrasil.hpp>
#include <core/base/cancellation.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <utility>

namespace LittleSkin {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr const char* CLIENT_ID = "1685";
        constexpr const char* CLIENT_SECRET = "w4YU0I0vNu8JwYtW1sQTDIfnGDYRnREKyys0jglI";
        constexpr int PORT = 41275;
        constexpr const char* PATH = "/ls/auth";
        constexpr const char* REDIRECT = "http://localhost:41275/ls/auth";
        constexpr const char* SITE = "https://littleskin.cn";
        constexpr const char* SCOPES = "User.Read Player.ReadWrite Closet.Read";
        constexpr const char* LOGIN_SCOPES =
                "User.Read Player.ReadWrite Closet.Read Yggdrasil.PlayerProfiles.Read Yggdrasil.MinecraftToken.Create";
        constexpr const char* STORE = "LittleSkinTokens";
        constexpr const char* EXPIRED = "LittleSkin 授权已失效，请重新授权";
        constexpr int PREVIEW_SIZE = 128;

        struct Token {
            std::string access;
            std::string refresh;
        };

        struct Session {
            Token token;
            bool renewed = false;
            bool expired = false;
        };

        struct Preview {
            bool requested = false;
            std::shared_ptr<const Pixels> pixels;
        };

        AsyncTasks tasks;
        std::map<std::string, State> states;
        std::map<std::string, Token> tokens;
        std::map<std::string, Preview> previews;
        bool tokensLoaded = false;
        std::string authorizingKey;
        std::shared_ptr<blb::cancellation_source> authorizing;

        void loadTokens() {
            if (tokensLoaded) {
                return;
            }
            tokensLoaded = true;
            std::optional<std::string> stored = Accounts::loadSealed(STORE);
            if (!stored) {
                return;
            }
            nlohmann::json doc = nlohmann::json::parse(*stored, nullptr, false);
            if (!doc.is_object()) {
                return;
            }
            for (const auto& [key, value] : doc.items()) {
                if (value.is_object() && !blb::json_text(value, "access").empty()) {
                    tokens[key] = Token{blb::json_text(value, "access"), blb::json_text(value, "refresh")};
                }
            }
        }

        void saveTokens() {
            nlohmann::json doc = nlohmann::json::object();
            for (const auto& [key, token] : tokens) {
                doc[key] = {{"access", token.access}, {"refresh", token.refresh}};
            }
            (void) Accounts::storeSealed(STORE, doc.dump());
        }

        void settle(const std::string& key, const Session& session) {
            if (session.expired) {
                if (tokens.erase(key) != 0) {
                    saveTokens();
                }
            } else if (session.renewed) {
                std::string previous = tokens.count(key) != 0 ? tokens[key].refresh : std::string();
                for (auto& [other, token] : tokens) {
                    if (other != key && !previous.empty() && token.refresh == previous) {
                        token = session.token;
                    }
                }
                tokens[key] = session.token;
                saveTokens();
            }
        }

        long long number(const nlohmann::json& node, const char* key) {
            auto found = node.find(key);
            if (found == node.end()) {
                return 0;
            }
            if (found->is_number_integer()) {
                return found->get<long long>();
            }
            if (found->is_string()) {
                return std::atoll(found->get<std::string>().c_str());
            }
            return 0;
        }

        std::string problemOf(const nlohmann::json& doc, long status) {
            std::string text;
            if (doc.is_object()) {
                text = blb::json_text(doc, "message");
                if (text.empty()) {
                    text = blb::json_text(doc, "error_description");
                }
            }
            return text.empty() ? "LittleSkin 返回错误（HTTP " + std::to_string(status) + "）" : text;
        }

        bool exchange(std::vector<std::pair<std::string, std::string>> fields, Token& token, std::string& error,
                      blb::cancellation_token cancel) {
            fields.push_back({"client_id", CLIENT_ID});
            fields.push_back({"client_secret", CLIENT_SECRET});
            auto response = blb::net::http_post_form(std::string(SITE) + "/oauth/token", fields, cancel);
            if (!response) {
                error = response.err().message;
                return false;
            }
            nlohmann::json doc = nlohmann::json::parse(response->body, nullptr, false);
            if (!response->ok() || !doc.is_object() || blb::json_text(doc, "access_token").empty()) {
                error = problemOf(doc, response->status);
                return false;
            }
            token.access = blb::json_text(doc, "access_token");
            std::string refresh = blb::json_text(doc, "refresh_token");
            if (!refresh.empty()) {
                token.refresh = refresh;
            }
            return true;
        }

        bool call(Session& session, const std::string& method, const std::string& path, const std::string& body,
                  nlohmann::json& out, std::string& error, blb::cancellation_token cancel) {
            for (int attempt = 0; attempt < 2; attempt++) {
                blb::net::http_request request;
                request.method = method;
                request.url = std::string(SITE) + path;
                request.timeout = std::chrono::milliseconds(20000);
                request.headers.push_back({"Authorization", "Bearer " + session.token.access});
                request.headers.push_back({"Accept", "application/json"});
                if (!body.empty()) {
                    request.headers.push_back({"Content-Type", "application/json"});
                    request.body = body;
                }
                auto response = blb::net::http_send(request, cancel);
                if (!response) {
                    error = response.err().message;
                    return false;
                }
                if (response->status == 401) {
                    std::string ignored;
                    if (attempt == 0 && !session.token.refresh.empty() &&
                        exchange({{"grant_type", "refresh_token"}, {"refresh_token", session.token.refresh}},
                                 session.token, ignored, cancel)) {
                        session.renewed = true;
                        continue;
                    }
                    session.expired = true;
                    error = EXPIRED;
                    return false;
                }
                out = nlohmann::json::parse(response->body, nullptr, false);
                if (!response->ok()) {
                    error = problemOf(out, response->status);
                    return false;
                }
                if (out.is_object() && out.contains("code") && out["code"].is_number_integer() &&
                    out["code"].get<int>() != 0) {
                    error = out["code"].get<int>() == 403 ? std::string("没有授予这项权限，请重新授权")
                                                          : problemOf(out, response->status);
                    return false;
                }
                return true;
            }
            error = EXPIRED;
            return false;
        }

        bool closet(Session& session, const char* category, std::vector<Texture>& out, std::string& error,
                    blb::cancellation_token cancel) {
            for (int page = 1; page <= 20; page++) {
                nlohmann::json doc;
                if (!call(session, "GET",
                          std::string("/api/closet?category=") + category + "&page=" + std::to_string(page),
                          std::string(), doc, error, cancel)) {
                    return false;
                }
                const nlohmann::json* items = nullptr;
                if (doc.is_object() && doc.contains("data") && doc["data"].is_array()) {
                    items = &doc["data"];
                } else if (doc.is_array()) {
                    items = &doc;
                }
                if (items == nullptr) {
                    break;
                }
                for (const nlohmann::json& item : *items) {
                    if (!item.is_object()) {
                        continue;
                    }
                    Texture texture;
                    texture.tid = number(item, "tid");
                    auto pivot = item.find("pivot");
                    if (pivot != item.end() && pivot->is_object()) {
                        texture.name = blb::json_text(*pivot, "item_name");
                    }
                    if (texture.name.empty()) {
                        texture.name = blb::json_text(item, "name");
                    }
                    texture.hash = blb::json_text(item, "hash");
                    texture.slim = blb::json_text(item, "type") == "alex";
                    if (texture.tid > 0 && !texture.hash.empty()) {
                        out.push_back(std::move(texture));
                    }
                }
                if (!doc.is_object() || page >= number(doc, "last_page")) {
                    break;
                }
            }
            return true;
        }

        bool load(Session& session, const std::string& name, State& state, std::string& error,
                  blb::cancellation_token cancel) {
            nlohmann::json players;
            if (!call(session, "GET", "/api/players", std::string(), players, error, cancel)) {
                return false;
            }
            if (!players.is_array()) {
                error = "LittleSkin 返回的角色列表无效";
                return false;
            }
            const nlohmann::json* match = nullptr;
            for (const nlohmann::json& player : players) {
                if (player.is_object() && blb::to_lower(blb::json_text(player, "name")) == blb::to_lower(name)) {
                    match = &player;
                }
            }
            if (match == nullptr) {
                error = "这个 LittleSkin 账号下没有角色 " + name;
                return false;
            }
            state.pid = number(*match, "pid");
            state.player = blb::json_text(*match, "name");
            state.skin = number(*match, "tid_skin");
            state.cape = number(*match, "tid_cape");
            return closet(session, "skin", state.skins, error, cancel) &&
                    closet(session, "cape", state.capes, error, cancel);
        }

        std::string consentUrl(const char* scopes, const std::string& nonce) {
            return std::string(SITE) + "/oauth/authorize?client_id=" + CLIENT_ID +
                    "&redirect_uri=" + blb::url_encode(REDIRECT) +
                    "&response_type=code&scope=" + blb::url_encode(scopes) + "&state=" + nonce;
        }

        std::string resultPage(const Redirect::Query&) {
            return "<!doctype html><html><head><meta charset=\"utf-8\"><title>LittleSkin</title></head>"
                   "<body style=\"font-family:sans-serif;padding:48px\">授权完成，可以回到启动器了。"
                   "<script>window.close()</script></body></html>";
        }

        void mutate(const std::string& key, const std::string& working, const std::string& method,
                    const std::string& path, const std::string& body, const std::string& message,
                    std::function<void(State&)> done) {
            loadTokens();
            auto token = tokens.find(key);
            State& state = states[key];
            if (token == tokens.end() || state.status != Status::Ready || !state.working.empty()) {
                return;
            }
            state.working = working;
            state.error.clear();
            Session session{token->second};
            tasks.run(
                    [key, session, method, path, body, message,
                     done](blb::cancellation_token cancel) mutable -> Continuation {
                        nlohmann::json reply;
                        std::string error;
                        bool ok = call(session, method, path, body, reply, error, cancel);
                        return [key, session, ok, error, message, done] {
                            settle(key, session);
                            State& state = states[key];
                            state.working.clear();
                            if (session.expired) {
                                state = State();
                                state.error = error;
                            }
                            if (!ok) {
                                Notices::add(Notices::Kind::Failed, error, 5000);
                                return;
                            }
                            done(state);
                            Notices::add(Notices::Kind::Done, message, 3000);
                        };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            states[key].working.clear();
                            Notices::add(Notices::Kind::Failed, error, 5000);
                        };
                    });
        }
    }

    struct Consent {
        std::shared_ptr<Redirect> redirect;
        std::string nonce;
    };

    std::shared_ptr<Consent> askConsent(std::string& error) {
        if (!authorizingKey.empty()) {
            error = "正在授权 LittleSkin，请先完成或取消";
            return nullptr;
        }
        auto consent = std::make_shared<Consent>();
        consent->redirect = std::make_shared<Redirect>();
        auto opened = consent->redirect->open(PORT);
        if (!opened) {
            error = "无法开始授权：" + opened.err().message;
            return nullptr;
        }
        consent->nonce = blb::random_bytes_hex(16);
        if (!Platform::openUrl(consentUrl(LOGIN_SCOPES, consent->nonce))) {
            consent->redirect->close();
            error = "无法打开浏览器";
            return nullptr;
        }
        return consent;
    }

    std::optional<Grant> awaitConsent(const std::shared_ptr<Consent>& consent, blb::cancellation_token cancel,
                                      std::string& error) {
        std::string nonce = consent->nonce;
        auto query = consent->redirect->wait(PATH, cancel, std::chrono::minutes(10), resultPage,
                                             [nonce](const Redirect::Query& query) {
                                                 auto found = query.find("state");
                                                 return found != query.end() && found->second == nonce;
                                             });
        consent->redirect->close();
        if (!query) {
            error = query.err().code == blb::errc::cancelled ? std::string() : query.err().message;
            return std::nullopt;
        }
        if (query->count("code") == 0) {
            auto reason = query->find("error_description");
            error = reason != query->end() ? reason->second : std::string("授权被拒绝");
            return std::nullopt;
        }
        Token token;
        if (!exchange({{"grant_type", "authorization_code"}, {"redirect_uri", REDIRECT}, {"code", query->at("code")}},
                      token, error, cancel)) {
            return std::nullopt;
        }
        return Grant{token.access, token.refresh};
    }

    void adopt(const std::string& key, const Grant& grant) {
        loadTokens();
        tokens[key] = Token{grant.access, grant.refresh};
        saveTokens();
        states.erase(key);
    }

    bool supports(const blb::auth::account& account) {
        return account.type == blb::auth::account_type::yggdrasil &&
                blb::auth::same_yggdrasil_server(account.server, blb::auth::littleskin_api);
    }

    const State& of(const std::string& key) {
        loadTokens();
        auto found = states.find(key);
        if (found != states.end()) {
            return found->second;
        }
        states[key] = State();
        if (tokens.count(key) != 0) {
            reload(key);
        }
        return states[key];
    }

    void authorize(const std::string& key) {
        if (!authorizingKey.empty()) {
            return;
        }
        State& state = states[key];
        state.error.clear();
        if (Accounts::login() != Accounts::Login::Idle) {
            state.error = "正在进行微软登录，请稍后再授权";
            return;
        }
        auto redirect = std::make_shared<Redirect>();
        auto opened = redirect->open(PORT);
        if (!opened) {
            state.error = "无法开始授权：" + opened.err().message;
            return;
        }
        std::string nonce = blb::random_bytes_hex(16);
        if (!Platform::openUrl(consentUrl(SCOPES, nonce))) {
            redirect->close();
            state.error = "无法打开浏览器";
            return;
        }
        const blb::auth::account* account = Accounts::find(key);
        std::string name = account != nullptr ? account->name : std::string();
        authorizingKey = key;
        authorizing = std::make_shared<blb::cancellation_source>();
        state.status = Status::Authorizing;
        blb::cancellation_token waiting = authorizing->token();
        tasks.run(
                [key, redirect, nonce, name, waiting](blb::cancellation_token cancel) -> Continuation {
                    auto query = redirect->wait(PATH, waiting, std::chrono::minutes(10), resultPage,
                                                [nonce](const Redirect::Query& query) {
                                                    auto found = query.find("state");
                                                    return found != query.end() && found->second == nonce;
                                                });
                    redirect->close();
                    std::string error;
                    Session session;
                    State loaded;
                    bool ok = false;
                    if (!query) {
                        error = query.err().code == blb::errc::cancelled ? std::string() : query.err().message;
                    } else if (query->count("code") == 0) {
                        auto reason = query->find("error_description");
                        error = reason != query->end() ? reason->second : std::string("授权被拒绝");
                    } else if (exchange({{"grant_type", "authorization_code"},
                                         {"redirect_uri", REDIRECT},
                                         {"code", query->at("code")}},
                                        session.token, error, cancel)) {
                        ok = load(session, name, loaded, error, cancel);
                    }
                    bool granted = !session.token.access.empty();
                    return [key, session, loaded, ok, granted, error] {
                        authorizingKey.clear();
                        authorizing.reset();
                        if (granted && !session.expired) {
                            tokens[key] = session.token;
                            saveTokens();
                        }
                        State& state = states[key];
                        if (ok) {
                            state = loaded;
                            state.status = Status::Ready;
                        } else {
                            state.status = granted ? Status::Failed : Status::Unauthorized;
                            state.error = error;
                        }
                    };
                },
                [key](const std::string& error) -> Continuation {
                    return [key, error] {
                        authorizingKey.clear();
                        authorizing.reset();
                        State& state = states[key];
                        state.status = Status::Unauthorized;
                        state.error = error;
                    };
                });
    }

    void cancel() {
        if (authorizing) {
            authorizing->cancel();
        }
    }

    void reload(const std::string& key) {
        loadTokens();
        State& state = states[key];
        auto token = tokens.find(key);
        const blb::auth::account* account = Accounts::find(key);
        if (token == tokens.end() || account == nullptr) {
            state = State();
            return;
        }
        if (state.status == Status::Loading || state.status == Status::Authorizing) {
            return;
        }
        state.status = Status::Loading;
        state.error.clear();
        Session session{token->second};
        std::string name = account->name;
        tasks.run(
                [key, session, name](blb::cancellation_token cancel) mutable -> Continuation {
                    State loaded;
                    std::string error;
                    bool ok = load(session, name, loaded, error, cancel);
                    return [key, session, loaded, ok, error] {
                        settle(key, session);
                        State& state = states[key];
                        if (ok) {
                            state = loaded;
                            state.status = Status::Ready;
                        } else {
                            state = State();
                            state.status = session.expired ? Status::Unauthorized : Status::Failed;
                            state.error = error;
                        }
                    };
                },
                [key](const std::string& error) -> Continuation {
                    return [key, error] {
                        State& state = states[key];
                        state.status = Status::Failed;
                        state.error = error;
                    };
                });
    }

    void rename(const std::string& key, const std::string& name) {
        State& state = states[key];
        std::string body = nlohmann::json({{"name", name}}).dump();
        mutate(key, "name", "PUT", "/api/players/" + std::to_string(state.pid) + "/name", body, "角色名已改为 " + name,
               [key, name](State& state) {
                   state.player = name;
                   if (const blb::auth::account* account = Accounts::find(key)) {
                       blb::auth::account renamed = *account;
                       renamed.name = name;
                       Accounts::refreshed(renamed);
                   }
                   Avatars::refresh(key);
               });
    }

    void wear(const std::string& key, bool cape, long long tid) {
        State& state = states[key];
        const char* type = cape ? "cape" : "skin";
        std::string path = "/api/players/" + std::to_string(state.pid) + "/textures";
        std::string body = tid > 0 ? nlohmann::json({{type, tid}}).dump()
                                   : nlohmann::json({{"type", nlohmann::json::array({type})}}).dump();
        std::string message = cape ? "已隐藏披风" : "已移除皮肤";
        for (const Texture& texture : cape ? state.capes : state.skins) {
            if (tid > 0 && texture.tid == tid) {
                message = "已换上 " + texture.name;
            }
        }
        mutate(key, std::string(type) + ":" + std::to_string(tid), tid > 0 ? "PUT" : "DELETE", path, body, message,
               [key, cape, tid](State& state) {
                   (cape ? state.cape : state.skin) = tid;
                   Avatars::refresh(key);
               });
    }

    void signOut(const std::string& key) {
        loadTokens();
        if (tokens.erase(key) != 0) {
            saveTokens();
        }
        states[key] = State();
    }

    std::string textureUrl(const std::string& hash) {
        return std::string(SITE) + "/textures/" + hash;
    }

    std::string uploadUrl() {
        return std::string(SITE) + "/skinlib/upload";
    }

    std::shared_ptr<const Pixels> preview(const Texture& texture) {
        Preview& entry = previews[texture.hash];
        if (!entry.requested) {
            entry.requested = true;
            std::string url = textureUrl(texture.hash);
            std::string hash = texture.hash;
            bool slim = texture.slim;
            tasks.run(
                    [url, hash, slim](blb::cancellation_token cancel) -> Continuation {
                        auto data = blb::net::http_get(url, cancel);
                        std::shared_ptr<const Pixels> skin =
                                data ? Pixels::decode(data->data(), data->size()) : nullptr;
                        if (skin == nullptr) {
                            return nullptr;
                        }
                        SkinRender::Options options;
                        options.size = PREVIEW_SIZE;
                        options.slim = slim;
                        std::shared_ptr<const Pixels> pixels = SkinRender::render(*skin, nullptr, options);
                        return [hash, pixels] { previews[hash].pixels = pixels; };
                    },
                    [](const std::string&) -> Continuation { return nullptr; });
        }
        return entry.pixels;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        cancel();
        tasks.stop();
    }
}

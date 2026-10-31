#include "app/accounts/Accounts.h"
#include "app/system/About.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"
#include "app/system/Redirect.h"
#include "app/system/Notices.h"

#include <core/auth/offline.hpp>
#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include "stb_image.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace Accounts {

    using MicrosoftClients::App;

    namespace {
        using Continuation = std::function<void()>;

        constexpr std::chrono::minutes LOGIN_TIMEOUT{10};
        constexpr int FACE_SCALE = 8;
        constexpr int FACE_SIZE = 8 * FACE_SCALE;

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;

        std::vector<blb::auth::account> accounts;
        std::string currentKey;
        std::set<std::string> busyKeys;
        std::set<std::string> expiredKeys;

        std::atomic<int> loginState{(int) Login::Idle};
        std::shared_ptr<blb::cancellation_source> loginCancel;

        ServerLogin serverState = ServerLogin::Idle;
        int serverRun = 0;
        std::shared_ptr<blb::cancellation_source> serverCancel;
        blb::auth::account pendingBase;
        std::vector<blb::auth::yggdrasil_profile> profiles;

        std::vector<Found> foundList;
        bool scanStarted = false;
        bool scanRunning = false;

        struct Face {
            std::shared_ptr<const Pixels> pixels;
            bool requested = false;
        };
        std::map<std::string, Face> faces;

        const char* const STORE_VALUE = "Accounts";

        void run(std::function<Continuation(blb::cancellation_token)> work,
                 std::function<Continuation(const std::string&)> fail) {
            blb::cancellation_token token = stopping.token();
            workers.emplace_back([work, fail, token] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    next = fail ? fail(error.what()) : nullptr;
                }
                if (next) {
                    std::lock_guard<std::mutex> guard(lock);
                    finished.push_back(std::move(next));
                }
            });
        }

        void notify(const std::string& text, bool error) {
            Notices::add(error ? Notices::Kind::Failed : Notices::Kind::Done, text, error ? 5000 : 2500);
        }

        std::optional<std::string> protect(const std::string& plain, bool encrypt) {
            DATA_BLOB input = {(DWORD) plain.size(), (BYTE*) plain.data()};
            DATA_BLOB output = {};
            BOOL done = encrypt ? CryptProtectData(&input, L"b-launcher accounts", nullptr, nullptr, nullptr,
                                                   CRYPTPROTECT_UI_FORBIDDEN, &output)
                                : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                                                     CRYPTPROTECT_UI_FORBIDDEN, &output);
            if (!done) {
                return std::nullopt;
            }
            std::string out((const char*) output.pbData, output.cbData);
            LocalFree(output.pbData);
            return out;
        }

        void load() {
            std::optional<std::string> sealed = Platform::loadValue(STORE_VALUE);
            if (!sealed) {
                return;
            }
            std::optional<std::string> plain = protect(*sealed, false);
            if (!plain) {
                Notices::add(Notices::Kind::Failed, "无法读取保存的账户，请重新添加", 5000);
                return;
            }
            auto parsed = blb::auth::parse_accounts(*plain);
            if (parsed) {
                accounts = std::move(*parsed);
            }
        }

        void save() {
            std::optional<std::string> sealed = protect(blb::auth::serialize_accounts(accounts), true);
            if (!sealed) {
                notify("无法保存账户", true);
                return;
            }
            if (!Platform::storeValue(STORE_VALUE, *sealed, false)) {
                notify("无法保存账户", true);
            }
        }

        void saveCurrent() {
            Config::data()["account"] = currentKey;
            Config::save();
        }

        void put(const blb::auth::account& account, bool makeCurrent) {
            std::string key = keyOf(account);
            auto existing = std::find_if(accounts.begin(), accounts.end(),
                                         [&](const blb::auth::account& a) { return keyOf(a) == key; });
            if (existing != accounts.end()) {
                *existing = account;
            } else {
                accounts.push_back(account);
            }
            expiredKeys.erase(key);
            save();
            if (makeCurrent) {
                select(key);
            }
        }

        std::string resultPage(const Redirect::Query&) {
            return std::string("<!doctype html><html><head><meta charset=\"utf-8\"><title>") + About::NAME +
                    "</title></head><body><script>window.close()</script></body></html>";
        }

        Continuation loginFailed(const blb::error& error) {
            return [error] {
                loginState = (int) Login::Idle;
                if (error.code != blb::errc::cancelled) {
                    notify("登录失败：" + describe(error), true);
                }
            };
        }

        blb::cancellation_token beginServerRun() {
            if (serverCancel) {
                serverCancel->cancel();
            }
            serverRun++;
            serverState = ServerLogin::Working;
            serverCancel = std::make_shared<blb::cancellation_source>();
            return serverCancel->token();
        }

        Continuation serverFailed(int run, const blb::error& error) {
            return [run, error] {
                if (run != serverRun || serverState != ServerLogin::Working) {
                    return;
                }
                serverState = profiles.empty() ? ServerLogin::Idle : ServerLogin::Choosing;
                if (error.code != blb::errc::cancelled) {
                    notify("登录失败：" + describe(error), true);
                }
            };
        }

        Continuation serverSucceeded(int run, const blb::auth::account& account) {
            return [run, account] {
                if (run != serverRun || serverState != ServerLogin::Working) {
                    return;
                }
                serverState = ServerLogin::Done;
                profiles.clear();
                pendingBase = blb::auth::account();
                put(account, true);
                notify("已登录 " + account.name, false);
            };
        }

        bool matches(const blb::auth::account& account, const std::string& login, const std::string& server) {
            std::string wanted(blb::trim(server));
            if (login == "microsoft") {
                return account.type == blb::auth::account_type::microsoft;
            }
            if (login == "offline") {
                return account.type == blb::auth::account_type::offline;
            }
            if (login == "yggdrasil") {
                return account.type == blb::auth::account_type::yggdrasil &&
                        (wanted.empty() || blb::auth::same_yggdrasil_server(account.server, wanted));
            }
            if (login == "nide8") {
                return account.type == blb::auth::account_type::nide8 &&
                        (wanted.empty() || blb::iequals(account.server, wanted));
            }
            return true;
        }

        std::filesystem::path skinFile(const std::string& url) {
            std::filesystem::path cache = Platform::cacheDirectory();
            size_t slash = url.find_last_of('/');
            std::string hash = slash == std::string::npos ? url : url.substr(slash + 1);
            if (cache.empty() || hash.empty() || hash.size() > 128 ||
                hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
                return {};
            }
            return cache / "skins" / (hash + ".png");
        }

        std::shared_ptr<std::vector<unsigned char>> faceOf(const std::string& skin) {
            int w = 0;
            int h = 0;
            int channels = 0;
            unsigned char* pixels =
                    stbi_load_from_memory((const stbi_uc*) skin.data(), (int) skin.size(), &w, &h, &channels, 4);
            if (pixels == nullptr) {
                return nullptr;
            }
            if (w < 64 || h < 32) {
                stbi_image_free(pixels);
                return nullptr;
            }
            auto rgba = std::make_shared<std::vector<unsigned char>>((size_t) FACE_SIZE * FACE_SIZE * 4);
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    const unsigned char* face = pixels + ((size_t) (8 + y) * w + 8 + x) * 4;
                    const unsigned char* hat = pixels + ((size_t) (8 + y) * w + 40 + x) * 4;
                    float cover = hat[3] / 255.0f;
                    unsigned char color[4];
                    for (int k = 0; k < 3; k++) {
                        color[k] = (unsigned char) (face[k] * (1.0f - cover) + hat[k] * cover + 0.5f);
                    }
                    color[3] = 255;
                    for (int dy = 0; dy < FACE_SCALE; dy++) {
                        for (int dx = 0; dx < FACE_SCALE; dx++) {
                            std::copy(color, color + 4,
                                      rgba->data() +
                                              ((size_t) (y * FACE_SCALE + dy) * FACE_SIZE + x * FACE_SCALE + dx) * 4);
                        }
                    }
                }
            }
            stbi_image_free(pixels);
            return rgba;
        }

        void fetchFace(const std::string& url) {
            run(
                    [url](blb::cancellation_token token) -> Continuation {
                        std::filesystem::path file = skinFile(url);
                        std::shared_ptr<std::vector<unsigned char>> rgba;
                        if (!file.empty()) {
                            if (auto cached = blb::fs::read_file(file)) {
                                rgba = faceOf(*cached);
                            }
                        }
                        if (rgba == nullptr) {
                            auto data = blb::net::http_get(url, token);
                            if (!data) {
                                return nullptr;
                            }
                            rgba = faceOf(*data);
                            if (rgba == nullptr) {
                                return nullptr;
                            }
                            if (!file.empty() && blb::fs::create_directories(file.parent_path())) {
                                (void) blb::fs::write_file_atomic(file, *data);
                            }
                        }
                        return [url, rgba] {
                            faces[url].pixels = std::make_shared<const Pixels>(Pixels{FACE_SIZE, FACE_SIZE, *rgba});
                        };
                    },
                    nullptr);
        }
    }

    void init() {
        load();
        const nlohmann::json& doc = Config::data();
        auto saved = doc.find("account");
        currentKey = saved != doc.end() && saved->is_string() ? saved->get<std::string>() : std::string();
    }

    void poll() {
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            if (next) {
                next();
            }
        }
    }

    void shutdown() {
        stopping.cancel();
        if (loginCancel) {
            loginCancel->cancel();
        }
        if (serverCancel) {
            serverCancel->cancel();
        }
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }

    std::string keyOf(const blb::auth::account& account) {
        return std::string(blb::auth::account_type_name(account.type)) + ":" + account.uuid;
    }

    const std::vector<blb::auth::account>& list() {
        return accounts;
    }

    const blb::auth::account* find(const std::string& key) {
        for (const blb::auth::account& account : accounts) {
            if (keyOf(account) == key) {
                return &account;
            }
        }
        return nullptr;
    }

    const blb::auth::account* current() {
        if (const blb::auth::account* chosen = find(currentKey)) {
            return chosen;
        }
        return accounts.empty() ? nullptr : &accounts.front();
    }

    void select(const std::string& key) {
        if (find(key) == nullptr || key == currentKey) {
            return;
        }
        currentKey = key;
        saveCurrent();
    }

    void remove(const std::string& key) {
        auto at = std::find_if(accounts.begin(), accounts.end(),
                               [&](const blb::auth::account& a) { return keyOf(a) == key; });
        if (at == accounts.end()) {
            return;
        }
        bool wasCurrent = current() == &*at;
        size_t index = (size_t) (at - accounts.begin());
        accounts.erase(at);
        expiredKeys.erase(key);
        save();
        if (wasCurrent) {
            currentKey = accounts.empty() ? std::string() : keyOf(accounts[std::min(index, accounts.size() - 1)]);
            saveCurrent();
        }
    }

    std::string addOffline(const std::string& typed) {
        std::string name = typed;
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);
        if (std::none_of(accounts.begin(), accounts.end(),
                         [](const blb::auth::account& a) { return a.type == blb::auth::account_type::microsoft; })) {
            return "先登录一个微软账户才能添加离线账户";
        }
        if (name.empty()) {
            return "请输入玩家名";
        }
        if (!blb::auth::valid_player_name(name)) {
            return "玩家名只能用 3 到 16 个字母、数字或下划线";
        }
        auto account = blb::auth::offline_account(name);
        if (!account) {
            return account.err().message;
        }
        put(*account, true);
        return {};
    }

    Login login() {
        return (Login) loginState.load();
    }

    void startLogin() {
        if (login() != Login::Idle) {
            return;
        }
        auto redirect = std::make_shared<Redirect>();
        auto opened = redirect->open(MicrosoftClients::REDIRECT_PORT);
        if (!opened) {
            notify("无法开始登录：" + opened.err().message, true);
            return;
        }
        blb::auth::microsoft_client client = MicrosoftClients::client(App::Launcher);
        std::string state = blb::random_bytes_hex(16);
        blb::auth::microsoft_authenticator authenticator(client);
        if (!Platform::openUrl(
                    authenticator.authorization_url(MicrosoftClients::REDIRECT_URI, state, blb::auth::pkce_pair{}))) {
            notify("无法打开浏览器", true);
            return;
        }
        loginState = (int) Login::Browser;
        loginCancel = std::make_shared<blb::cancellation_source>();
        blb::cancellation_token token = loginCancel->token();
        run(
                [redirect, client, state, token](blb::cancellation_token) -> Continuation {
                    auto query = redirect->wait(MicrosoftClients::REDIRECT_PATH, token, LOGIN_TIMEOUT, resultPage);
                    redirect->close();
                    if (!query) {
                        return loginFailed(query.err());
                    }
                    auto value = [&](const char* name) {
                        auto found = query->find(name);
                        return found != query->end() ? found->second : std::string();
                    };
                    if (!value("error").empty()) {
                        std::string detail = value("error_description");
                        return loginFailed(blb::make_error(value("error") == "access_denied" ? blb::errc::auth_declined
                                                                                             : blb::errc::auth,
                                                           detail.empty() ? value("error") : detail));
                    }
                    if (value("state") != state || value("code").empty()) {
                        return loginFailed(blb::make_error(blb::errc::auth, "登录页面返回的内容无效"));
                    }
                    loginState = (int) Login::SigningIn;
                    blb::auth::microsoft_authenticator authenticator(client);
                    auto tokens = authenticator.exchange_code(value("code"), MicrosoftClients::REDIRECT_URI,
                                                              blb::auth::pkce_pair{}, token);
                    if (!tokens) {
                        return loginFailed(tokens.err());
                    }
                    auto account = authenticator.login(*tokens, token);
                    if (!account) {
                        return loginFailed(account.err());
                    }
                    blb::auth::account signedIn = *account;
                    return [signedIn] {
                        loginState = (int) Login::Idle;
                        put(signedIn, true);
                        notify("已登录 " + signedIn.name, false);
                    };
                },
                [](const std::string& message) -> Continuation {
                    return loginFailed(blb::make_error(blb::errc::auth, message));
                });
    }

    void cancelLogin() {
        if (loginCancel) {
            loginCancel->cancel();
        }
    }

    ServerLogin serverLogin() {
        return serverState;
    }

    void startServerLogin(blb::auth::account_type type, const std::string& server, const std::string& username,
                          const std::string& password) {
        profiles.clear();
        pendingBase = blb::auth::account();
        blb::cancellation_token token = beginServerRun();
        int current = serverRun;
        run(
                [type, server, username, password, token, current](blb::cancellation_token) -> Continuation {
                    auto session = blb::auth::yggdrasil_login(type, server, username, password, token);
                    if (!session) {
                        return serverFailed(current, session.err());
                    }
                    if (session->ready()) {
                        return serverSucceeded(current, session->base);
                    }
                    blb::auth::yggdrasil_session waiting = *session;
                    return [current, waiting] {
                        if (current != serverRun || serverState != ServerLogin::Working) {
                            return;
                        }
                        pendingBase = waiting.base;
                        profiles = waiting.profiles;
                        serverState = ServerLogin::Choosing;
                    };
                },
                [current](const std::string& message) -> Continuation {
                    return serverFailed(current, blb::make_error(blb::errc::auth, message));
                });
    }

    const std::vector<blb::auth::yggdrasil_profile>& serverProfiles() {
        return profiles;
    }

    void chooseProfile(size_t index) {
        if (serverState != ServerLogin::Choosing || index >= profiles.size()) {
            return;
        }
        blb::auth::account base = pendingBase;
        blb::auth::yggdrasil_profile profile = profiles[index];
        blb::cancellation_token token = beginServerRun();
        int current = serverRun;
        run(
                [base, profile, token, current](blb::cancellation_token) -> Continuation {
                    auto chosen = blb::auth::yggdrasil_select(base, profile, token);
                    if (!chosen) {
                        return serverFailed(current, chosen.err());
                    }
                    return serverSucceeded(current, *chosen);
                },
                [current](const std::string& message) -> Continuation {
                    return serverFailed(current, blb::make_error(blb::errc::auth, message));
                });
    }

    void cancelServerLogin() {
        if (serverCancel) {
            serverCancel->cancel();
        }
        serverRun++;
        serverState = ServerLogin::Idle;
        profiles.clear();
        pendingBase = blb::auth::account();
    }

    void finishServerLogin() {
        if (serverState == ServerLogin::Done) {
            serverState = ServerLogin::Idle;
        }
    }

    std::string kindOf(const blb::auth::account& account) {
        switch (account.type) {
            case blb::auth::account_type::microsoft: return "微软";
            case blb::auth::account_type::yggdrasil:
                return account.server_name.empty() ? std::string("第三方") : account.server_name;
            case blb::auth::account_type::nide8: return "统一通行证";
            default: return "离线";
        }
    }

    const blb::auth::account* pick(const std::string& login, const std::string& server) {
        const blb::auth::account* chosen = current();
        if (chosen == nullptr || matches(*chosen, login, server)) {
            return chosen;
        }
        for (const blb::auth::account& account : accounts) {
            if (matches(account, login, server)) {
                return &account;
            }
        }
        return nullptr;
    }

    const blb::auth::account* pick(const std::string& login, const std::string& server, const std::string& preferred) {
        const blb::auth::account* wanted = preferred.empty() ? nullptr : find(preferred);
        if (wanted != nullptr && matches(*wanted, login, server)) {
            return wanted;
        }
        return pick(login, server);
    }

    bool suits(const blb::auth::account& account, const std::string& login, const std::string& server) {
        return matches(account, login, server);
    }

    std::string requirement(const std::string& login, const std::string& server) {
        std::string wanted(blb::trim(server));
        if (login == "microsoft") {
            return "这个实例要求使用微软账户";
        }
        if (login == "offline") {
            return "这个实例要求使用离线账户";
        }
        if (login == "yggdrasil") {
            return wanted.empty() ? std::string("这个实例要求使用第三方账户")
                                  : "这个实例要求使用 " + wanted + " 的第三方账户";
        }
        if (login == "nide8") {
            return "这个实例要求使用统一通行证账户";
        }
        return "先添加一个账户再启动";
    }

    void refresh(const std::string& key) {
        const blb::auth::account* account = find(key);
        if (account == nullptr || busy(key)) {
            return;
        }
        bool yggdrasil = blb::auth::is_yggdrasil(account->type);
        if (!yggdrasil && account->type != blb::auth::account_type::microsoft) {
            return;
        }
        busyKeys.insert(key);
        blb::auth::account saved = *account;
        blb::auth::microsoft_client client = MicrosoftClients::client(MicrosoftClients::of(saved.client_id));
        run(
                [key, saved, client, yggdrasil](blb::cancellation_token token) -> Continuation {
                    blb::auth::microsoft_authenticator authenticator(client);
                    auto result = yggdrasil ? blb::auth::yggdrasil_refresh(saved, token)
                                            : authenticator.refresh(saved, token);
                    if (!result) {
                        blb::error error = result.err();
                        return [key, error] {
                            busyKeys.erase(key);
                            if (error.code == blb::errc::cancelled) {
                                return;
                            }
                            if (error.code == blb::errc::auth_expired) {
                                expiredKeys.insert(key);
                            }
                            notify("刷新失败：" + describe(error), true);
                        };
                    }
                    blb::auth::account fresh = *result;
                    return [key, fresh] {
                        busyKeys.erase(key);
                        if (find(key) != nullptr) {
                            put(fresh, false);
                            notify("已刷新 " + fresh.name, false);
                        }
                    };
                },
                [key](const std::string& message) -> Continuation {
                    return [key, message] {
                        busyKeys.erase(key);
                        notify("刷新失败：" + message, true);
                    };
                });
    }

    bool busy(const std::string& key) {
        return busyKeys.count(key) != 0;
    }

    bool expired(const std::string& key) {
        return expiredKeys.count(key) != 0;
    }

    const std::vector<Found>& found() {
        if (!scanStarted) {
            rescan();
        }
        return foundList;
    }

    bool scanning() {
        return scanRunning;
    }

    void rescan() {
        if (scanRunning) {
            return;
        }
        scanStarted = true;
        scanRunning = true;
        run(
                [](blb::cancellation_token) -> Continuation {
                    std::vector<Migrators::Found> sources = Migrators::scan();
                    return [sources] {
                        std::vector<Found> next;
                        for (const Migrators::Found& source : sources) {
                            Found entry{source, Found::State::Ready};
                            for (const Found& before : foundList) {
                                if (before.source.refreshToken == source.refreshToken) {
                                    entry.state = before.state;
                                }
                            }
                            next.push_back(entry);
                        }
                        foundList = std::move(next);
                        scanRunning = false;
                    };
                },
                [](const std::string&) -> Continuation { return [] { scanRunning = false; }; });
    }

    void import(size_t index) {
        if (index >= foundList.size() || foundList[index].state == Found::State::Importing ||
            foundList[index].state == Found::State::Imported) {
            return;
        }
        Found& entry = foundList[index];
        entry.state = Found::State::Importing;
        std::string refreshToken = entry.source.refreshToken;
        std::string launcher = entry.source.launcher;
        blb::auth::microsoft_client client = MicrosoftClients::client(entry.source.app);
        run(
                [refreshToken, launcher, client](blb::cancellation_token token) -> Continuation {
                    blb::auth::account seed;
                    seed.type = blb::auth::account_type::microsoft;
                    seed.refresh_token = refreshToken;
                    blb::auth::microsoft_authenticator authenticator(client);
                    auto result = authenticator.refresh(seed, token);
                    auto mark = [refreshToken](Found::State state) {
                        for (Found& found : foundList) {
                            if (found.source.refreshToken == refreshToken) {
                                found.state = state;
                            }
                        }
                    };
                    if (!result) {
                        blb::error error = result.err();
                        return [mark, error, launcher] {
                            mark(Found::State::Failed);
                            if (error.code != blb::errc::cancelled) {
                                notify("无法导入 " + launcher + " 的账户：" + describe(error), true);
                            }
                        };
                    }
                    blb::auth::account imported = *result;
                    return [mark, imported] {
                        mark(Found::State::Imported);
                        std::string key = keyOf(imported);
                        if (find(key) != nullptr && !expired(key)) {
                            notify(imported.name + " 已经在账户列表里", false);
                            return;
                        }
                        put(imported, find(key) == nullptr && accounts.empty());
                        notify("已导入 " + imported.name, false);
                    };
                },
                [refreshToken](const std::string&) -> Continuation {
                    return [refreshToken] {
                        for (Found& found : foundList) {
                            if (found.source.refreshToken == refreshToken) {
                                found.state = Found::State::Failed;
                            }
                        }
                    };
                });
    }

    std::shared_ptr<const Pixels> face(const blb::auth::account& account) {
        if (account.type == blb::auth::account_type::offline || account.skin_url.empty()) {
            return nullptr;
        }
        Face& entry = faces[account.skin_url];
        if (!entry.requested) {
            entry.requested = true;
            fetchFace(account.skin_url);
        }
        return entry.pixels;
    }

    std::shared_ptr<blb::auth::microsoft_authenticator> authenticator(const blb::auth::account& account) {
        if (account.type != blb::auth::account_type::microsoft) {
            return nullptr;
        }
        return std::make_shared<blb::auth::microsoft_authenticator>(
                MicrosoftClients::client(MicrosoftClients::of(account.client_id)));
    }

    void refreshed(const blb::auth::account& account) {
        if (find(keyOf(account)) != nullptr) {
            put(account, false);
        }
    }

    void markExpired(const std::string& key) {
        if (find(key) != nullptr) {
            expiredKeys.insert(key);
        }
    }

    std::string describe(const blb::error& error) {
        switch (error.code) {
            case blb::errc::cancelled: return "已取消";
            case blb::errc::auth_declined: return "没有同意授权";
            case blb::errc::auth_expired: return "登录已失效，请重新登录";
            case blb::errc::no_xbox_account: return "这个微软账户还没有 Xbox 档案，先在 xbox.com 登录一次";
            case blb::errc::child_account: return "未成年账户需要先加入微软家庭组";
            case blb::errc::no_minecraft_profile: return "这个微软账户没有 Minecraft";
            case blb::errc::network: return "网络错误：" + error.message;
            default: return error.message;
        }
    }
}

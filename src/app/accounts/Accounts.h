#pragma once

#include "app/accounts/Migrators.h"
#include "app/system/Pixels.h"

#include <core/auth/account.hpp>
#include <core/auth/microsoft.hpp>
#include <core/auth/yggdrasil.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Accounts {

    namespace blb = b_launcher_base;

    void init();
    void poll();
    void shutdown();

    std::string keyOf(const blb::auth::account& account);
    const std::vector<blb::auth::account>& list();
    const blb::auth::account* find(const std::string& key);
    const blb::auth::account* current();
    void select(const std::string& key);
    void remove(const std::string& key);
    std::string addOffline(const std::string& name);

    enum class Login { Idle, Browser, SigningIn };
    Login login();
    void startLogin();
    void cancelLogin();

    // Shared form rules; callers keep their input normalization and notice presentation.
    std::string serverLoginProblem(blb::auth::account_type type, const std::string& server,
                                   const std::string& username, const std::string& password);
    std::string registrationUrl(blb::auth::account_type type, const std::string& server);

    struct Credential {
        std::string username;
        std::string password;
    };

    std::optional<Credential> rememberedLogin(blb::auth::account_type type, const std::string& server);
    std::optional<std::string> loadSealed(const std::string& name);
    bool storeSealed(const std::string& name, const std::string& data);
    std::string updateOffline(const std::string& key, const std::string& name, const std::string& uuid,
                              std::string& updatedKey);
    void rememberLogin(blb::auth::account_type type, const std::string& server, const std::string& username,
                       const std::string& password);
    void forgetLogin(blb::auth::account_type type, const std::string& server);

    enum class ServerLogin { Idle, Working, Choosing, Done };
    ServerLogin serverLogin();
    void startServerLogin(blb::auth::account_type type, const std::string& server, const std::string& username,
                          const std::string& password);
    const std::vector<blb::auth::yggdrasil_profile>& serverProfiles();
    blb::auth::account serverCandidate(size_t index);
    void chooseProfile(size_t index);
    void chooseProfiles(const std::vector<size_t>& indices, const std::string& username, const std::string& password);
    void cancelServerLogin();
    void finishServerLogin();

    std::string kindOf(const blb::auth::account& account);
    const blb::auth::account* pick(const std::string& login, const std::string& server);
    const blb::auth::account* pick(const std::string& login, const std::string& server, const std::string& preferred);
    bool matches(const blb::auth::account& account, const std::string& login, const std::string& server);
    std::string requirement(const std::string& login, const std::string& server);

    void refresh(const std::string& key);
    bool busy(const std::string& key);
    bool expired(const std::string& key);

    struct Found {
        enum class State { Ready, Importing, Imported, Failed };

        Migrators::Found source;
        State state = State::Ready;
    };
    const std::vector<Found>& found();
    bool scanning();
    void rescan();
    void import(size_t index);

    std::shared_ptr<const Pixels> face(const blb::auth::account& account);

    std::shared_ptr<blb::auth::microsoft_authenticator> authenticator(const blb::auth::account& account);
    void refreshed(const blb::auth::account& account);
    void markExpired(const std::string& key);
    std::string describe(const blb::error& error);
}

#pragma once

#include "app/system/Pixels.h"

#include <core/auth/account.hpp>
#include <core/base/cancellation.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LittleSkin {

    enum class Status { Unauthorized, Authorizing, Loading, Ready, Failed };

    struct Texture {
        long long tid = 0;
        std::string name;
        std::string hash;
        bool slim = false;
    };

    struct State {
        Status status = Status::Unauthorized;
        std::string error;
        long long pid = 0;
        std::string player;
        long long skin = 0;
        long long cape = 0;
        std::vector<Texture> skins;
        std::vector<Texture> capes;
        std::string working;
    };

    struct Grant {
        std::string access;
        std::string refresh;
    };

    struct Consent;

    std::shared_ptr<Consent> askConsent(std::string& error);
    std::optional<Grant> awaitConsent(const std::shared_ptr<Consent>& consent,
                                      b_launcher_base::cancellation_token cancel, std::string& error);
    void adopt(const std::string& key, const Grant& grant);

    bool supports(const b_launcher_base::auth::account& account);
    const State& of(const std::string& key);
    void authorize(const std::string& key);
    void cancel();
    void reload(const std::string& key);
    void rename(const std::string& key, const std::string& name);
    void wear(const std::string& key, bool cape, long long tid);
    void signOut(const std::string& key);
    std::string textureUrl(const std::string& hash);
    std::string uploadUrl();
    std::shared_ptr<const Pixels> preview(const Texture& texture);
    void poll();
    void shutdown();
}

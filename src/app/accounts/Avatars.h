#pragma once

#include "app/system/Pixels.h"

#include <core/auth/account.hpp>

#include <memory>
#include <string>

namespace Avatars {

    enum class Style { Bust, Head };

    Style style();
    void setStyle(Style style);
    std::shared_ptr<const Pixels> bust(const b_launcher_base::auth::account& account);
    std::shared_ptr<const Pixels> head(const b_launcher_base::auth::account& account);
    void refresh(const std::string& key);
    void poll();
    void shutdown();
}

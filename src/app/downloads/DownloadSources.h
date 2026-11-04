#pragma once

#include <core/network/mirror_provider.hpp>

#include <memory>

namespace DownloadSources {

    enum class Kind { Minecraft, Assets, Modrinth, CurseForge };

    b_launcher_base::net::source_order order(Kind kind);
    void setOrder(Kind kind, b_launcher_base::net::source_order value);

    std::shared_ptr<b_launcher_base::net::mirror_provider> provider();
    void apply();
}

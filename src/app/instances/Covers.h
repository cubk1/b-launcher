#pragma once

#include "app/system/Pixels.h"

#include <core/meta/version_manifest.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Covers {

    std::string keyOf(const b_launcher_base::meta::manifest_version& version, std::string& upcoming);
    std::string keyOf(const std::string& minecraft);
    bool ready(const std::vector<std::string>& keys, int height);
    std::shared_ptr<const Pixels> thumb(const std::string& key, int height);
    std::shared_ptr<const Pixels> icon(const std::string& name, int side, bool dark);
    std::shared_ptr<const Pixels> logo(const std::string& name, int side, bool dark);
    std::shared_ptr<const Pixels> get(const std::string& key);
    void poll();
    void shutdown();
}

#pragma once

#include "app/instances/Mods.h"

#include <filesystem>
#include <string>

namespace ModVersionPicker {

    void open(const std::string& key, const std::filesystem::path& file);
    bool list(const std::string& id, const std::string& key, const Mods::Mod& mod, float x, float y, float w, float h,
              bool blocked);
}

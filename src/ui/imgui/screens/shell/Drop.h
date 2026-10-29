#pragma once

#include <filesystem>

namespace Drop {

    void attach(void* window);
    void detach();
    void draw(float width, float height);
    void poll();
    void importPack(const std::filesystem::path& file);
    void shutdown();
}

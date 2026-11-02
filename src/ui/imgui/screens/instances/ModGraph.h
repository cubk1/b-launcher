#pragma once

#include "app/instances/Instances.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ModGraph {

    struct Area {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct Row {
        std::filesystem::path file;
        Area area;
    };

    struct Frame {
        Area clip;
        Area layout;
        float chrome = 1.0f;
        float grid = 1.0f;
        float nodes = 1.0f;
        float morph = 1.0f;
        bool focused = false;
        float focusX = 0.0f;
        float focusY = 0.0f;
        std::vector<Row> rows;
        float top = 0.0f;
        float bottom = 0.0f;
        float inputTop = 0.0f;
        std::vector<Area> holes;
    };

    void draw(const Instances::Entry& entry, const std::string& query, const Frame& frame, bool blocked);
    void pin(const std::string& key, const std::vector<std::filesystem::path>& files, float x, float y);
    void preview(const std::string& key, const std::string& label, float x, float y);
}

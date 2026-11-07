#pragma once

#include "app/instances/Instances.h"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
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

    enum class Tone { Normal, Root, Missing, Muted };
    enum class Link { Required, Optional, Integration, Incompatible };

    struct Item {
        std::string id;
        std::string name;
        std::string version;
        std::string iconUrl;
        std::string group;
        bool head = false;
        Tone tone = Tone::Normal;
        std::string about;
        std::vector<std::pair<std::string, std::string>> rows;
        bool openable = false;
    };

    struct Relation {
        std::string from;
        std::string to;
        Link link = Link::Required;
    };

    struct Model {
        std::string signature;
        std::vector<Item> items;
        std::vector<Relation> relations;
        bool grouped = false;
        std::function<void(const std::string& id)> open;
    };

    void draw(const Instances::Entry& entry, const std::string& query, const Frame& frame, bool blocked);
    void drawModel(const std::string& key, const Model& model, const std::string& query, const Frame& frame,
                   bool blocked);
    void pin(const std::string& key, const std::vector<std::filesystem::path>& files, float x, float y);
    void preview(const std::string& key, const std::string& label, float x, float y);
}

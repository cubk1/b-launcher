#pragma once

#include "app/instances/FileTree.h"

#include <functional>
#include <string>

namespace FileTreeView {

    struct Tag {
        std::string text;
        bool strong = false;
    };

    using Tagger = std::function<Tag(const FileTree::Node&)>;

    Tag purpose(const FileTree::Node& node);
    float height(FileTree::Tree& tree);
    bool draw(const std::string& id, FileTree::Tree& tree, FileTree::Rules& rules, float x, float y, float w,
              bool blocked, const Tagger& tagger, std::string* toggled = nullptr);
}

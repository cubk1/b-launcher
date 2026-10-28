#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Xaml {

    struct Node {
        std::string name;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<Node> children;
        std::string text;
        int line = 0;

        bool isText() const;
        const std::string* attribute(std::string_view key) const;
    };

    struct Document {
        std::vector<Node> roots;
        std::string error;
        int errorLine = 0;
    };

    Document parse(std::string_view source);
}

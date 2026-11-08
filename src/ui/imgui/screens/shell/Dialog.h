#pragma once

#include <functional>
#include <string>

namespace Dialog {

    using Body = std::function<void(float x, float y, float w, float h)>;

    void open(const std::string& id, float width, float height, Body body,
              std::function<void()> closed = std::function<void()>());
    void close();
    void resize(const std::string& id, float width, float height);
    bool isOpen(const std::string& id);
    bool active();
    void closeAbove();
}

#pragma once

#include <string>

class Widget {
public:
    std::string id;
    float x;
    float y;
    float w;
    float h;
    float hitX;
    float hitY;
    float hitW;
    float hitH;
    bool blocked;
    // Set by clicked(): the press landed on this widget in this frame. The click itself comes on release.
    bool pressStarted;

    explicit Widget(const std::string& id);
    virtual ~Widget() = default;

    Widget& bounds(float x, float y, float w, float h);
    Widget& hitArea(float x, float y, float w, float h);
    Widget& block(bool blocked);
    bool hovered() const;
    bool clicked();
    virtual void paint();
};

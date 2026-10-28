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

    explicit Widget(const std::string& id);
    virtual ~Widget() = default;

    Widget& bounds(float x, float y, float w, float h);
    Widget& hitArea(float x, float y, float w, float h);
    Widget& block(bool blocked);
    bool hovered() const;
    bool clicked();
    virtual void paint();
};

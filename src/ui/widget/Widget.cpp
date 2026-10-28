#include "Widget.h"
#include "../panel/NewUi.h"

Widget::Widget(const std::string& id)
    : id(id),
      x(0.0f),
      y(0.0f),
      w(0.0f),
      h(0.0f),
      hitX(0.0f),
      hitY(0.0f),
      hitW(0.0f),
      hitH(0.0f),
      blocked(false) {}

Widget& Widget::bounds(float x, float y, float w, float h) {
    this->x = x;
    this->y = y;
    this->w = w;
    this->h = h;
    hitX = x;
    hitY = y;
    hitW = w;
    hitH = h;
    return *this;
}

Widget& Widget::hitArea(float x, float y, float w, float h) {
    hitX = x;
    hitY = y;
    hitW = w;
    hitH = h;
    return *this;
}

Widget& Widget::block(bool blocked) {
    this->blocked = blocked;
    return *this;
}

bool Widget::hovered() const {
    return !blocked && NewUi::hit(hitX, hitY, hitW, hitH);
}

bool Widget::clicked() {
    return !blocked && NewUi::click(hitX, hitY, hitW, hitH);
}

void Widget::paint() {}

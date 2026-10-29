#include "BindWidget.h"
#include "../panel/NewUi.h"
#include "../panel/UiKeys.h"
#include "../gl/RenderUtil.h"
#include "../panel/UiFont.h"
#include "../panel/UiRender.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

#include <algorithm>

BindWidget::BindWidget(std::shared_ptr<KeyValue> value) : Widget(value->owner), value(std::move(value)), right(0.0f) {}

BindWidget& BindWidget::row(float x, float y, float w, float h, float right) {
    bounds(x, y, w, h);
    this->right = right;
    return *this;
}

bool BindWidget::listening() const {
    return NewUi::bindingKey != nullptr && NewUi::bindingKey->owner == id;
}

void BindWidget::paint() {
    if (clicked()) {
        NewUi::bindingKey = listening() ? nullptr : value;
    }
    bool capturing = listening();
    int key = value->get();
    std::string keyText = capturing ? "..." : UiKeys::isValidBind(key) ? RenderUtil::keyName(key) : "无";
    float keyW = UiFont::width(keyText, UiFont::MEDIUM, UiWidgets::TEXT_PILL);
    float boxW = std::max(22.0f, keyW + 10);
    float boxH = 18.0f;
    float boxY = y + (h - boxH) * 0.5f;
    UiRender::roundRect(right - boxW, boxY, boxW, boxH, 5.0f, UiTheme::hover());
    UiRender::textY(keyText, right - boxW + (boxW - keyW) * 0.5f, boxY, boxH,
                    capturing ? UiTheme::accent() : UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_PILL);
}

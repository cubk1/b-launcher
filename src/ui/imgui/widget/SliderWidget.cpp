#include "SliderWidget.h"
#include "../panel/NewUi.h"
#include "../panel/UiInput.h"
#include "../panel/UiRender.h"
#include "../panel/UiWidgets.h"

#include <algorithm>

SliderWidget::SliderWidget(const std::string& id, std::shared_ptr<NumberValue> value)
    : Widget(id),
      value(std::move(value)),
      pillRight(0.0f),
      compact(false) {}

SliderWidget& SliderWidget::compactTrack(float trackX, float trackY, float trackW) {
    bounds(trackX, trackY, trackW, 4.0f);
    compact = true;
    return *this;
}

SliderWidget& SliderWidget::track(float trackX, float trackY, float trackW, float pillRight) {
    bounds(trackX, trackY, trackW, 3.0f);
    this->pillRight = pillRight;
    return *this;
}

std::string SliderWidget::text() {
    return NewUi::formatStep(value->get(), value->step());
}

void SliderWidget::paintCompact() {
    double min = value->min();
    double max = value->max();
    bool sliderHover = !blocked && NewUi::hit(x - 6, y - 8, w + 12, 20);
    if (sliderHover && NewUi::pendingClick && !NewUi::clickConsumed) {
        NewUi::clickConsumed = true;
        NewUi::draggingSlider = id;
    }
    bool active = id == NewUi::draggingSlider;
    if (active) {
        value->apply(min + (max - min) * UiRender::saturate((NewUi::mouseX - x) / w));
    }
    if (sliderHover) {
        NewUi::hoveredSlider = value;
    }
    float amount = max - min <= 0 ? 0.0f : (float) ((value->get() - min) / (max - min));
    UiWidgets::subSlider("subprop#" + id, x, y, w, amount, sliderHover, active, text() + value->suffix());
}

void SliderWidget::paint() {
    if (compact) {
        paintCompact();
        return;
    }
    double min = value->min();
    double max = value->max();
    bool editing = id == NewUi::sliderEditing;
    std::string pillText = editing ? UiInput::get(NewUi::INPUT_SLIDER) : text() + value->suffix();
    float pillW = UiWidgets::pillWidth(pillText);
    float trackW = this->w - std::max(0.0f, pillW - 43.0f);
    bool sliderHover = NewUi::hit(x - 6, y - 9, trackW + 12, 21);
    if (sliderHover && NewUi::pendingClick && !NewUi::clickConsumed) {
        NewUi::clickConsumed = true;
        NewUi::draggingSlider = id;
    }
    bool active = id == NewUi::draggingSlider;
    if (active) {
        value->apply(min + (max - min) * UiRender::saturate((NewUi::mouseX - x) / trackW));
    }
    float amount = max - min <= 0 ? 0.0f : (float) ((value->get() - min) / (max - min));
    if (!editing && NewUi::click(pillRight - pillW, y - 10, pillW, 21)) {
        NewUi::beginSliderEdit(id, text());
        NewUi::sliderEditValue = value;
        editing = id == NewUi::sliderEditing;
        pillText = editing ? UiInput::get(NewUi::INPUT_SLIDER) : text() + value->suffix();
    }
    UiWidgets::slider("slider#" + id, x, y, trackW, amount, pillText, pillRight + 13, sliderHover, active, editing);
}

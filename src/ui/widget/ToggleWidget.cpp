#include "ToggleWidget.h"
#include "../panel/UiSound.h"
#include "../panel/UiWidgets.h"

ToggleWidget::ToggleWidget(const std::string& id, std::shared_ptr<Flag> flag, bool compact)
    : Widget(id),
      flag(std::move(flag)),
      compact(compact) {}

void ToggleWidget::paint() {
    bool isHovered = hovered();
    if (clicked()) {
        UiSound::request(UiSound::TICK);
        flag->flip();
    }
    if (compact) {
        UiWidgets::subToggle(id, x, y, flag->isOn());
    } else {
        UiWidgets::toggle(id, x, y, flag->isOn(), true, isHovered);
    }
}

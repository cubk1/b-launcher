#include "Overlay.h"
#include "../panel/NewUi.h"
#include "../value/Values.h"

#include <algorithm>

Overlay::Overlay()
    : host(nullptr),
      marked(false),
      markX(0.0f),
      markY(0.0f),
      markW(0.0f),
      markH(0.0f),
      shown(false),
      shownX(0.0f),
      shownY(0.0f),
      shownW(0.0f),
      shownH(0.0f) {}

bool Overlay::isOpen() {
    return false;
}

void Overlay::release() {}

void Overlay::draw() {}

bool Overlay::scroll(double amount) {
    (void) amount;
    return false;
}

bool Overlay::stepBack() {
    return false;
}

bool Overlay::bounded() {
    return marked;
}

bool Overlay::contains(float x, float y) {
    return marked && x >= markX && x <= markX + markW && y >= markY && y <= markY + markH;
}

void Overlay::mark(float x, float y, float w, float h) {
    if (!marked) {
        marked = true;
        markX = x;
        markY = y;
        markW = w;
        markH = h;
        return;
    }
    float x0 = std::min(markX, x);
    float y0 = std::min(markY, y);
    float x1 = std::max(markX + markW, x + w);
    float y1 = std::max(markY + markH, y + h);
    markX = x0;
    markY = y0;
    markW = x1 - x0;
    markH = y1 - y0;
}

void Overlay::clearMark() {
    shown = marked;
    shownX = markX;
    shownY = markY;
    shownW = markW;
    shownH = markH;
    marked = false;
}

bool Overlay::shownAt(float x, float y) const {
    return shown && x >= shownX && x <= shownX + shownW && y >= shownY && y <= shownY + shownH;
}

bool SubOverlay::isOpen() {
    return !NewUi::subOwner.empty();
}

void SubOverlay::release() {
    NewUi::subInlinePicker.clear();
    NewUi::subOwner.clear();
    NewUi::subParent.clear();
}

void SubOverlay::draw() {
    NewUi::subLayer();
}

bool SubOverlay::stepBack() {
    if (NewUi::subOwner.empty() || NewUi::subParent.empty()) {
        return false;
    }
    NewUi::subInlinePicker.erase(NewUi::subOwner);
    NewUi::subOwner = NewUi::subParent;
    NewUi::subAnchorX = NewUi::subParentX;
    NewUi::subAnchorY = NewUi::subParentY;
    NewUi::subParent.clear();
    return true;
}

bool SubOverlay::scroll(double amount) {
    if (NewUi::hoveredSlider != nullptr) {
        NewUi::hoveredSlider->nudge(amount);
    }
    return true;
}

bool MenuOverlay::isOpen() {
    return !NewUi::menuOwner.empty();
}

void MenuOverlay::release() {
    NewUi::menuOwner.clear();
}

void MenuOverlay::draw() {
    NewUi::menuLayer();
}

bool PopupOverlay::isOpen() {
    return NewUi::popupOpen;
}

void PopupOverlay::release() {
    NewUi::popupOpen = false;
    NewUi::popupDragFrom = -1;
}

void PopupOverlay::draw() {
    NewUi::popupLayer();
}

bool PopupOverlay::scroll(double amount) {
    NewUi::popupScroll = NewUi::popupScroll - (float) amount * 32.0f;
    return true;
}

bool PickerOverlay::isOpen() {
    return !NewUi::pickerOwner.empty();
}

void PickerOverlay::release() {
    if (!NewUi::pickerOwner.empty() && NewUi::pickerOwner == NewUi::pickerModeOwner) {
        NewUi::pickerModeOwner.clear();
    }
    NewUi::pickerOwner.clear();
    NewUi::pickerColor = nullptr;
}

void PickerOverlay::draw() {
    NewUi::colorPickerLayer();
}

bool PickerOverlay::scroll(double amount) {
    (void) amount;
    return true;
}

bool SwatchOverlay::isOpen() {
    return !NewUi::swatchMenuOwner.empty();
}

void SwatchOverlay::release() {
    NewUi::swatchMenuOwner.clear();
}

void SwatchOverlay::draw() {
    NewUi::swatchMenuLayer();
}

bool FieldMenuOverlay::isOpen() {
    return !NewUi::fieldMenuTarget.empty();
}

void FieldMenuOverlay::release() {
    NewUi::fieldMenuTarget.clear();
}

void FieldMenuOverlay::draw() {
    NewUi::fieldMenuLayer();
}

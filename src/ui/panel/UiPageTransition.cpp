#include "UiPageTransition.h"
#include "NewUi.h"
#include "UiRender.h"

#include <cmath>

PageTransition::PageTransition(int page) : page(page), mix(1.0f), savedAlpha(1.0f), savedOffsetX(0.0f) {}

bool PageTransition::go(int next) {
    if (next == page) {
        return false;
    }
    page = next;
    mix = 0.0f;
    NewUi::closeAllOverlays();
    return true;
}

void PageTransition::begin() {
    mix = UiRender::lerp(mix, 1.0f, 1.0f - (float) std::exp(-15.0f * UiRender::deltaTime()));
    float eased = 1.0f - (float) std::pow(1.0f - mix, 3.0);
    savedAlpha = UiRender::alphaMul;
    savedOffsetX = UiRender::offsetX;
    UiRender::alphaMul = savedAlpha * mix;
    UiRender::offsetX = savedOffsetX + 9.0f * (1.0f - eased) * UiRender::uiScale;
}

void PageTransition::end() {
    UiRender::alphaMul = savedAlpha;
    UiRender::offsetX = savedOffsetX;
}

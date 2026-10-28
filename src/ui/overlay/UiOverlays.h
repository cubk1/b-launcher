#pragma once

#include "Overlay.h"

#include <vector>

namespace UiOverlays {

    extern SubOverlay* const sub;
    extern MenuOverlay* const menu;
    extern PopupOverlay* const popup;
    extern PickerOverlay* const picker;
    extern SwatchOverlay* const swatch;
    extern FieldMenuOverlay* const fieldMenu;

    extern std::vector<Overlay*> layers_;
    extern std::vector<Overlay*> stack;
    extern Overlay* drawing;

    std::vector<Overlay*>& layers();
    void addLayer(Overlay* layer);
    void addLayerBefore(Overlay* layer, Overlay* before);
    void beginFrame();
    void sync();
    Overlay* top();
    bool isEmpty();
    void open(Overlay* layer);
    void openOn(Overlay* host, Overlay* layer);
    void close(Overlay* layer);
    void closeFrom(int index);
    void closeAll();
    bool dismiss(float x, float y);
    bool scroll(double amount);
    void drawAll();
    void draw(Overlay* layer);
    Overlay* enter(Overlay* layer);
    void leave(Overlay* saved);
    bool blocks(float x, float y);
    void mark(float x, float y, float w, float h);
}

#include "UiOverlays.h"
#include "../panel/NewUi.h"

#include <algorithm>

namespace UiOverlays {

    static SubOverlay subInstance;
    static MenuOverlay menuInstance;
    static PopupOverlay popupInstance;
    static PickerOverlay pickerInstance;
    static SwatchOverlay swatchInstance;
    static FieldMenuOverlay fieldMenuInstance;

    SubOverlay* const sub = &subInstance;
    MenuOverlay* const menu = &menuInstance;
    PopupOverlay* const popup = &popupInstance;
    PickerOverlay* const picker = &pickerInstance;
    SwatchOverlay* const swatch = &swatchInstance;
    FieldMenuOverlay* const fieldMenu = &fieldMenuInstance;

    std::vector<Overlay*> layers_;
    std::vector<Overlay*> stack;
    Overlay* drawing = nullptr;

    static int indexOf(const std::vector<Overlay*>& list, Overlay* layer) {
        auto it = std::find(list.begin(), list.end(), layer);
        return it == list.end() ? -1 : (int) (it - list.begin());
    }

    std::vector<Overlay*>& layers() {
        if (layers_.empty()) {
            layers_.push_back(sub);
            layers_.push_back(menu);
            layers_.push_back(popup);
            layers_.push_back(picker);
            layers_.push_back(swatch);
            layers_.push_back(fieldMenu);
        }
        return layers_;
    }

    void addLayer(Overlay* layer) {
        std::vector<Overlay*>& all = layers();
        if (indexOf(all, layer) < 0) {
            all.push_back(layer);
        }
    }

    void addLayerBefore(Overlay* layer, Overlay* before) {
        std::vector<Overlay*>& all = layers();
        if (indexOf(all, layer) >= 0) {
            return;
        }
        int at = indexOf(all, before);
        all.insert(at < 0 ? all.end() : all.begin() + at, layer);
    }

    void beginFrame() {
        sync();
        std::vector<Overlay*>& all = layers();
        for (size_t i = 0; i < all.size(); i++) {
            all[i]->clearMark();
        }
    }

    void sync() {
        for (size_t i = 0; i < stack.size(); i++) {
            if (!stack[i]->isOpen()) {
                closeFrom((int) i);
                break;
            }
        }
        std::vector<Overlay*>& all = layers();
        for (size_t i = 0; i < all.size(); i++) {
            Overlay* layer = all[i];
            if (layer->isOpen() && indexOf(stack, layer) < 0) {
                layer->host = top();
                stack.push_back(layer);
            }
        }
    }

    Overlay* top() {
        return stack.empty() ? nullptr : stack.back();
    }

    bool isEmpty() {
        sync();
        return stack.empty();
    }

    void open(Overlay* layer) {
        openOn(drawing, layer);
    }

    void openOn(Overlay* host, Overlay* layer) {
        sync();
        int hostIndex = host == nullptr ? -1 : indexOf(stack, host);
        int own = indexOf(stack, layer);
        if (own >= 0 && own <= hostIndex) {
            closeFrom(own + 1);
            return;
        }
        closeFrom(hostIndex + 1);
        layer->host = hostIndex >= 0 ? host : nullptr;
        stack.push_back(layer);
    }

    void close(Overlay* layer) {
        int index = indexOf(stack, layer);
        if (index >= 0) {
            closeFrom(index);
        } else {
            layer->release();
        }
    }

    void closeFrom(int index) {
        while (index >= 0 && (int) stack.size() > index) {
            Overlay* layer = stack.back();
            stack.pop_back();
            layer->host = nullptr;
            layer->release();
        }
    }

    void closeAll() {
        closeFrom(0);
        std::vector<Overlay*>& all = layers();
        for (size_t i = 0; i < all.size(); i++) {
            all[i]->release();
        }
    }

    bool dismiss(float x, float y) {
        sync();
        if (stack.empty()) {
            return false;
        }
        for (int i = (int) stack.size() - 1; i >= 0; i--) {
            if (i >= (int) stack.size()) {
                continue;
            }
            Overlay* layer = stack[i];
            if (layer->contains(x, y)) {
                return true;
            }
            if (layer->stepBack()) {
                return true;
            }
            bool bounded = layer->bounded();
            closeFrom(i);
            if (!bounded) {
                return true;
            }
        }
        return true;
    }

    bool scroll(double amount) {
        sync();
        if (NewUi::scrollColor(amount)) {
            return true;
        }
        for (int i = (int) stack.size() - 1; i >= 0; i--) {
            if (stack[i]->scroll(amount)) {
                return true;
            }
        }
        return false;
    }

    void drawAll() {
        std::vector<Overlay*> all = layers();
        for (size_t i = 0; i < all.size(); i++) {
            draw(all[i]);
        }
    }

    void draw(Overlay* layer) {
        Overlay* saved = drawing;
        drawing = layer;
        layer->draw();
        drawing = saved;
    }

    Overlay* enter(Overlay* layer) {
        Overlay* saved = drawing;
        drawing = layer;
        return saved;
    }

    void leave(Overlay* saved) {
        drawing = saved;
    }

    bool blocks(float x, float y) {
        std::vector<Overlay*>& all = layers();
        int start = drawing == nullptr ? 0 : indexOf(all, drawing) + 1;
        for (int i = start; i < (int) all.size(); i++) {
            if (all[i]->shownAt(x, y)) {
                return true;
            }
        }
        return false;
    }

    void mark(float x, float y, float w, float h) {
        if (drawing != nullptr) {
            drawing->mark(x, y, w, h);
        }
    }
}

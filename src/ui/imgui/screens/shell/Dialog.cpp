#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"

#include "ui/imgui/overlay/UiOverlays.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <cmath>
#include <utility>

namespace Dialog {

    namespace {
        constexpr float RADIUS = 14.0f;
        constexpr float SHELL_RADIUS = 14.0f;
        constexpr float OPEN_SPEED = 14.0f;
        constexpr float CLOSE_SPEED = 20.0f;
        constexpr float GONE = 0.02f;
        constexpr float SMALLEST = 0.95f;
        const char* const MOTION = "dialog#open";

        void paint();

        class DialogOverlay final : public Overlay {
        public:
            std::string owner;
            float width = 0.0f;
            float height = 0.0f;
            Body body;
            std::function<void()> closed;
            Body fading;
            float fadeWidth = 0.0f;
            float fadeHeight = 0.0f;
            std::function<void()> faded;

            bool isOpen() override {
                return !owner.empty();
            }

            void release() override {
                if (owner.empty()) {
                    return;
                }
                fading = std::move(body);
                fadeWidth = width;
                fadeHeight = height;
                faded = std::move(closed);
                body = nullptr;
                closed = nullptr;
                owner.clear();
            }

            void settle() {
                std::function<void()> done = std::move(faded);
                faded = nullptr;
                fading = nullptr;
                if (done) {
                    done();
                }
            }

            void draw() override {
                paint();
            }

            bool bounded() override {
                return true;
            }
        };

        DialogOverlay layer;

        void paint() {
            bool closing = layer.owner.empty();
            if (closing ? !layer.fading : !layer.body) {
                return;
            }
            bool covered = !closing && UiOverlays::top() != &layer;
            if (covered && NewUi::pendingClick && !NewUi::clickConsumed &&
                !UiOverlays::blocks(NewUi::mouseX, NewUi::mouseY)) {
                UiOverlays::closeAbove(&layer);
                NewUi::clickConsumed = true;
            }
            float open = closing ? UiState::motion(MOTION, 0.0f, CLOSE_SPEED)
                                 : UiState::motionFrom(MOTION, 1.0f, OPEN_SPEED, 0.0f);
            if (closing && open < GONE) {
                UiState::remove(MOTION);
                layer.settle();
                return;
            }
            float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
            float windowW = MainWindow::width();
            float windowH = MainWindow::height();
            float w = closing ? layer.fadeWidth : layer.width;
            float h = closing ? layer.fadeHeight : layer.height;
            float x = std::round((windowW - w) * 0.5f);
            float y = std::round((windowH - h) * 0.5f);

            float savedAlpha = UiRender::alphaMul;
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            UiRender::alphaMul = savedAlpha * open;
            UiRender::roundRect(0.0f, 0.0f, windowW, windowH, SHELL_RADIUS, UiTheme::scrim());
            NewUi::markOverlayUnion(0.0f, 0.0f, windowW, windowH);
            NewUi::markAbove(0.0f, 0.0f, windowW, windowH);
            UiRender::scaleFactor = savedScale * UiRender::lerp(SMALLEST, 1.0f, eased);
            UiRender::pivotX = (x + w * 0.5f) * UiRender::uiScale;
            UiRender::pivotY = (y + h * 0.5f) * UiRender::uiScale;
            UiRender::shadow(x, y, w, h, RADIUS, 14.0f, 6.0f, UiTheme::shadow());
            UiWidgets::overlayPanel(x, y, w, h, RADIUS, UiTheme::surface(), 0xF, ThemePaintSpec::DIALOG);
            if (closing) {
                Body body = layer.fading;
                bool clicked = NewUi::pendingClick;
                bool released = NewUi::pendingRelease;
                NewUi::pendingClick = false;
                NewUi::pendingRelease = false;
                body(x, y, w, h);
                NewUi::pendingClick = clicked;
                NewUi::pendingRelease = released;
            } else {
                Body body = layer.body;
                body(x, y, w, h);
            }
            UiRender::alphaMul = savedAlpha;
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedPivotX;
            UiRender::pivotY = savedPivotY;

            if (layer.owner.empty()) {
                return;
            }
            bool inside = NewUi::rawHit(x, y, w, h) || NewUi::pressedIn(x, y, w, h);
            if (NewUi::click(0.0f, 0.0f, windowW, windowH) && !inside) {
                close();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (covered) {
                    UiOverlays::closeAbove(&layer);
                } else {
                    close();
                }
            }
        }
    }

    void open(const std::string& id, float width, float height, Body body, std::function<void()> closed) {
        UiOverlays::addLayerBefore(&layer, UiOverlays::sub);
        NewUi::closeAllOverlays();
        layer.settle();
        UiOverlays::openOn(nullptr, &layer);
        layer.owner = id;
        layer.width = width;
        layer.height = height;
        layer.body = std::move(body);
        layer.closed = std::move(closed);
        UiState::remove(MOTION);
    }

    void close() {
        UiOverlays::close(&layer);
        layer.release();
    }

    void resize(const std::string& id, float width, float height) {
        if (!layer.owner.empty() && layer.owner == id) {
            layer.width = width;
            layer.height = height;
        }
    }

    bool isOpen(const std::string& id) {
        return !layer.owner.empty() && layer.owner == id;
    }

    bool active() {
        return !layer.owner.empty();
    }

    void closeAbove() {
        UiOverlays::closeAbove(&layer);
    }
}

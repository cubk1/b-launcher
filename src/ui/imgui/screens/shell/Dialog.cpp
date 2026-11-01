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

        void paint();

        class DialogOverlay final : public Overlay {
        public:
            std::string owner;
            float width = 0.0f;
            float height = 0.0f;
            Body body;
            std::function<void()> closed;

            bool isOpen() override {
                return !owner.empty();
            }

            void release() override {
                std::function<void()> done = std::move(closed);
                closed = nullptr;
                owner.clear();
                body = nullptr;
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
            if (layer.owner.empty() || !layer.body) {
                return;
            }
            float open = UiState::motionFrom("dialog#open", 1.0f, 14.0f, 0.0f);
            float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
            float windowW = MainWindow::width();
            float windowH = MainWindow::height();
            float w = layer.width;
            float h = layer.height;
            float x = std::round((windowW - w) * 0.5f);
            float y = std::round((windowH - h) * 0.5f);

            float savedAlpha = UiRender::alphaMul;
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            UiRender::alphaMul = savedAlpha * open;
            UiRender::roundRect(0.0f, 0.0f, windowW, windowH, SHELL_RADIUS,
                                UiTheme::isLight() ? 0x47000000u : 0x80000000u);
            NewUi::markOverlayUnion(0.0f, 0.0f, windowW, windowH);
            NewUi::markAbove(0.0f, 0.0f, windowW, windowH);
            UiRender::scaleFactor = savedScale * UiRender::lerp(0.965f, 1.0f, eased);
            UiRender::pivotX = (x + w * 0.5f) * UiRender::uiScale;
            UiRender::pivotY = (y + h * 0.5f) * UiRender::uiScale;
            UiRender::shadow(x, y, w, h, RADIUS, 14.0f, 6.0f, UiTheme::shadow());
            UiWidgets::overlayPanel(x, y, w, h, RADIUS, UiTheme::surface(), 0xF);
            Body body = layer.body;
            body(x, y, w, h);
            UiRender::alphaMul = savedAlpha;
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedPivotX;
            UiRender::pivotY = savedPivotY;

            if (layer.owner.empty()) {
                return;
            }
            bool inside = NewUi::rawHit(x, y, w, h);
            if (NewUi::click(0.0f, 0.0f, windowW, windowH) && !inside) {
                close();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                close();
            }
        }
    }

    void open(const std::string& id, float width, float height, Body body, std::function<void()> closed) {
        UiOverlays::addLayerBefore(&layer, UiOverlays::sub);
        NewUi::closeAllOverlays();
        UiOverlays::openOn(nullptr, &layer);
        layer.owner = id;
        layer.width = width;
        layer.height = height;
        layer.body = std::move(body);
        layer.closed = std::move(closed);
        UiState::remove("dialog#open");
    }

    void close() {
        UiOverlays::close(&layer);
        layer.release();
    }

    bool isOpen(const std::string& id) {
        return !layer.owner.empty() && layer.owner == id;
    }
}

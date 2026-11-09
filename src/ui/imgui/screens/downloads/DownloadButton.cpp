#include "ui/imgui/screens/downloads/DownloadButton.h"
#include "app/system/Transfers.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/screens/launch/LaunchButton.h"
#include "ui/imgui/screens/shell/MainWindow.h"

#include "imgui.h"

#include <cstdint>
#include <string>

namespace DownloadButton {

    namespace {
        constexpr float RADIUS = 14.0f;
        constexpr float BADGE = 16.0f;

        float advance() {
            static int frame = -1;
            static float value = 0.0f;
            if (frame != ImGui::GetFrameCount()) {
                frame = ImGui::GetFrameCount();
                value = UiState::motion("downloads#floating", visible() ? 1.0f : 0.0f, 14.0f);
            }
            return value;
        }

        float overall() {
            float sum = 0.0f;
            int known = 0;
            for (const Transfers::Task& task : Transfers::list()) {
                if (task.state != Transfers::State::Running) {
                    continue;
                }
                float value = task.progress();
                if (value >= 0.0f) {
                    sum += value;
                    known++;
                }
            }
            return known > 0 ? sum / (float) known : -1.0f;
        }
    }

    bool visible() {
        return Transfers::running() > 0;
    }

    float shown() {
        return advance();
    }

    float inset() {
        return (SIZE + MARGIN - 8.0f) * advance();
    }

    void draw(float x, float y) {
        float shown = advance();
        if (shown < 0.01f) {
            return;
        }
        float cx = x + SIZE * 0.5f;
        float cy = y + SIZE * 0.5f;
        bool hovered = shown > 0.99f && !NewUi::overlayCoversMouse() && NewUi::hit(x, y, SIZE, SIZE);
        float hover = UiState::motion("downloads#floating#hover", hovered ? 1.0f : 0.0f, 18.0f);
        float press = UiState::motion("downloads#floating#press", hovered && NewUi::mouseDown ? 1.0f : 0.0f, 26.0f);

        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::pivotX = cx * UiRender::uiScale + UiRender::offsetX;
        UiRender::pivotY = cy * UiRender::uiScale + UiRender::offsetY;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.6f, 1.0f, shown) * UiRender::lerp(1.0f, 0.94f, press);
        UiRender::alphaMul = savedAlpha * shown;

        UiRender::shadow(x, y, SIZE, SIZE, RADIUS, 12.0f, 4.0f, UiTheme::shadow());
        UiRender::roundRect(x, y, SIZE, SIZE, RADIUS,
                            UiRender::mix(UiTheme::surface(), UiTheme::surfaceRaised(), hover));
        UiRender::outline(x, y, SIZE, SIZE, RADIUS, UiTheme::outlineSoft());
        uint32_t ink = UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), hover);
        LaunchButton::progressRing("downloads#floating", cx, cy, ink, overall());

        int running = Transfers::running();
        if (running > 1) {
            std::string count = std::to_string(running);
            float bx = x + SIZE - BADGE + 3.0f;
            float by = y - 3.0f;
            UiRender::circle(bx + BADGE * 0.5f, by + BADGE * 0.5f, BADGE * 0.5f, UiTheme::textPrimary());
            float textW = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_CAPTION);
            UiRender::textY(count, bx + (BADGE - textW) * 0.5f, by, BADGE, UiTheme::surface(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CAPTION);
        }

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;

        if (hovered) {
            std::string text =
                    running > 1 ? "下载列表（" + std::to_string(running) + " 个进行中）" : std::string("下载列表");
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
            NewUi::tooltipText = text;
            NewUi::tooltipX = x + SIZE - tipW;
            NewUi::tooltipY = y - 34.0f;
        }
        if (shown > 0.99f && NewUi::click(x, y, SIZE, SIZE)) {
            UiSound::request(UiSound::PAGE);
            MainWindow::showTransfers();
        }
    }
}

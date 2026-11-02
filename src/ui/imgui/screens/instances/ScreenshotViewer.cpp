#include "ui/imgui/screens/instances/ScreenshotViewer.h"
#include "app/instances/Screenshots.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/instances/InstanceScreenshots.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/overlay/UiOverlays.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace ScreenshotViewer {

    namespace {
        constexpr float BAR_H = 56.0f;
        constexpr float TOOL = 34.0f;
        constexpr float TOOL_GAP = 4.0f;
        constexpr float GROUP_GAP = 16.0f;
        constexpr float PERCENT_W = 50.0f;
        constexpr float SIDE_W = 72.0f;
        constexpr float SHELL_RADIUS = 14.0f;
        constexpr float ZOOM_STEP = 1.25f;
        constexpr float ZOOM_MIN = 0.25f;
        constexpr float ZOOM_MAX = 16.0f;
        constexpr double DOUBLE_CLICK = 0.3;
        constexpr const char* OPEN = "screenshotViewer#open";

        std::string key;
        int index = -1;
        std::filesystem::path current;
        bool closing = false;
        float zoom = 1.0f;
        float zoomTarget = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
        float panTargetX = 0.0f;
        float panTargetY = 0.0f;
        float fitW = 0.0f;
        float fitH = 0.0f;
        bool pressing = false;
        bool dragged = false;
        float pressX = 0.0f;
        float pressY = 0.0f;
        float lastX = 0.0f;
        float lastY = 0.0f;
        double lastPress = -1.0;
        double copiedAt = -10.0;
        uint64_t copyVersion = 0;

        void paint();
        bool wheelAt(double amount);

        class ViewerOverlay final : public Overlay {
        public:
            bool active = false;

            bool isOpen() override {
                return active;
            }

            void release() override {
                if (!active) {
                    return;
                }
                active = false;
                closing = false;
                pressing = false;
                key.clear();
                index = -1;
                current.clear();
                Screenshots::forgetFullImage();
            }

            void draw() override {
                paint();
            }

            bool scroll(double amount) override {
                return active && wheelAt(amount);
            }

            bool bounded() override {
                return true;
            }
        };

        ViewerOverlay layer;

        uint32_t white(float alpha) {
            return ((uint32_t) Ui::round(255.0f * std::clamp(alpha, 0.0f, 1.0f)) << 24) | 0x00FFFFFFu;
        }

        void go(int at) {
            index = at;
            current = Screenshots::of(key).entries[at].path;
            zoom = zoomTarget = 1.0f;
            panX = panY = panTargetX = panTargetY = 0.0f;
            pressing = false;
            lastPress = -1.0;
            copiedAt = -10.0;
            ++copyVersion;
            Screenshots::forgetFullImage();
        }

        void copy(const std::filesystem::path& path) {
            copiedAt = -10.0;
            uint64_t version = ++copyVersion;
            Screenshots::copy(path, [version] {
                if (layer.active && !closing && copyVersion == version) {
                    copiedAt = ImGui::GetTime();
                }
            });
        }

        void finish() {
            UiOverlays::close(&layer);
            layer.release();
        }

        void clampPan(float& x, float& y, float level) {
            float roomX = std::max(0.0f, (fitW * level - MainWindow::width()) * 0.5f);
            float roomY = std::max(0.0f, (fitH * level - MainWindow::height()) * 0.5f);
            x = std::clamp(x, -roomX, roomX);
            y = std::clamp(y, -roomY, roomY);
        }

        void zoomAt(float level, float x, float y) {
            level = std::clamp(level, ZOOM_MIN, ZOOM_MAX);
            float ratio = level / zoomTarget;
            float offsetX = x - MainWindow::width() * 0.5f;
            float offsetY = y - MainWindow::height() * 0.5f;
            panTargetX = offsetX - (offsetX - panTargetX) * ratio;
            panTargetY = offsetY - (offsetY - panTargetY) * ratio;
            zoomTarget = level;
            clampPan(panTargetX, panTargetY, zoomTarget);
        }

        void zoomStep(int direction) {
            float next = direction > 0 ? zoomTarget * ZOOM_STEP : zoomTarget / ZOOM_STEP;
            if ((zoomTarget < 0.999f && next > 1.001f) || (zoomTarget > 1.001f && next < 0.999f)) {
                next = 1.0f;
            }
            zoomAt(next, MainWindow::width() * 0.5f, MainWindow::height() * 0.5f);
        }

        void toggleZoom(int nativeW, float x, float y) {
            if (std::fabs(zoomTarget - 1.0f) > 0.01f) {
                zoomAt(1.0f, x, y);
                return;
            }
            float physicalWidth = fitW * UiRender::uiScale * UiRender::pixelScale();
            float actual = nativeW > 0 && physicalWidth > 0.0f ? (float) nativeW / physicalWidth : 0.0f;
            zoomAt(actual > 1.5f ? actual : 2.0f, x, y);
        }

        int count() {
            return (int) Screenshots::of(key).entries.size();
        }

        bool wheelAt(double amount) {
            if (!closing) {
                zoomAt(zoomTarget * std::pow(ZOOM_STEP, (float) amount), NewUi::mouseX, NewUi::mouseY);
            }
            return true;
        }

        void ease(float& value, float target, float t, float snap) {
            value += (target - value) * t;
            if (std::fabs(target - value) < snap) {
                value = target;
            }
        }

        bool tool(const std::string& id, int glyph, const std::string& hint, float x, float y, bool enabled) {
            bool hovered = !closing && NewUi::hit(x, y, TOOL, TOOL);
            float lit = UiState::motion("screenshotViewer#" + id, hovered && enabled ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, TOOL, TOOL, 8.0f, white(0.14f * lit));
            }
            UiWidgets::icon(Ui::glyph(glyph), x + TOOL * 0.5f, y + TOOL * 0.5f,
                            white(enabled ? UiRender::lerp(0.78f, 1.0f, lit) : 0.28f), 13.0f);
            if (hovered) {
                Page::tooltip(hint, x, y, TOOL, TOOL);
                if (enabled) {
                    UiCursor::hand();
                }
            }
            bool clicked = !closing && NewUi::click(x, y, TOOL, TOOL);
            return enabled && clicked;
        }

        bool side(const std::string& id, int glyph, float x, float y, float h, bool enabled) {
            if (!enabled) {
                return false;
            }
            bool hovered = !closing && NewUi::hit(x, y, SIDE_W, h);
            float lit = UiState::motion("screenshotViewer#" + id, hovered ? 1.0f : 0.0f, 18.0f);
            float cx = x + SIDE_W * 0.5f;
            float cy = y + h * 0.5f;
            UiRender::circle(cx, cy, 20.0f, white(0.06f + 0.12f * lit));
            UiWidgets::icon(Ui::glyph(glyph), cx, cy, white(UiRender::lerp(0.45f, 1.0f, lit)), 13.0f);
            if (hovered) {
                UiCursor::hand();
            }
            return hovered;
        }

        void shade(float y, float h, bool down) {
            const int strips = 14;
            float stripH = h / (float) strips;
            for (int i = 0; i < strips; i++) {
                float t = ((float) i + 0.5f) / (float) strips;
                float strength = down ? t : 1.0f - t;
                UiRender::roundRect(0.0f, y + (float) i * stripH, MainWindow::width(), stripH + 0.5f, 0.0f,
                                    (uint32_t) Ui::round(140.0f * strength * strength) << 24);
            }
        }

        void picture(uint32_t texture, float x, float y, float w, float h) {
            float windowW = MainWindow::width();
            float windowH = MainWindow::height();
            float x0 = std::max(x, 0.0f);
            float y0 = std::max(y, 0.0f);
            float x1 = std::min(x + w, windowW);
            float y1 = std::min(y + h, windowH);
            if (x1 <= x0 || y1 <= y0) {
                return;
            }
            bool spanX = x <= 0.5f && x + w >= windowW - 0.5f;
            bool spanY = y <= 0.5f && y + h >= windowH - 0.5f;
            float radius = spanX && spanY ? SHELL_RADIUS : (spanX || spanY ? 0.0f : 4.0f);
            ShaderUtil::roundTex(UiRender::transformX(x0), UiRender::transformY(y0), UiRender::transformW(x1 - x0),
                                 UiRender::transformW(y1 - y0), UiRender::transformW(radius), texture, (x0 - x) / w,
                                 (y0 - y) / h, (x1 - x) / w, (y1 - y) / h, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        void paint() {
            if (!layer.active) {
                return;
            }
            const Screenshots::List& list = Screenshots::of(key);
            int total = (int) list.entries.size();
            if (total == 0) {
                finish();
                return;
            }
            auto found = std::find_if(list.entries.begin(), list.entries.end(),
                                      [](const Screenshots::Entry& item) { return item.path == current; });
            if (found != list.entries.end()) {
                index = (int) std::distance(list.entries.begin(), found);
            } else {
                go(std::clamp(index, 0, total - 1));
            }
            float open = UiState::motionFrom(OPEN, closing ? 0.0f : 1.0f, closing ? 22.0f : 14.0f, 0.0f);
            if (closing && open < 0.02f) {
                finish();
                return;
            }
            const Screenshots::Entry entry = list.entries[index];
            float windowW = MainWindow::width();
            float windowH = MainWindow::height();
            float mx = NewUi::mouseX;
            float my = NewUi::mouseY;
            double now = ImGui::GetTime();

            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * open;
            UiRender::roundRect(0.0f, 0.0f, windowW, windowH, SHELL_RADIUS, 0xEB000000u);
            NewUi::markOverlayUnion(0.0f, 0.0f, windowW, windowH);
            NewUi::markAbove(0.0f, 0.0f, windowW, windowH);

            std::shared_ptr<const Pixels> full = Screenshots::fullImage(entry.path, entry.modified);
            std::shared_ptr<const Pixels> thumb = Screenshots::thumbnail(entry.path, entry.modified);
            std::shared_ptr<const Pixels> shown = full ? full : thumb;
            uint32_t texture = Textures::of(shown);
            if (shown && shown->width > 0 && shown->height > 0) {
                float fit = std::min((windowW - 2.0f * SIDE_W) / (float) shown->width,
                                     (windowH - 2.0f * BAR_H) / (float) shown->height);
                fitW = (float) shown->width * fit;
                fitH = (float) shown->height * fit;
            }

            float t = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f);
            clampPan(panTargetX, panTargetY, zoomTarget);
            ease(zoom, zoomTarget, t, 0.0005f);
            ease(panX, panTargetX, t, 0.05f);
            ease(panY, panTargetY, t, 0.05f);
            if (zoom != zoomTarget || panX != panTargetX || panY != panTargetY) {
                UiState::keepAnimating();
            }

            float eased = 1.0f - (float) std::pow(1.0f - open, 3.0);
            float drawW = fitW * zoom * UiRender::lerp(0.94f, 1.0f, eased);
            float drawH = fitH * zoom * UiRender::lerp(0.94f, 1.0f, eased);
            float imageX = windowW * 0.5f + panX - drawW * 0.5f;
            float imageY = windowH * 0.5f + panY - drawH * 0.5f;
            if (texture != 0 && drawW > 0.0f && drawH > 0.0f) {
                picture(texture, imageX, imageY, drawW, drawH);
            } else {
                Page::spinner(windowW * 0.5f, windowH * 0.5f, 10.0f, 0xFFFFFFFFu);
            }
            bool overImage = NewUi::rawHit(imageX, imageY, drawW, drawH);

            shade(0.0f, BAR_H + 20.0f, false);
            shade(windowH - BAR_H - 20.0f, BAR_H + 20.0f, true);

            float zoneY = BAR_H;
            float zoneH = windowH - 2.0f * BAR_H;
            bool inPrev = side("prev", 0xf053, 0.0f, zoneY, zoneH, index > 0);
            bool inNext = side("next", 0xf054, windowW - SIDE_W, zoneY, zoneH, index + 1 < total);

            float toolY = (BAR_H - TOOL) * 0.5f;
            float x = windowW - 12.0f - TOOL;
            bool removed = false;
            if (tool("close", 0xf00d, "关闭", x, toolY, true)) {
                ScreenshotViewer::close();
            }
            x -= GROUP_GAP + TOOL;
            if (tool("delete", 0xf2ed, "删除", x, toolY, true)) {
                Screenshots::remove(key, entry.path);
                removed = true;
            }
            x -= TOOL_GAP + TOOL;
            if (tool("reveal", 0xf07c, "在文件夹中显示", x, toolY, true)) {
                Platform::reveal(entry.path);
            }
            x -= TOOL_GAP + TOOL;
            if (tool("copy", 0xf0c5, "复制", x, toolY, true)) {
                copy(entry.path);
            }
            x -= GROUP_GAP + TOOL;
            if (tool("zoomIn", 0xf00e, "放大", x, toolY, zoomTarget < ZOOM_MAX - 0.001f)) {
                zoomStep(1);
            }
            x -= PERCENT_W;
            if (full) {
                float physicalWidth = drawW * UiRender::uiScale * UiRender::pixelScale();
                int percent = (int) std::lround(physicalWidth / (float) full->width * 100.0f);
                std::string text = std::to_string(percent) + "%";
                bool hovered = !closing && NewUi::hit(x, toolY, PERCENT_W, TOOL);
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(text, x + (PERCENT_W - textW) * 0.5f, toolY, TOOL, white(hovered ? 1.0f : 0.78f),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (hovered) {
                    Page::tooltip("适应窗口", x, toolY, PERCENT_W, TOOL);
                    UiCursor::hand();
                }
                if (!closing && NewUi::click(x, toolY, PERCENT_W, TOOL)) {
                    zoomAt(1.0f, windowW * 0.5f, windowH * 0.5f);
                }
            }
            x -= TOOL;
            if (tool("zoomOut", 0xf010, "缩小", x, toolY, zoomTarget > ZOOM_MIN + 0.001f)) {
                zoomStep(-1);
            }

            float titleX = 20.0f;
            float room = x - 24.0f - titleX;
            std::string meta = InstanceScreenshots::dateText(entry.modified) + "  " +
                    InstanceScreenshots::sizeText(entry.size) +
                    (full ? "  " + std::to_string(full->width) + "×" + std::to_string(full->height) : std::string());
            float metaW = UiFont::width(meta, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            bool withMeta = room - metaW - 14.0f >= 120.0f;
            std::string name = UiFont::fitEnd(entry.filename, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                              std::max(0.0f, withMeta ? room - metaW - 14.0f : room));
            float nameY = (BAR_H - UiWidgets::TEXT_CONTROL) * 0.5f;
            UiFont::draw(name, titleX, nameY, white(1.0f), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (withMeta) {
                float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float baseline = nameY + UiFont::ascent(UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiFont::draw(meta, titleX + nameW + 14.0f,
                             baseline - UiFont::ascent(UiFont::MEDIUM, UiWidgets::TEXT_SMALL), white(0.55f),
                             UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }

            std::string counter = std::to_string(index + 1) + " / " + std::to_string(total);
            float counterW = UiFont::width(counter, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(counter, (windowW - counterW) * 0.5f, windowH - BAR_H, BAR_H, white(0.7f), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);

            float copied = (float) (now - copiedAt);
            if (copied < 1.6f) {
                float fade = std::min(1.0f, (1.6f - copied) / 0.3f);
                std::string text = "已复制到剪贴板";
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float pillW = textW + 32.0f;
                float pillY = windowH - BAR_H - 44.0f;
                UiRender::roundRect((windowW - pillW) * 0.5f, pillY, pillW, 34.0f, 17.0f,
                                    ((uint32_t) Ui::round(220.0f * fade) << 24) | 0x00202024u);
                UiRender::textY(text, (windowW - textW) * 0.5f, pillY, 34.0f, white(fade), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                UiState::keepAnimating();
            }
            UiRender::alphaMul = savedAlpha;

            if (removed) {
                int left = count();
                if (left == 0) {
                    ScreenshotViewer::close();
                } else {
                    go(std::min(index, left - 1));
                }
                return;
            }
            if (closing) {
                return;
            }

            if (NewUi::pendingClick && !NewUi::clickConsumed && NewUi::hit(0.0f, 0.0f, windowW, windowH)) {
                NewUi::clickConsumed = true;
                bool imagePress = overImage && !inPrev && !inNext;
                bool twice = imagePress && now - lastPress < DOUBLE_CLICK && std::fabs(mx - pressX) < 6.0f &&
                        std::fabs(my - pressY) < 6.0f;
                lastPress = imagePress && !twice ? now : -1.0;
                pressX = lastX = mx;
                pressY = lastY = my;
                dragged = false;
                pressing = !twice;
                if (twice) {
                    toggleZoom(full ? full->width : 0, mx, my);
                }
            }
            if (pressing) {
                if (NewUi::mouseDown) {
                    if (!dragged && (std::fabs(mx - pressX) > 4.0f || std::fabs(my - pressY) > 4.0f)) {
                        dragged = true;
                    }
                    if (dragged) {
                        panTargetX += mx - lastX;
                        panTargetY += my - lastY;
                        clampPan(panTargetX, panTargetY, zoomTarget);
                        panX = panTargetX;
                        panY = panTargetY;
                    }
                    lastX = mx;
                    lastY = my;
                } else {
                    pressing = false;
                    if (!dragged) {
                        if (inPrev) {
                            go(index - 1);
                        } else if (inNext) {
                            go(index + 1);
                        } else if (!overImage) {
                            ScreenshotViewer::close();
                            return;
                        }
                    }
                }
            }

            if (UiOverlays::top() != &layer) {
                return;
            }
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                ScreenshotViewer::close();
            } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) && index > 0) {
                go(index - 1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && index + 1 < total) {
                go(index + 1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, true)) {
                zoomStep(1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Minus, true) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, true)) {
                zoomStep(-1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
                zoomAt(1.0f, windowW * 0.5f, windowH * 0.5f);
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                copy(entry.path);
            }
        }
    }

    void open(const std::string& instanceKey, const std::filesystem::path& path) {
        const Screenshots::List& list = Screenshots::of(instanceKey);
        auto found = std::find_if(list.entries.begin(), list.entries.end(),
                                  [&path](const Screenshots::Entry& item) { return item.path == path; });
        if (found == list.entries.end()) {
            return;
        }
        int at = (int) std::distance(list.entries.begin(), found);
        UiOverlays::addLayerBefore(&layer, UiOverlays::sub);
        NewUi::closeAllOverlays();
        UiInput::blur();
        UiOverlays::openOn(nullptr, &layer);
        layer.active = true;
        closing = false;
        key = instanceKey;
        go(at);
        UiState::remove(OPEN);
    }

    void close() {
        if (layer.active && !closing) {
            closing = true;
            pressing = false;
        }
    }

    bool isOpen() {
        return layer.active;
    }
}

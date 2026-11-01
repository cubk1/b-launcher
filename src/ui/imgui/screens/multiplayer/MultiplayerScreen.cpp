#include "ui/imgui/screens/multiplayer/MultiplayerScreen.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/multiplayer/RoomApi.h"

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/widget/IconButtonWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace MultiplayerScreen {

    namespace {
        using Multiplayer::Role;
        using Multiplayer::Stage;

        constexpr int VIEW_HOME = 0;
        constexpr int VIEW_WAIT = 1;
        constexpr int VIEW_ROOM = 2;

        constexpr float CODE_SIZE = 18.0f;
        constexpr float TAG_H = 18.0f;
        constexpr float BUTTON_MIN = 96.0f;
        constexpr float ICON_INSET = 20.0f;
        constexpr float WELCOME_W = 360.0f;
        constexpr float OPTION_H = 56.0f;
        constexpr float OPTION_ICON = 32.0f;
        constexpr float LINE_H = 20.0f;
        constexpr float DIALOG_W = 380.0f;
        constexpr float DIALOG_PAD = 24.0f;
        constexpr float JOIN_H = 236.0f;
        constexpr float PORT_H = 214.0f;
        constexpr float BOX_W = 42.0f;
        constexpr float BOX_H = 52.0f;
        constexpr float BOX_GAP = 8.0f;
        constexpr float BOX_TEXT = 22.0f;
        constexpr int CODE_LENGTH = 6;
        constexpr int PORT_LENGTH = 5;
        constexpr int ROOM_LIMIT = 10;
        const char* const CODE_INPUT = "multiplayer#codeInput";
        const char* const PORT_INPUT = "multiplayer#portInput";
        const char* const JOIN_DIALOG = "multiplayer#join";
        const char* const PORT_DIALOG = "multiplayer#port";
        const char* const ADDRESS_DOC = "在游戏的「多人游戏」里选「直接连接」，输入这个地址就能进入房主的世界。";
        const char* const PLAY_DOC = "启动当前选中的实例，进入游戏后自动连接房主的世界。";
        const char* const WAIT_TEXT = "进入要分享的世界，按 Esc 打开游戏菜单，选择「对局域网开放」，"
                                      "再点「创建局域网世界」。启动器检测到之后会自动创建房间。";

        PageTransition views(VIEW_HOME);
        bool waiting = false;
        int waitSerial = 0;
        std::string joinNotice;
        std::string noticeFor;
        double shakeAt = -10.0;

        uint32_t onlineColor() {
            return UiTheme::isLight() ? 0xFF2F9E5Cu : 0xFF5CC98Au;
        }

        void tip(const std::string& text, float x, float y, float w, float h) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
            NewUi::tooltipText = text;
            NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
            NewUi::tooltipY = y + h + 6.0f;
        }

        bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y, bool blocked) {
            IconButtonWidget button(id, Ui::glyph(glyph), UiTheme::textSecondary(), UiTheme::textPrimary());
            button.bounds(x, y, 28, 28).block(blocked);
            button.paint();
            if (!blocked && button.hovered()) {
                tip(hint, x, y, 28, 28);
            }
            return button.pressed;
        }

        int corners(int index, int count) {
            return (index == 0 ? 1 | 2 : 0) | (index == count - 1 ? 4 | 8 : 0);
        }

        void centredText(const std::string& text, float cx, float y, float h, uint32_t color, float size) {
            float w = UiFont::width(text, UiFont::MEDIUM, size);
            UiRender::textY(text, cx - w * 0.5f, y, h, color, UiFont::MEDIUM, size);
        }

        float paragraph(const std::string& text, float x, float y, float w, uint32_t color, bool centred) {
            std::vector<std::string> lines = UiFont::wrap(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w);
            for (size_t i = 0; i < lines.size(); i++) {
                float lineX = x;
                if (centred) {
                    lineX = x + (w - UiFont::width(lines[i], UiFont::MEDIUM, UiWidgets::TEXT_CONTROL)) * 0.5f;
                }
                UiRender::textY(lines[i], lineX, y + i * LINE_H, LINE_H, color, UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            return lines.size() * LINE_H;
        }

        float paragraphHeight(const std::string& text, float w) {
            return UiFont::wrap(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w).size() * LINE_H;
        }

        bool dimButton(const std::string& id, const std::string& text, float x, float y, float w, bool primary,
                       bool ready, bool blocked, int glyph) {
            float saved = UiRender::alphaMul;
            if (!ready) {
                UiRender::alphaMul = saved * 0.5f;
            }
            bool pressed = Page::button(id, text, x, y, w, primary, blocked || !ready, glyph);
            UiRender::alphaMul = saved;
            return pressed && ready;
        }

        float labelRight(const std::string& label) {
            return Page::left() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
        }

        void explain(const std::string& id, const std::string& text, float rowY, float right, bool blocked) {
            if (!blocked && NewUi::hit(Page::left(), rowY, right - Page::left(), Page::ROW_H)) {
                NewUi::markDoc(id, text, Page::left() + 6.0f, rowY + Page::ROW_H + 10.0f);
            }
        }

        void valueText(const std::string& text, float right, float left, float rowY, uint32_t color) {
            std::string shown =
                    UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, std::max(0.0f, right - left));
            float w = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - w, rowY, Page::ROW_H, color, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }

        void hint(const std::string& text, float right, float rowY) {
            float left = Page::left() + 16.0f;
            std::string shown =
                    UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, std::max(0.0f, right - left));
            UiRender::textY(shown, left, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        float tag(const std::string& text, float x, float rowY, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            float y = rowY + (Page::ROW_H - TAG_H) * 0.5f;
            UiRender::roundRect(x, y, w, TAG_H, 6.0f, fill);
            UiRender::textY(text, x + 6.0f, y, TAG_H, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        std::string digits(const std::string& text, size_t limit) {
            std::string out;
            for (char c : text) {
                if (c >= '0' && c <= '9' && out.size() < limit) {
                    out.push_back(c);
                }
            }
            return out;
        }

        std::string cleanInput(const char* id, size_t limit) {
            if (!UiInput::isFocused(id)) {
                UiInput::focus(id, (int) limit);
            }
            std::string raw = UiInput::get(id);
            std::string clean = digits(raw, limit);
            if (clean != raw) {
                UiInput::set(id, clean);
            }
            return clean;
        }

        bool enterPressed() {
            return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        }

        bool blink() {
            UiState::keepAnimating();
            return std::fmod(ImGui::GetTime(), 1.06) < 0.53;
        }

        void caret(float x, float y, float h) {
            if (blink()) {
                UiRender::rect(x - 0.75f, y, 1.5f, h, UiTheme::textPrimary());
            }
        }

        std::string worldName(const LanWorlds::World& world) {
            return world.name.empty() ? std::string("局域网世界") : world.name;
        }

        void startHosting(int picked, int typed) {
            if (picked > 0) {
                Multiplayer::pick(picked);
            }
            Multiplayer::type(typed);
            Multiplayer::host();
            waiting = false;
        }

        void serviceLine(float cx, float y, float maxW, bool blocked) {
            const Multiplayer::Service& service = Multiplayer::service();
            std::string text;
            uint32_t dot = 0;
            uint32_t ink = UiTheme::textMuted();
            if (!service.known) {
                text = "正在查询服务器状态";
            } else if (!service.reachable) {
                text = "联机服务器暂时连不上";
                dot = UiTheme::danger();
            } else if (service.maintenance) {
                text = service.message.empty() ? std::string("联机服务器维护中，暂时不能创建房间") : service.message;
                dot = UiTheme::danger();
                ink = UiTheme::danger();
            } else {
                text = "联机服务器可用性 " + std::to_string(service.available) + "/" + std::to_string(service.total);
                dot = service.available > 0 ? onlineColor() : UiTheme::danger();
            }
            float lead = dot != 0 ? 15.0f : 18.0f;
            std::string shown = UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, maxW - lead);
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float x = cx - (lead + shownW) * 0.5f;
            float cy = y + LINE_H * 0.5f;
            if (dot != 0) {
                UiRender::circle(x + 3.5f, cy, 3.5f, dot);
            } else {
                Page::spinner(x + 5.0f, cy, 5.0f, UiTheme::accent());
            }
            UiRender::textY(shown, x + lead, y, LINE_H, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (!blocked && shown != text && NewUi::hit(x + lead, y, shownW, LINE_H)) {
                tip(text, x + lead, y, shownW, LINE_H);
            }
        }

        bool optionRow(const std::string& id, int glyph, const std::string& label, const std::string& note, float x,
                       float y, float w, float h, int index, int count, bool enabled, bool blocked) {
            bool live = enabled && !blocked;
            bool hovered = live && NewUi::hit(x, y, w, h);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::overlay(Ui::round(14 * hover)), corners(index, count));
            }
            float saved = UiRender::alphaMul;
            if (!enabled) {
                UiRender::alphaMul = saved * 0.5f;
            }
            float iconX = x + 14.0f;
            float iconY = y + (h - OPTION_ICON) * 0.5f;
            if (glyph != 0) {
                UiRender::roundRect(iconX, iconY, OPTION_ICON, OPTION_ICON, 8.0f, UiTheme::accentSoft(36));
                UiWidgets::icon(Ui::glyph(glyph), iconX + OPTION_ICON * 0.5f, iconY + OPTION_ICON * 0.5f,
                                UiTheme::accentText(), 13.0f);
            }
            float textX = glyph != 0 ? iconX + OPTION_ICON + 14.0f : x + 16.0f;
            float right = x + w - 16.0f;
            float centerY = y + h * 0.5f;
            UiWidgets::icon(Ui::glyph(0xf054), right - 4.0f, centerY,
                            UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), hover), 9.0f);
            float noteRight = right - 20.0f;
            if (!note.empty()) {
                float noteW = UiFont::width(note, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(note, noteRight - noteW, y, h, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                noteRight -= noteW + 12.0f;
            }
            std::string shown =
                    UiFont::fitEnd(label, UiFont::MEDIUM, UiWidgets::TEXT_BODY, std::max(0.0f, noteRight - textX));
            UiRender::textY(shown, textX, y, h, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            UiRender::alphaMul = saved;
            if (live && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::PAGE);
                return true;
            }
            return false;
        }

        void listCard(float x, float y, float w, float rowH, int rows) {
            float h = rows * rowH;
            UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::surface());
            UiRender::outline(x, y, w, h, 12.0f, UiTheme::outlineSoft());
            for (int i = 1; i < rows; i++) {
                UiRender::hLine(x + 14.0f, x + w - 14.0f, y + i * rowH, UiTheme::outlineSoft());
            }
        }

        void dialogHeader(const std::string& title, const std::string& text, float x, float y, float w) {
            UiRender::textY(title, x + DIALOG_PAD, y + 20.0f, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            std::string shown = UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 2.0f * DIALOG_PAD);
            UiRender::textY(shown, x + DIALOG_PAD, y + 48.0f, LINE_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        bool dialogButtons(const std::string& id, const std::string& confirm, int glyph, bool ready, float x, float y,
                           float w, float h) {
            float buttonY = y + h - DIALOG_PAD - Page::FIELD_H;
            float confirmW = std::max(88.0f, Page::buttonWidth(confirm, glyph));
            float confirmX = x + w - DIALOG_PAD - confirmW;
            const std::string cancel = "取消";
            float cancelW = std::max(72.0f, Page::buttonWidth(cancel));
            if (Page::button(id + "#cancel", cancel, confirmX - 8.0f - cancelW, buttonY, cancelW, false, false)) {
                Dialog::close();
                return false;
            }
            bool pressed = dimButton(id + "#confirm", confirm, confirmX, buttonY, confirmW, true, ready, false, glyph);
            return pressed || (ready && enterPressed());
        }

        void blurInput(const char* id) {
            if (UiInput::isFocused(id)) {
                UiInput::blur();
            }
        }

        float shakeOffset() {
            double elapsed = ImGui::GetTime() - shakeAt;
            if (elapsed < 0.0 || elapsed >= 0.5) {
                return 0.0f;
            }
            UiState::keepAnimating();
            return (float) (std::sin(elapsed * 42.0) * 7.0 * std::exp(-elapsed * 7.0));
        }

        void finishJoin(const std::string& code) {
            blurInput(CODE_INPUT);
            Dialog::close();
            Multiplayer::forgetLookup();
            UiInput::set(CODE_INPUT, std::string());
            joinNotice.clear();
            Multiplayer::join(code);
        }

        void joinBody(float x, float y, float w, float h) {
            dialogHeader("加入房间", "输入好友发来的 6 位房间号", x, y, w);
            std::string code = cleanInput(CODE_INPUT, CODE_LENGTH);
            const Multiplayer::Lookup& lookup = Multiplayer::lookup();
            bool complete = (int) code.size() == CODE_LENGTH;
            if (!complete && !lookup.code.empty()) {
                Multiplayer::forgetLookup();
            } else if (complete && lookup.code != code) {
                Multiplayer::check(code);
            } else if (complete && lookup.done && lookup.exists) {
                finishJoin(code);
                return;
            } else if (complete && lookup.done && lookup.error.empty()) {
                joinNotice = "房间 " + code + " 不存在或已关闭";
                noticeFor.clear();
                shakeAt = ImGui::GetTime();
                Multiplayer::forgetLookup();
                UiInput::set(CODE_INPUT, std::string());
                code.clear();
                complete = false;
            } else if (complete && lookup.done) {
                joinNotice = lookup.error;
                noticeFor = code;
            }
            if (!joinNotice.empty() && code != noticeFor) {
                joinNotice.clear();
            }
            bool checking = complete && lookup.checking && lookup.code == code;
            bool retryable = complete && lookup.done && !lookup.error.empty() && lookup.code == code;

            bool focused = UiInput::isFocused(CODE_INPUT);
            float total = CODE_LENGTH * BOX_W + (CODE_LENGTH - 1) * BOX_GAP;
            float boxesX = x + (w - total) * 0.5f + shakeOffset();
            float boxesY = y + 86.0f;
            int filled = (int) code.size();
            for (int i = 0; i < CODE_LENGTH; i++) {
                float boxX = boxesX + i * (BOX_W + BOX_GAP);
                bool active = focused && !checking && (i == filled || (filled == CODE_LENGTH && i == CODE_LENGTH - 1));
                float lit = UiState::motion("multiplayer#box#" + std::to_string(i), active ? 1.0f : 0.0f, 18.0f);
                UiRender::roundRect(boxX, boxesY, BOX_W, BOX_H, 10.0f, UiTheme::surfaceRaised());
                UiRender::outline(boxX, boxesY, BOX_W, BOX_H, 10.0f,
                                  UiRender::mix(UiTheme::outlineSoft(), UiTheme::accent(), lit));
                if (i < filled) {
                    std::string digit(1, code[(size_t) i]);
                    float digitW = UiFont::width(digit, UiFont::MEDIUM, BOX_TEXT);
                    UiRender::textY(digit, boxX + (BOX_W - digitW) * 0.5f, boxesY, BOX_H,
                                    checking ? UiTheme::textMuted() : UiTheme::textPrimary(), UiFont::MEDIUM, BOX_TEXT);
                } else if (active) {
                    caret(boxX + BOX_W * 0.5f, boxesY + 15.0f, BOX_H - 30.0f);
                }
            }

            float cx = x + w * 0.5f;
            float noticeY = boxesY + BOX_H + 12.0f;
            if (checking) {
                const std::string text = "正在查找房间";
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float startX = cx - (textW + 18.0f) * 0.5f;
                Page::spinner(startX + 5.0f, noticeY + LINE_H * 0.5f, 5.0f, UiTheme::accent());
                UiRender::textY(text, startX + 18.0f, noticeY, LINE_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else if (!joinNotice.empty()) {
                std::string shown =
                        UiFont::fitEnd(joinNotice, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 2.0f * DIALOG_PAD);
                centredText(shown, cx, noticeY, LINE_H, UiTheme::danger(), UiWidgets::TEXT_CONTROL);
            }

            if (dialogButtons("multiplayer#join", retryable ? "重试" : "加入", retryable ? 0xf2f1 : 0xf2f6,
                              complete && !checking, x, y, w, h)) {
                Multiplayer::check(code);
            }
        }

        void openJoin() {
            Multiplayer::forgetLookup();
            joinNotice.clear();
            noticeFor.clear();
            shakeAt = -10.0;
            std::string pasted = Ui::trim(UiInput::clipboard());
            UiInput::set(CODE_INPUT, RoomApi::validCode(pasted) ? pasted : std::string());
            Dialog::open(JOIN_DIALOG, DIALOG_W, JOIN_H, joinBody, [] {
                blurInput(CODE_INPUT);
                Multiplayer::forgetLookup();
            });
            UiInput::focus(CODE_INPUT, CODE_LENGTH);
        }

        void portBody(float x, float y, float w, float h) {
            dialogHeader("手动填写端口", "对局域网开放后，游戏聊天栏会显示这个端口号", x, y, w);
            std::string port = cleanInput(PORT_INPUT, PORT_LENGTH);
            int value = port.empty() ? 0 : std::stoi(port);
            bool valid = value > 0 && value <= 65535;
            bool focused = UiInput::isFocused(PORT_INPUT);
            float fieldX = x + DIALOG_PAD;
            float fieldY = y + 86.0f;
            float fieldW = w - 2.0f * DIALOG_PAD;
            float lit = UiState::motion("multiplayer#portField", focused ? 1.0f : 0.0f, 18.0f);
            UiRender::roundRect(fieldX, fieldY, fieldW, BOX_H, 10.0f, UiTheme::surfaceRaised());
            UiRender::outline(fieldX, fieldY, fieldW, BOX_H, 10.0f,
                              UiRender::mix(UiTheme::outlineSoft(), UiTheme::accent(), lit));
            float textX = fieldX + 16.0f;
            if (port.empty() && !focused) {
                UiRender::textY("例如 51234", textX, fieldY, BOX_H, UiTheme::textFaint(), UiFont::MEDIUM, BOX_TEXT);
            } else {
                UiRender::textY(port, textX, fieldY, BOX_H, UiTheme::textPrimary(), UiFont::MEDIUM, BOX_TEXT);
            }
            if (focused) {
                caret(textX + UiFont::width(port, UiFont::MEDIUM, BOX_TEXT) + 1.0f, fieldY + 15.0f, BOX_H - 30.0f);
            }
            if (dialogButtons("multiplayer#port", "创建房间", 0xf067, valid, x, y, w, h)) {
                blurInput(PORT_INPUT);
                Dialog::close();
                startHosting(0, value);
                UiInput::set(PORT_INPUT, std::string());
            }
        }

        void openPort() {
            UiInput::set(PORT_INPUT, std::string());
            Dialog::open(PORT_DIALOG, DIALOG_W, PORT_H, portBody, [] { blurInput(PORT_INPUT); });
            UiInput::focus(PORT_INPUT, PORT_LENGTH);
        }

        void homeView(bool blocked) {
            float cx = Page::left() + Page::width() * 0.5f;
            float middle = MainWindow::height() * 0.5f;
            if (!Multiplayer::available()) {
                centredText("这个版本没有包含联机组件", cx, middle - 12.0f, 24.0f, UiTheme::textMuted(),
                            UiWidgets::TEXT_BODY);
                return;
            }
            float cardH = 2 * OPTION_H;
            float total = 24.0f + 22.0f + cardH;
            float y = std::max(Page::TOP, middle - total * 0.5f);
            centredText("联机", cx, y, 24.0f, UiTheme::textPrimary(), Page::TITLE_SIZE);
            y += 24.0f + 22.0f;
            float x = cx - WELCOME_W * 0.5f;
            listCard(x, y, WELCOME_W, OPTION_H, 2);
            const Multiplayer::Service& service = Multiplayer::service();
            bool hostable = Multiplayer::canHost();
            std::string note = hostable ? std::string() : service.maintenance ? "维护中" : "暂时不可用";
            if (optionRow("multiplayer#create", 0xf233, "创建房间", note, x, y, WELCOME_W, OPTION_H, 0, 2, hostable,
                          blocked)) {
                waiting = true;
                waitSerial = Multiplayer::worldsSerial();
            }
            if (optionRow("multiplayer#enter", 0xf2f6, "加入房间", std::string(), x, y + OPTION_H, WELCOME_W, OPTION_H,
                          1, 2, true, blocked)) {
                openJoin();
            }
            serviceLine(cx, MainWindow::height() - 28.0f - LINE_H, Page::width(), blocked);
        }

        void waitView(bool blocked) {
            float cx = Page::left() + Page::width() * 0.5f;
            float middle = MainWindow::height() * 0.5f;
            bool fresh = Multiplayer::worldsSerial() != waitSerial;
            std::vector<LanWorlds::World> worlds = fresh ? Multiplayer::worlds() : std::vector<LanWorlds::World>();
            bool hostable = Multiplayer::canHost();
            if (worlds.size() == 1 && hostable) {
                startHosting(worlds.front().port, 0);
                return;
            }
            float textW = WELCOME_W + 40.0f;
            float textH = paragraphHeight(WAIT_TEXT, textW);
            int rows = (int) worlds.size();
            float listH = rows > 1 ? 18.0f + rows * Page::ROW_H : 0.0f;
            float total = 28.0f + 18.0f + 24.0f + 10.0f + textH + listH + 22.0f + LINE_H + 22.0f + Page::FIELD_H;
            float y = std::max(Page::TOP, middle - total * 0.5f);

            UiRender::circle(cx, y + 14.0f, 14.0f, UiTheme::accentSoft(36));
            UiWidgets::icon(Ui::glyph(0xf6ff), cx, y + 14.0f, UiTheme::accentText(), 12.0f);
            y += 28.0f + 18.0f;
            centredText("在游戏里对局域网开放", cx, y, 24.0f, UiTheme::textPrimary(), Page::TITLE_SIZE);
            y += 24.0f + 10.0f;
            y += paragraph(WAIT_TEXT, cx - textW * 0.5f, y, textW, UiTheme::textMuted(), true);

            if (rows > 1) {
                y += 18.0f;
                float x = cx - WELCOME_W * 0.5f;
                listCard(x, y, WELCOME_W, Page::ROW_H, rows);
                for (int i = 0; i < rows; i++) {
                    const LanWorlds::World& world = worlds[(size_t) i];
                    if (optionRow("multiplayer#world#" + std::to_string(world.port), 0, worldName(world),
                                  "端口 " + std::to_string(world.port), x, y + i * Page::ROW_H, WELCOME_W, Page::ROW_H,
                                  i, rows, hostable, blocked)) {
                        startHosting(world.port, 0);
                        return;
                    }
                }
                y += rows * Page::ROW_H;
            }

            y += 22.0f;
            if (!hostable) {
                serviceLine(cx, y, WELCOME_W, blocked);
            } else {
                std::string status = rows > 1 ? "检测到多个世界，选一个分享" : "正在等待局域网世界";
                float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float statusX = cx - (statusW + 18.0f) * 0.5f;
                if (rows > 1) {
                    centredText(status, cx, y, LINE_H, UiTheme::textMuted(), UiWidgets::TEXT_CONTROL);
                } else {
                    Page::spinner(statusX + 5.0f, y + LINE_H * 0.5f, 5.0f, UiTheme::accent());
                    UiRender::textY(status, statusX + 18.0f, y, LINE_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                }
            }
            y += LINE_H + 22.0f;

            const std::string back = "返回";
            const std::string manual = "手动填写端口";
            float backW = std::max(72.0f, Page::buttonWidth(back, 0xf053));
            float manualW = Page::buttonWidth(manual, 0xf11c);
            float buttonsX = cx - (backW + 8.0f + manualW) * 0.5f;
            if (Page::button("multiplayer#wait#back", back, buttonsX, y, backW, false, blocked, 0xf053)) {
                waiting = false;
            }
            if (dimButton("multiplayer#wait#manual", manual, buttonsX + backW + 8.0f, y, manualW, false, hostable,
                          blocked, 0xf11c)) {
                openPort();
            }
        }

        void codeRow(const Multiplayer::State& state, float rowY, bool blocked) {
            const char* label = "房间号";
            Page::label(label, rowY);
            if (state.code.empty()) {
                valueText(state.stage == Stage::Failed ? "未创建" : "正在分配", Page::controlRight(), labelRight(label),
                          rowY, UiTheme::textMuted());
                return;
            }
            float copyX = Page::controlRight() - ICON_INSET;
            if (iconButton("multiplayer#copyCode", 0xf0c5, "复制房间号", copyX, rowY + 6.0f, blocked)) {
                UiInput::setClipboard(state.code);
                Notifications::add(Notifications::INFO, "已复制房间号", 2500);
            }
            float w = UiFont::width(state.code, UiFont::MEDIUM, CODE_SIZE);
            UiRender::textY(state.code, copyX - 4.0f - w, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            CODE_SIZE);
        }

        void statusRow(const Multiplayer::State& state, float rowY) {
            const char* label = "状态";
            Page::label(label, rowY);
            bool failed = state.stage == Stage::Failed;
            bool ready = state.stage == Stage::Ready;
            std::string text = Multiplayer::stageText();
            bool live = ready && (state.role == Role::Guest || state.published);
            if (live && state.role == Role::Host) {
                text += state.links > 0 ? "，" + std::to_string(state.links) + " 个连接"
                                        : std::string("，等待好友加入");
            }
            float right = Page::controlRight();
            float cy = rowY + Page::ROW_H * 0.5f;
            if (!ready && !failed) {
                Page::spinner(right - 6.0f, cy, 6.0f, UiTheme::accent());
                right -= 22.0f;
            }
            valueText(text, right, labelRight(label), rowY, failed ? UiTheme::danger() : UiTheme::textSecondary());
        }

        void addressRow(const Multiplayer::State& state, float rowY, bool blocked) {
            const char* label = "服务器地址";
            Page::label(label, rowY);
            float left = labelRight(label);
            if (state.address.empty()) {
                valueText("连上房主后显示", Page::controlRight(), left, rowY, UiTheme::textMuted());
            } else {
                float copyX = Page::controlRight() - ICON_INSET;
                if (iconButton("multiplayer#copyAddress", 0xf0c5, "复制地址", copyX, rowY + 6.0f, blocked)) {
                    UiInput::setClipboard(state.address);
                    Notifications::add(Notifications::INFO, "已复制服务器地址", 2500);
                }
                valueText(state.address, copyX - 4.0f, left, rowY, UiTheme::textPrimary());
            }
            explain("multiplayer#address#doc", ADDRESS_DOC, rowY, left, blocked);
        }

        void playRow(const Multiplayer::State& state, float rowY, bool blocked) {
            const char* label = "进入游戏";
            Page::label(label, rowY);
            const Instances::Entry* entry = Instances::find(Instances::selected());
            bool ready = state.stage == Stage::Ready && !state.address.empty() && entry != nullptr;
            const std::string text = "启动并加入";
            float w = std::max(BUTTON_MIN, Page::buttonWidth(text, 0xf04b));
            float left = labelRight(label);
            valueText(entry != nullptr ? entry->info.id : std::string("没有选中实例"), Page::controlRight() - w - 12.0f,
                      left, rowY, UiTheme::textMuted());
            if (dimButton("multiplayer#play", text, Page::controlRight() - w, rowY + Page::FIELD_Y, w, true, ready,
                          blocked, 0xf04b)) {
                Game::launch(entry->key, false, std::string(), state.address);
            }
            explain("multiplayer#play#doc", PLAY_DOC, rowY, left, blocked);
        }

        int roomRows(const Multiplayer::State& state) {
            return state.role == Role::Host ? 2 : 4;
        }

        float roomCard(const Multiplayer::State& state, float y, bool blocked) {
            bool hosting = state.role == Role::Host;
            Page::caption(hosting ? "你的房间" : "加入的房间", y);
            float rowY = y + Page::CAPTION_H;
            int rows = roomRows(state);
            Page::card(rowY, rows);
            codeRow(state, rowY, blocked);
            statusRow(state, rowY + Page::ROW_H);
            if (!hosting) {
                addressRow(state, rowY + 2 * Page::ROW_H, blocked);
                playRow(state, rowY + 3 * Page::ROW_H, blocked);
            }
            return rowY + rows * Page::ROW_H;
        }

        void memberMenu(const Multiplayer::Member& member, float x, float y) {
            std::string device = member.device;
            NewUi::openMenu(
                    "multiplayer#member#menu",
                    [device] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("移出房间", 0xf2f5,
                                                             [device] { Multiplayer::kick(device, false); }));
                        items.push_back(
                                ActionMenuItem::make("封禁", 0xf05e, [device] { Multiplayer::kick(device, true); })
                                        ->dangerous()
                                        ->confirmFirst());
                        return items;
                    },
                    x, y, 150.0f);
        }

        void memberRow(const Multiplayer::Member& member, float rowY, bool manage, bool blocked) {
            float cy = rowY + Page::ROW_H * 0.5f;
            UiWidgets::icon(Ui::glyph(member.host ? 0xf521 : 0xf108), Page::left() + 24.0f, cy,
                            UiTheme::textSecondary(), 12.0f);
            float x = Page::left() + 44.0f;
            std::string name = member.name.empty() ? "设备 " + member.device : member.name;
            name = UiFont::fitEnd(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::width() * 0.45f);
            UiRender::textY(name, x, rowY, Page::ROW_H, member.online ? UiTheme::textPrimary() : UiTheme::textMuted(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            x += UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
            if (member.host) {
                x += tag("房主", x, rowY, UiTheme::accentSoft(40), UiTheme::accentText()) + 4.0f;
            }
            if (member.self) {
                x += tag("你", x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
            }
            float right = Page::controlRight();
            if (manage && !member.self) {
                float menuX = right - ICON_INSET;
                if (iconButton("multiplayer#member#" + member.device, 0xf142, "管理成员", menuX, rowY + 6.0f,
                               blocked)) {
                    memberMenu(member, menuX - 122.0f, rowY + 34.0f);
                }
                right = menuX - 4.0f;
            }
            if (!member.online) {
                valueText("离线", right, x + 8.0f, rowY, UiTheme::textMuted());
            }
        }

        float membersCard(const Multiplayer::State& state, float y, bool blocked) {
            int count = (int) state.members.size();
            Page::caption("成员（" + std::to_string(count) + "/" + std::to_string(ROOM_LIMIT) + "）", y);
            float rowY = y + Page::CAPTION_H;
            int rows = std::max(1, count);
            bool manage = state.role == Role::Host && state.stage == Stage::Ready;
            Page::card(rowY, rows);
            if (count == 0) {
                hint("还没有成员", Page::controlRight(), rowY);
            }
            for (int i = 0; i < count; i++) {
                float memberY = rowY + i * Page::ROW_H;
                if (Page::shown(memberY)) {
                    memberRow(state.members[i], memberY, manage, blocked);
                }
            }
            return rowY + rows * Page::ROW_H;
        }

        std::vector<std::string> detailLines(const std::string& detail) {
            std::vector<std::string> out;
            size_t from = 0;
            while (from <= detail.size()) {
                size_t end = detail.find('\n', from);
                std::string line = detail.substr(from, end == std::string::npos ? std::string::npos : end - from);
                if (!line.empty()) {
                    out.push_back(line);
                }
                if (end == std::string::npos) {
                    break;
                }
                from = end + 1;
            }
            return out;
        }

        float errorTextWidth() {
            return Page::width() - 32.0f - 34.0f;
        }

        float errorHeight(const Multiplayer::State& state) {
            std::vector<std::string> lines = detailLines(state.detail);
            float h = 0.0f;
            for (size_t i = 0; i < lines.size(); i++) {
                h += paragraphHeight(lines[i], errorTextWidth()) + (i > 0 ? 6.0f : 0.0f);
            }
            return std::max(Page::ROW_H, h + 28.0f);
        }

        void errorCard(const Multiplayer::State& state, float y, bool blocked) {
            Page::caption("错误详情", y);
            float top = y + Page::CAPTION_H;
            float h = errorHeight(state);
            UiRender::roundRect(Page::left(), top, Page::width(), h, 12.0f, UiTheme::surface());
            UiRender::outline(Page::left(), top, Page::width(), h, 12.0f, UiTheme::outlineSoft());
            std::vector<std::string> lines = detailLines(state.detail);
            float textY = top + 14.0f;
            for (size_t i = 0; i < lines.size(); i++) {
                bool technical = lines.size() > 1 && i + 1 == lines.size();
                textY += paragraph(lines[i], Page::left() + 16.0f, textY, errorTextWidth(),
                                   technical ? UiTheme::textMuted() : UiTheme::textSecondary(), false) +
                        6.0f;
            }
            if (iconButton("multiplayer#copyError", 0xf0c5, "复制错误信息", Page::controlRight() - ICON_INSET,
                           top + 6.0f, blocked)) {
                UiInput::setClipboard(state.error + "\n" + state.detail);
                Notifications::add(Notifications::INFO, "已复制错误信息", 2500);
            }
        }

        void roomView(const Multiplayer::State& state, bool covered) {
            Page::title("联机", Page::left(), Page::TOP);
            bool hosting = state.role == Role::Host;
            bool failed = state.stage == Stage::Failed;
            bool closing = state.stage == Stage::Closing;
            float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
            int members = std::max(1, (int) state.members.size());
            float lower = failed ? errorHeight(state) : members * Page::ROW_H;
            float content = 2 * Page::CAPTION_H + roomRows(state) * Page::ROW_H + Page::GAP + lower;
            Page::Scroll body("multiplayer#body", Page::TOP + 40.0f, buttonTop - 14.0f, content);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            y = roomCard(state, y, blocked) + Page::GAP;
            if (failed) {
                errorCard(state, y, blocked);
            } else {
                membersCard(state, y, blocked);
            }
            body.end();

            if (failed) {
                const std::string retry = "重试";
                const std::string back = "返回";
                float retryW = std::max(BUTTON_MIN, Page::buttonWidth(retry, 0xf2f1));
                float backW = std::max(BUTTON_MIN, Page::buttonWidth(back, 0xf053));
                float retryX = Page::right() - retryW;
                if (Page::button("multiplayer#back", back, retryX - 8.0f - backW, buttonTop, backW, false, covered,
                                 0xf053)) {
                    Multiplayer::dismiss();
                }
                if (dimButton("multiplayer#retry", retry, retryX, buttonTop, retryW, true, Multiplayer::canRetry(),
                              covered, 0xf2f1)) {
                    Multiplayer::retry();
                }
                return;
            }
            std::string text = closing ? (hosting ? "正在关闭" : "正在退出") : hosting ? "关闭房间" : "退出房间";
            float w = std::max(BUTTON_MIN, Page::buttonWidth(text, 0xf2f5));
            if (Page::button("multiplayer#leave", text, Page::right() - w, buttonTop, w, false, covered || closing,
                             0xf2f5)) {
                Multiplayer::leave();
            }
        }
    }

    void draw() {
        bool covered = NewUi::overlayCoversMouse();
        const Multiplayer::State& state = Multiplayer::state();
        bool inRoom = state.role != Role::None;
        if (inRoom || !Multiplayer::available()) {
            waiting = false;
        }
        if (Multiplayer::available()) {
            Multiplayer::warmUp();
            if (!inRoom) {
                Multiplayer::watchService();
            }
            if (waiting || state.role == Role::Host) {
                Multiplayer::watchWorlds();
            }
        }
        views.go(inRoom ? VIEW_ROOM : waiting ? VIEW_WAIT : VIEW_HOME);
        views.begin();
        if (views.page == VIEW_ROOM) {
            roomView(state, covered);
        } else if (views.page == VIEW_WAIT) {
            waitView(covered);
        } else {
            homeView(covered);
        }
        views.end();
    }
}

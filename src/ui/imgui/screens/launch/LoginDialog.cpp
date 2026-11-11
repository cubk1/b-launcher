#include "ui/imgui/screens/launch/LoginDialog.h"
#include "app/launch/Game.h"
#include "app/system/Network.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/strings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LoginDialog {

    namespace blb = b_launcher_base;

    namespace {
        const char* const DIALOG = "login";
        const char* const TITLE = "登录失败";
        constexpr float WIDTH = 520.0f;
        constexpr float PAD = 24.0f;
        constexpr float LINE_H = 20.0f;
        constexpr float SECTION_GAP = 16.0f;
        constexpr float WINDOW_MARGIN = 48.0f;
        constexpr float EDGE = 12.0f;
        constexpr float BUTTON_GAP = 10.0f;
        constexpr float COPY_SIZE = 28.0f;
        constexpr float COPY_GAP = 6.0f;

        std::optional<LoginCheck::Problem> shown;

        std::vector<std::string> phrases(std::string_view text) {
            std::vector<std::string> out;
            size_t from = 0;
            for (size_t at = 0; at < text.size();) {
                size_t mark = 0;
                for (std::string_view stop : {"，", "。", "；"}) {
                    mark = text.substr(at, stop.size()) == stop ? stop.size() : mark;
                }
                at += mark > 0 ? mark : 1;
                if (mark > 0) {
                    out.emplace_back(text.substr(from, at - from));
                    from = at;
                }
            }
            if (from < text.size()) {
                out.emplace_back(text.substr(from));
            }
            return out;
        }

        bool fits(const std::string& text, float width) {
            return UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) <= width;
        }

        std::vector<std::string> fold(std::string_view paragraph, float width) {
            std::vector<std::string> out;
            std::string line;
            for (const std::string& phrase : phrases(paragraph)) {
                if (fits(line + phrase, width)) {
                    line += phrase;
                    continue;
                }
                if (!line.empty()) {
                    out.push_back(std::move(line));
                    line.clear();
                }
                if (fits(phrase, width)) {
                    line = phrase;
                    continue;
                }
                std::vector<std::string> pieces = UiFont::wrap(phrase, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, width);
                if (!pieces.empty()) {
                    out.insert(out.end(), pieces.begin(), pieces.end() - 1);
                    line = pieces.back();
                }
            }
            if (!line.empty()) {
                out.push_back(std::move(line));
            }
            return out;
        }

        std::vector<std::string> wrapped(const std::string& text, float width, size_t limit) {
            std::vector<std::string> out;
            for (std::string_view part : blb::split(text, '\n')) {
                std::vector<std::string> lines = fold(part, width);
                out.insert(out.end(), lines.begin(), lines.end());
            }
            if (out.size() > limit) {
                out.resize(limit);
                out.back() = UiFont::fitEnd(out.back() + "…", UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, width);
            }
            return out;
        }

        float lines(const std::vector<std::string>& list, float x, float top, uint32_t color, bool draw) {
            for (const std::string& line : list) {
                if (draw) {
                    UiRender::textY(line, x, top, LINE_H, color, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                }
                top += LINE_H;
            }
            return top;
        }

        std::string causeOf(const LoginCheck::Problem& problem) {
            switch (problem.reach) {
                case LoginCheck::Reach::Offline:
                    return "设备无互联网连接，无法联网登录。";
                case LoginCheck::Reach::LoginServer:
                    return "设备有互联网连接，但无法连接至登录服务器。";
                case LoginCheck::Reach::Reachable:
                    break;
            }
            return "已连接至登录服务器，但无法登录。";
        }

        std::string hintsOf(const LoginCheck::Problem& problem) {
            switch (problem.reach) {
                case LoginCheck::Reach::Offline:
                    return "请检查您的设备是否正确连接互联网。";
                case LoginCheck::Reach::LoginServer:
                    return "可能是防火墙或代理配置错误、DNS 解析异常、或服务器正在维护。";
                case LoginCheck::Reach::Reachable:
                    break;
            }
            return "当前账户可能并未拥有 Minecraft，或服务器临时拒绝了登录请求。";
        }

        std::string offlineNote() {
            return "你也可以以离线模式继续启动，但无法通过正版验证。";
        }

        float header(const std::string& title, float left, float top, bool draw) {
            if (draw) {
                UiRender::textY(title, left, top, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            }
            return top + 24.0f + SECTION_GAP;
        }

        float text(const LoginCheck::Problem& problem, size_t cap, float left, float top, float inner, bool draw) {
            top = lines(wrapped(causeOf(problem), inner, cap), left, top, UiTheme::textSecondary(), draw) +
                  SECTION_GAP;
            top = lines(wrapped(hintsOf(problem), inner, cap), left, top, UiTheme::textSecondary(), draw) +
                  SECTION_GAP;
            top = lines(wrapped(offlineNote(), inner, cap), left, top, UiTheme::textSecondary(), draw);
            if (!problem.reason.empty()) {
                top += SECTION_GAP;

                float rowH = std::max(LINE_H, COPY_SIZE);
                if (draw) {
                    float textW = std::max(0.0f, inner - COPY_SIZE - COPY_GAP);
                    std::string reason =
                            UiFont::fitEnd(problem.reason, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, textW);
                    UiRender::textY(reason, left, top + (rowH - LINE_H) * 0.5f, LINE_H, UiTheme::textFaint(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    if (Page::iconButton("login#copy", 0xf0c5, "复制错误详情", left + inner - COPY_SIZE, top, false) &&
                        Platform::setClipboardText(problem.reason)) {
                        Notices::add(Notices::Kind::Done, "已复制错误详情", 2000);
                    }
                }
                top += rowH;
            }
            return top + SECTION_GAP;
        }

        void launch(const LoginCheck::Problem& problem, bool offline) {
            if (offline) {
                Network::setOfflineOnce(true);
            }
            Game::launch(problem.instance, problem.test, problem.account, problem.server);
            Dialog::close();
        }

        float buttonWidth() {
            static const char* const LABELS[] = {"取消启动", "离线启动", "重试"};
            float width = 0.0f;
            for (const char* label : LABELS) {
                width = std::max(width, Page::buttonWidth(label));
            }
            return std::max(96.0f, width);
        }

        float buttons(const LoginCheck::Problem& problem, float left, float top, float inner, bool draw) {
            bool offlineOnly = problem.reach == LoginCheck::Reach::Offline;
            std::string cancel = "取消启动";
            std::string offline = "离线启动";
            std::string retry = "重试";
            float width = buttonWidth();
            float total = width * (offlineOnly ? 2.0f : 3.0f) + BUTTON_GAP * (offlineOnly ? 1.0f : 2.0f);
            bool grid = total > inner;
            float cell = (inner - BUTTON_GAP) * 0.5f;
            float height = grid ? (offlineOnly ? Page::FIELD_H : Page::FIELD_H * 2.0f + BUTTON_GAP) : Page::FIELD_H;
            if (!draw) {
                return top + height;
            }
            if (grid) {
                width = cell;
            }
            float cancelX = grid ? left : left + inner - total;
            float offlineX = grid ? left + cell + BUTTON_GAP : cancelX + width + BUTTON_GAP;
            float retryX = grid ? left : offlineX + width + BUTTON_GAP;
            float retryY = grid ? top + Page::FIELD_H + BUTTON_GAP : top;
            bool cancelled = Page::button("login#cancel", cancel, cancelX, top, width, false, false);
            bool offlineClicked = Page::button("login#offline", offline, offlineX, top, width, offlineOnly, false);
            bool retried =
                    offlineOnly ? false : Page::button("login#retry", retry, retryX, retryY, width, true, false);
            if (cancelled) {
                Dialog::close();
            } else if (offlineClicked) {
                launch(problem, true);
            } else if (retried) {
                launch(problem, false);
            }
            return top + height;
        }

        float paint(const LoginCheck::Problem& problem, size_t cap, float x, float y, float w, bool draw) {
            float left = x + PAD;
            float inner = w - 2.0f * PAD;
            float top = header(TITLE, left, y + PAD, draw);
            top = text(problem, cap, left, top, inner, draw);
            return buttons(problem, left, top, inner, draw) + PAD - y;
        }

        size_t fitting(const LoginCheck::Problem& problem, float width) {
            float room = MainWindow::height() - WINDOW_MARGIN;
            for (size_t cap : {6u, 4u, 2u}) {
                if (paint(problem, cap, 0.0f, 0.0f, width, false) <= room) {
                    return cap;
                }
            }
            return 1;
        }

        float widthOf() {
            return std::min(WIDTH, MainWindow::width() - 2.0f * EDGE);
        }

        void body(float x, float y, float w, float h) {
            if (!shown) {
                Dialog::close();
                return;
            }
            LoginCheck::Problem problem = *shown;
            float needed = paint(problem, fitting(problem, w), x, y, w, true);
            float width = widthOf();
            if (std::abs(width - w) > 0.5f) {
                needed = paint(problem, fitting(problem, width), 0.0f, 0.0f, width, false);
            }
            if (std::abs(needed - h) > 0.5f || std::abs(width - w) > 0.5f) {
                Dialog::resize(DIALOG, width, needed);
            }
        }
    }

    void open(const LoginCheck::Problem& problem) {
        float width = widthOf();
        float height = paint(problem, fitting(problem, width), 0.0f, 0.0f, width, false);
        Dialog::open(DIALOG, width, height, body, [] { shown.reset(); });
        shown = problem;
    }
}

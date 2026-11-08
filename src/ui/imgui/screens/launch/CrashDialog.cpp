#include "ui/imgui/screens/launch/CrashDialog.h"
#include "app/instances/Covers.h"
#include "app/launch/AskAi.h"
#include "app/launch/Crashes.h"
#include "app/launch/GameLog.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/launch/LogWindow.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/strings.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace CrashDialog {

    namespace blb = b_launcher_base;

    namespace {
        const char* const DIALOG = "crash";
        const char* const HINT = "请不要给别人发送这个窗口，点击“导出崩溃信息”，使用导出的文件去求助。";
        constexpr float WIDTH = 520.0f;
        constexpr float PAD = 24.0f;
        constexpr float LINE_H = 20.0f;
        constexpr float LABEL_H = 24.0f;
        constexpr float SECTION_GAP = 16.0f;
        constexpr float CAUSE_GAP = 8.0f;
        constexpr float WINDOW_MARGIN = 48.0f;
        constexpr float EDGE = 12.0f;
        constexpr float BUTTON_GAP = 10.0f;
        constexpr float GROUP_GAP = 24.0f;

        std::shared_ptr<const Crashes::Report> shown;

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

        void exportReport(const std::shared_ptr<const Crashes::Report>& report) {
            std::optional<std::filesystem::path> target = Platform::pickSaveAs(
                    Platform::desktopDirectory() / blb::path_from_utf8(Crashes::fileName(*report)));
            if (target) {
                Crashes::save(report, *target);
            }
        }

        float header(float left, float top, float inner, bool draw) {
            if (draw) {
                UiRender::textY("游戏崩溃了", left, top, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                                Page::TITLE_SIZE);
            }
            return lines(wrapped(HINT, inner, 3), left, top + 26.0f, UiTheme::textMuted(), draw) + SECTION_GAP;
        }

        float causes(const Crashes::Report& report, size_t detailLines, float left, float top, float inner, bool draw) {
            if (report.causes.empty()) {
                return lines({"没有识别出具体原因。"}, left, top, UiTheme::textSecondary(), draw) + SECTION_GAP;
            }
            if (draw) {
                UiRender::textY("可能的原因", left, top, LABEL_H - 6.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            top += LABEL_H;
            for (const Crashes::Cause& cause : report.causes) {
                if (draw) {
                    UiRender::textY(UiFont::fitEnd(cause.title, UiFont::MEDIUM, UiWidgets::TEXT_BODY, inner), left, top,
                                    LINE_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                }
                top = lines(wrapped(cause.detail, inner, detailLines), left, top + LINE_H, UiTheme::textSecondary(),
                            draw) +
                        CAUSE_GAP;
            }
            return top - CAUSE_GAP + SECTION_GAP;
        }

        uint32_t logoOf(const AskAi::Service& service) {
            int side = (int) std::lround(16.0f * UiRender::stableScale() * UiRender::pixelScale());
            return Textures::of(Covers::logo(service.icon, side, !UiTheme::isLight()));
        }

        float widthOf() {
            return std::min(WIDTH, MainWindow::width() - 2.0f * EDGE);
        }

        std::string askText() {
            std::string chosen = AskAi::chosen();
            return Crashes::asking() ? std::string("正在上传…")
                    : chosen.empty() ? std::string("问 AI")
                                     : "问 " + AskAi::nameOf(chosen);
        }

        void askButton(const std::shared_ptr<const Crashes::Report>& report, float x, float top, float w) {
            std::string chosen = AskAi::chosen();
            if (chosen.empty()) {
                for (const AskAi::Service& service : AskAi::services()) {
                    logoOf(service);
                }
            }
            std::string text = askText();
            if (!Page::button("crash#ask", text, x, top, w, false, Crashes::asking(), 0xf544)) {
                return;
            }
            if (!chosen.empty()) {
                Crashes::ask(report, chosen);
                return;
            }
            NewUi::openMenu(
                    "crash#ai",
                    [report] {
                        MenuItems items;
                        for (const AskAi::Service& service : AskAi::services()) {
                            std::string key = service.key;
                            auto item = ActionMenuItem::make(service.name, 0xf544,
                                                             [report, key] { Crashes::ask(report, key); });
                            if (uint32_t texture = logoOf(service); texture != 0) {
                                item->image(texture, 0.0f);
                            }
                            items.push_back(item);
                        }
                        return items;
                    },
                    x, top + Page::FIELD_H + 4.0f, std::max(140.0f, w));
        }

        float buttons(const std::shared_ptr<const Crashes::Report>& report, float left, float top, float inner,
                      bool draw) {
            std::string log = "查看日志";
            float logW = Page::buttonWidth(log, 0xf15c);
            float askW = Page::buttonWidth(askText(), 0xf544);
            bool saving = Crashes::saving();
            std::string save = saving ? "正在导出…" : "导出崩溃信息";
            float saveW = std::max(116.0f, Page::buttonWidth(save, 0xf56e));
            std::string close = "关闭";
            float closeW = std::max(72.0f, Page::buttonWidth(close));
            bool grid = logW + BUTTON_GAP + askW + GROUP_GAP + closeW + BUTTON_GAP + saveW > inner;
            float rightTop = grid ? top + Page::FIELD_H + BUTTON_GAP : top;
            if (!draw) {
                return rightTop + Page::FIELD_H;
            }
            float cell = (inner - BUTTON_GAP) * 0.5f;
            if (grid) {
                logW = askW = closeW = saveW = cell;
            }
            if (Page::button("crash#log", log, left, top, logW, false, false, 0xf15c)) {
                GameLog::show(report->launch.log);
                LogWindow::open();
            }
            askButton(report, left + logW + BUTTON_GAP, top, askW);
            float saveX = left + inner - saveW;
            float closeX = saveX - BUTTON_GAP - closeW;
            if (Page::button("crash#close", close, closeX, rightTop, closeW, false, false)) {
                Dialog::close();
            } else if (Page::button("crash#export", save, saveX, rightTop, saveW, true, saving, 0xf56e)) {
                exportReport(report);
            }
            return rightTop + Page::FIELD_H;
        }

        float paint(const std::shared_ptr<const Crashes::Report>& report, size_t detailLines, float x, float y, float w,
                    bool draw) {
            float left = x + PAD;
            float inner = w - 2.0f * PAD;
            float top = header(left, y + PAD, inner, draw);
            top = causes(*report, detailLines, left, top, inner, draw);
            return buttons(report, left, top, inner, draw) + PAD - y;
        }

        size_t fitting(const std::shared_ptr<const Crashes::Report>& report, float width) {
            float room = MainWindow::height() - WINDOW_MARGIN;
            for (size_t detailLines : {4u, 2u}) {
                if (paint(report, detailLines, 0.0f, 0.0f, width, false) <= room) {
                    return detailLines;
                }
            }
            return 1;
        }

        void body(float x, float y, float w, float h) {
            std::shared_ptr<const Crashes::Report> report = shown;
            if (report == nullptr) {
                Dialog::close();
                return;
            }
            float needed = paint(report, fitting(report, w), x, y, w, true);
            float width = widthOf();
            if (std::abs(width - w) > 0.5f) {
                needed = paint(report, fitting(report, width), 0.0f, 0.0f, width, false);
            }
            if (std::abs(needed - h) > 0.5f || std::abs(width - w) > 0.5f) {
                Dialog::resize(DIALOG, width, needed);
            }
        }
    }

    void open(std::shared_ptr<const Crashes::Report> report) {
        if (report == nullptr) {
            return;
        }
        float width = widthOf();
        float height = paint(report, fitting(report, width), 0.0f, 0.0f, width, false);
        Dialog::open(DIALOG, width, height, body, [] { shown.reset(); });
        shown = std::move(report);
    }
}

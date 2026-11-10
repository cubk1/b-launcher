#include "ui/imgui/screens/launch/CrashDialog.h"
#include "app/instances/Covers.h"
#include "app/launch/AskAi.h"
#include "app/launch/Crashes.h"
#include "app/launch/GameLog.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/launch/LogWindow.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/strings.hpp>

#include <qrcodegen.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CrashDialog {

    namespace blb = b_launcher_base;

    namespace {
        const char* const DIALOG = "crash";
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
        constexpr float CODE_BOX = 100.0f;

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

        float header(float left, float top, bool draw) {
            if (draw) {
                UiRender::textY("游戏崩溃了", left, top, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                                Page::TITLE_SIZE);
            }
            return top + 24.0f + SECTION_GAP;
        }

        struct Code {
            std::string text;
            int size = 0;
            std::vector<uint8_t> dark;
        };

        const Code& codeOf(const std::string& text) {
            static Code cached;
            if (cached.text == text) {
                return cached;
            }
            cached = Code{text, 0, {}};
            std::vector<uint8_t> temp(qrcodegen_BUFFER_LEN_MAX);
            std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX);
            if (!qrcodegen_encodeText(text.c_str(), temp.data(), qr.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                      qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
                return cached;
            }
            cached.size = qrcodegen_getSize(qr.data());
            cached.dark.resize((size_t) cached.size * cached.size);
            for (int y = 0; y < cached.size; y++) {
                for (int x = 0; x < cached.size; x++) {
                    cached.dark[(size_t) y * cached.size + x] = qrcodegen_getModule(qr.data(), x, y) ? 1 : 0;
                }
            }
            return cached;
        }

        void drawCode(const std::string& text, float x, float y) {
            const Code& code = codeOf(text);
            UiRender::roundRect(x, y, CODE_BOX, CODE_BOX, 8.0f, UiRender::color(255, 255, 255));
            if (code.size == 0) {
                return;
            }
            float module = std::floor(CODE_BOX / (float) (code.size + 8));
            float origin = std::floor((CODE_BOX - module * (float) code.size) * 0.5f);
            uint32_t ink = UiRender::color(0, 0, 0);
            for (int row = 0; row < code.size; row++) {
                int run = -1;
                for (int col = 0; col <= code.size; col++) {
                    bool dark = col < code.size && code.dark[(size_t) row * code.size + col] != 0;
                    if (dark && run < 0) {
                        run = col;
                    } else if (!dark && run >= 0) {
                        UiRender::rect(x + origin + (float) run * module, y + origin + (float) row * module,
                                       (float) (col - run) * module, module, ink);
                        run = -1;
                    }
                }
            }
        }

        float logLink(const std::shared_ptr<const Crashes::Report>& report, float left, float top, float inner,
                      bool draw) {
            Crashes::Upload state = Crashes::uploadOf(report);
            if (state == Crashes::Upload::None) {
                return top;
            }
            if (!draw) {
                return top + CODE_BOX + SECTION_GAP;
            }
            std::string link = Crashes::linkOf(report);
            if (state == Crashes::Upload::Done) {
                drawCode(link, left, top);
            } else {
                UiRender::roundRect(left, top, CODE_BOX, CODE_BOX, 8.0f, UiTheme::surfaceRaised());
                if (state == Crashes::Upload::Running) {
                    Page::spinner(left + CODE_BOX * 0.5f, top + CODE_BOX * 0.5f, 8.0f, UiTheme::accent());
                } else {
                    UiWidgets::icon(Ui::glyph(0xf071), left + CODE_BOX * 0.5f, top + CODE_BOX * 0.5f,
                                    UiTheme::textFaint(), 16.0f);
                }
            }
            float textX = left + CODE_BOX + 16.0f;
            float textW = std::max(0.0f, left + inner - textX);
            float y = top + (CODE_BOX - (18.0f + LINE_H + 10.0f + Page::FIELD_H)) * 0.5f;
            UiRender::textY("日志链接", textX, y, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            y += 18.0f;
            if (state == Crashes::Upload::Done) {
                std::string shown = UiFont::fitEnd(link, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, textW);
                float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                bool hovered = NewUi::hit(textX, y, shownW, LINE_H);
                uint32_t ink = hovered ? UiTheme::accent() : UiTheme::accentText();
                UiRender::textY(shown, textX, y, LINE_H, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (hovered) {
                    UiRender::hLine(textX, textX + shownW, y + LINE_H - 2.0f, ink);
                    UiCursor::hand();
                }
                if (NewUi::click(textX, y, shownW, LINE_H)) {
                    Platform::openUrl(link);
                }
            } else {
                std::string note = state == Crashes::Upload::Running ? "正在上传到 mclo.gs…" : "无法上传到 mclo.gs";
                UiRender::textY(note, textX, y, LINE_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            y += LINE_H + 10.0f;
            if (state == Crashes::Upload::Done) {
                std::string copy = "复制链接";
                if (Page::button("crash#copyLink", copy, textX, y, Page::buttonWidth(copy, 0xf0c5), false, false,
                                 0xf0c5) &&
                    Platform::setClipboardText(link)) {
                    Notices::add(Notices::Kind::Done, "已复制日志链接", 2000);
                }
            } else if (state == Crashes::Upload::Failed) {
                std::string retry = "重新上传";
                if (Page::button("crash#retryLink", retry, textX, y, Page::buttonWidth(retry, 0xf2f1), false, false,
                                 0xf2f1)) {
                    Crashes::share(report);
                }
            }
            return top + CODE_BOX + SECTION_GAP;
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
            float top = header(left, y + PAD, draw);
            top = causes(*report, detailLines, left, top, inner, draw);
            top = logLink(report, left, top, inner, draw);
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

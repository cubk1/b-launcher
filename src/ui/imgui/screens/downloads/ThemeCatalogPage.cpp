#include "ui/imgui/screens/downloads/ThemeCatalogPage.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/instances/ScreenshotViewer.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/theme/ThemeCatalog.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/widget/InputWidget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace ThemeCatalogPage {

    namespace {
        using ThemeCatalog::Entry;
        using ThemeCatalog::Local;

        const char* const SEARCH = "downloads#themes#search";
        constexpr float ROW = 56.0f;
        constexpr float ICON = 36.0f;
        constexpr float HEAD_ICON = 64.0f;
        constexpr float PREVIEW_GAP = 10.0f;
        constexpr float PREVIEW_RATIO = 638.0f / 1025.0f;

        float left = 0.0f;
        std::string opened;

        float mainX() {
            return left;
        }

        float mainW() {
            return Page::right() - left;
        }

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return text;
        }

        bool matches(const Entry& entry, const std::string& query) {
            if (query.empty()) {
                return true;
            }
            std::string needle = lower(query);
            for (const std::string* field : {&entry.name, &entry.author, &entry.description, &entry.id}) {
                if (lower(*field).find(needle) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        void icon(const Entry& entry, float x, float y, float size) {
            if (entry.icon.empty()) {
                UiRender::roundRect(x, y, size, size, 8.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(0xf1fc), x + size * 0.5f, y + size * 0.5f, UiTheme::textMuted(),
                                size * 0.36f);
                return;
            }
            DownloadsParts::webImage(entry.icon, x, y, size, size, 8.0f, true);
        }

        float tag(const std::string& text, float x, float centreY, float limit, uint32_t fill, uint32_t ink) {
            return DownloadsParts::tag(text, x, centreY, limit, fill, ink);
        }

        std::string sizeLabel(uint64_t size) {
            if (size == 0) {
                return {};
            }
            if (size < 1000000) {
                return std::to_string(std::max<uint64_t>(1, (size + 500) / 1000)) + " KB";
            }
            return std::to_string((size + 500000) / 1000000) + " MB";
        }

        float button(const std::string& id, const std::string& text, int glyph, float right, float cy, bool primary,
                     bool blocked, bool& pressed) {
            float w = Page::buttonWidth(text, glyph);
            pressed = Page::button(id, text, right - w, cy - Page::FIELD_H * 0.5f, w, primary, blocked, glyph);
            return right - w;
        }

        float actions(const Entry& entry, float right, float cy, bool blocked) {
            std::string id = "downloads#themes#" + entry.id;
            if (ThemeCatalog::busy(entry.id)) {
                Page::progressRing(right - 10.0f, cy, ThemeCatalog::progress(entry.id));
                std::string text = "下载中";
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(text, right - 26.0f - w, cy - 12.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return right - 26.0f - w;
            }
            bool pressed = false;
            bool current = ThemeRuntime::current() == entry.id;
            Local local = ThemeCatalog::local(entry);
            if (local == Local::Missing) {
                float x = button(id + "#apply", "下载并应用", 0xf019, right, cy, true, blocked, pressed);
                if (pressed) {
                    ThemeCatalog::install(entry.id, true);
                }
                x = button(id + "#get", "下载", 0, x - 8.0f, cy, false, blocked, pressed);
                if (pressed) {
                    ThemeCatalog::install(entry.id, false);
                }
                return x;
            }
            float x = right;
            if (local == Local::Current && current) {
                std::string text = "使用中";
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(text, right - w, cy - 12.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                UiWidgets::check(right - w - 12.0f, cy, UiTheme::accent(), 9.0f);
                return right - w - 22.0f;
            }
            if (local == Local::Current) {
                x = button(id + "#use", "应用", 0xf00c, x, cy, true, blocked, pressed);
                if (pressed) {
                    Appearance::setTheme(entry.id);
                }
                return x;
            }
            x = button(id + "#update", "更新", 0xf2f1, x, cy, true, blocked, pressed);
            if (pressed) {
                ThemeCatalog::install(entry.id, false);
            }
            if (!current) {
                x = button(id + "#use", "应用", 0, x - 8.0f, cy, false, blocked, pressed);
                if (pressed) {
                    Appearance::setTheme(entry.id);
                }
            }
            return x;
        }

        float stateTags(const Entry& entry, float x, float centreY, float limit) {
            Local local = ThemeCatalog::local(entry);
            if (local == Local::Missing) {
                return x;
            }
            bool current = ThemeRuntime::current() == entry.id;
            std::string text = local == Local::Current ? (current ? "使用中" : "已安装") : "可更新";
            uint32_t fill = local == Local::Current ? UiTheme::accentSoft(40) : UiTheme::surfaceRaised();
            uint32_t ink = local == Local::Current ? UiTheme::accentText() : UiTheme::warningInk();
            float w = tag(text, x, centreY, limit, fill, ink);
            return w > 0.0f ? x + w + 4.0f : x;
        }

        void row(const Entry& entry, float x, float y, float w, int index, int count, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW);
            float hover = UiState::motion("downloads#themes#" + entry.id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, ROW, DownloadsParts::CARD_RADIUS, UiTheme::overlay(Ui::round(14 * hover)),
                                    Page::rowCorners(index, count));
            }
            icon(entry, x + 12.0f, y + (ROW - ICON) * 0.5f, ICON);
            float actionX = actions(entry, x + w - 14.0f, y + ROW * 0.5f, blocked);
            float textX = x + 12.0f + ICON + 12.0f;
            float limit = actionX - 14.0f;
            float titleY = entry.description.empty() ? y + (ROW - 20.0f) * 0.5f : y + 9.0f;
            float centre = titleY + 10.0f;
            std::string title = UiFont::fitEnd(entry.name, UiFont::MEDIUM, UiWidgets::TEXT_BODY, limit - textX);
            UiRender::textY(title, textX, titleY, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float tagX = textX + UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 8.0f;
            if (!entry.version.empty()) {
                float tagW =
                        tag(entry.version, tagX, centre, limit, UiTheme::surfaceRaised(), UiTheme::textSecondary());
                tagX += tagW > 0.0f ? tagW + 4.0f : 0.0f;
            }
            tagX = stateTags(entry, tagX, centre, limit);
            if (!entry.author.empty() && tagX + 30.0f < limit) {
                std::string author = UiFont::fitEnd(entry.author, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, limit - tagX);
                UiRender::textY(author, tagX + 2.0f, titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            if (!entry.description.empty()) {
                UiRender::textY(UiFont::fitEnd(entry.description, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, limit - textX),
                                textX, y + 30.0f, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (!blocked && NewUi::click(x, y, w, ROW)) {
                UiSound::request(UiSound::PAGE);
                opened = entry.id;
            }
        }

        void message(const std::string& text, bool loading, float top) {
            DownloadsParts::message(text, UiTheme::textMuted(), top, MainWindow::height() - 14.0f, loading);
        }

        void header(bool covered) {
            float top = Page::top();
            float searchW = std::min(260.0f, mainW() * 0.5f);
            float sx = mainX();
            bool focused = UiInput::isFocused(SEARCH);
            UiRender::roundRect(sx, top, searchW, Page::FIELD_H, 7.0f, UiTheme::overlay(10));
            UiRender::outline(sx, top, searchW, Page::FIELD_H, 7.0f,
                              focused ? UiTheme::accent() : UiTheme::border(UiTheme::outlineSoft()));
            UiWidgets::icon(Ui::glyph(0xf002), sx + 12.0f, top + Page::FIELD_H * 0.5f, UiTheme::textFaint(), 9.0f);
            InputWidget input(SEARCH);
            input.area(sx + 24.0f, searchW - 30.0f, UiWidgets::TEXT_CONTROL, 13.0f)
                    .limit(64)
                    .hint("搜索启动器主题")
                    .bounds(sx, top, searchW, Page::FIELD_H)
                    .block(covered);
            input.paint();
            bool loading = ThemeCatalog::status() == ThemeCatalog::Status::Loading;
            std::string text = "检查更新";
            float w = Page::buttonWidth(text, 0xf2f1);
            if (Page::button("downloads#themes#refresh", text, Page::right() - w, top, w, false, covered || loading,
                             0xf2f1)) {
                ThemeCatalog::refresh();
            }
        }

        void list(bool covered) {
            header(covered);
            float top = Page::top() + Page::FIELD_H + 10.0f;
            ThemeCatalog::Status status = ThemeCatalog::status();
            const std::vector<Entry>& all = ThemeCatalog::entries();
            if (all.empty()) {
                if (status == ThemeCatalog::Status::Failed) {
                    message("无法加载主题目录：" + ThemeCatalog::error(), false, top);
                } else if (status == ThemeCatalog::Status::Ready) {
                    message("目录里还没有主题", false, top);
                } else {
                    message("正在加载主题目录", true, top);
                }
                return;
            }
            std::string query = Ui::trim(UiInput::get(SEARCH));
            std::vector<const Entry*> shown;
            for (const Entry& entry : all) {
                if (matches(entry, query)) {
                    shown.push_back(&entry);
                }
            }
            if (shown.empty()) {
                message("没有找到匹配的主题", false, top);
                return;
            }
            int count = (int) shown.size();
            float bottom = MainWindow::height() - 14.0f;
            Page::Scroll body("downloads#themes#list", top, bottom, count * ROW);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y0 = body.top - body.offset;
            Page::card(mainX(), y0, mainW(), count * ROW);
            for (int i = 0; i < count; i++) {
                float y = y0 + i * ROW;
                if (Page::shown(y, ROW)) {
                    row(*shown[i], mainX(), y, mainW(), i, count, blocked);
                }
            }
            body.end();
        }

        void detail(const Entry& entry, bool covered) {
            float top = Page::top();
            if (Page::iconButton("downloads#themes#back", 0xf053, "返回", mainX(), top, covered)) {
                opened.clear();
                return;
            }
            UiRender::textY("启动器主题", mainX() + 36.0f, top, 28.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float width = mainW();
            std::vector<std::string> lines = entry.description.empty()
                    ? std::vector<std::string>()
                    : UiFont::wrap(entry.description, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, width - 8.0f);
            float previewW = std::floor((width - PREVIEW_GAP) * 0.5f);
            float previewH = std::round(previewW * PREVIEW_RATIO);
            int previewRows = ((int) entry.previews.size() + 1) / 2;
            float headH = HEAD_ICON + 18.0f;
            float textH = lines.empty() ? 0.0f : lines.size() * 22.0f + 12.0f;
            float galleryH = previewRows > 0 ? previewRows * (previewH + PREVIEW_GAP) : 0.0f;
            float bodyTop = top + 40.0f;
            Page::Scroll body("downloads#themes#detail#" + entry.id, bodyTop, MainWindow::height() - 14.0f,
                              headH + textH + galleryH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            float x = mainX();
            icon(entry, x, y, HEAD_ICON);
            float actionX = actions(entry, x + width, y + HEAD_ICON * 0.5f, blocked);
            float textX = x + HEAD_ICON + 16.0f;
            float limit = actionX - 16.0f;
            std::string name = UiFont::fitEnd(entry.name, UiFont::BOLD, UiWidgets::TEXT_TITLE + 3.0f, limit - textX);
            UiRender::textY(name, textX, y + 6.0f, 26.0f, UiTheme::textPrimary(), UiFont::BOLD,
                            UiWidgets::TEXT_TITLE + 3.0f);
            float tagX = textX + UiFont::width(name, UiFont::BOLD, UiWidgets::TEXT_TITLE + 3.0f) + 10.0f;
            stateTags(entry, tagX, y + 19.0f, limit);
            std::vector<std::string> facts;
            if (!entry.author.empty()) {
                facts.push_back(entry.author);
            }
            if (!entry.version.empty()) {
                facts.push_back("版本 " + entry.version);
            }
            if (!entry.updated.empty()) {
                facts.push_back("更新于 " + DownloadsParts::shortDate(entry.updated));
            }
            if (std::string size = sizeLabel(entry.size); !size.empty()) {
                facts.push_back(size);
            }
            float factX = textX;
            for (const std::string& fact : facts) {
                float w = UiFont::width(fact, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (factX + w > limit) {
                    break;
                }
                UiRender::textY(fact, factX, y + 36.0f, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                factX += w + 16.0f;
            }
            y += headH;
            for (const std::string& line : lines) {
                UiRender::textY(line, x + 2.0f, y, 22.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                y += 22.0f;
            }
            if (!lines.empty()) {
                y += 12.0f;
            }
            for (size_t i = 0; i < entry.previews.size(); i++) {
                float px = x + (i % 2 == 0 ? 0.0f : previewW + PREVIEW_GAP);
                float py = y + (float) (i / 2) * (previewH + PREVIEW_GAP);
                if (!Page::shown(py, previewH)) {
                    continue;
                }
                DownloadsParts::webImage(entry.previews[i], px, py, previewW, previewH, 10.0f, true);
                bool hovered = !blocked && NewUi::hit(px, py, previewW, previewH);
                float lit = UiState::motion("downloads#themes#shot#" + std::to_string(i), hovered ? 1.0f : 0.0f, 20.0f);
                if (lit > 0.001f) {
                    UiRender::roundRect(px, py, previewW, previewH, 10.0f, UiTheme::overlay(Ui::round(18 * lit)));
                    UiRender::outline(px, py, previewW, previewH, 10.0f,
                                      UiRender::mix(UiTheme::clear(), UiTheme::accent(), lit));
                }
                if (hovered) {
                    UiCursor::hand();
                    if (NewUi::click(px, py, previewW, previewH)) {
                        UiSound::request(UiSound::PAGE);
                        std::vector<ScreenshotViewer::Picture> pictures;
                        for (const std::string& preview : entry.previews) {
                            pictures.push_back({preview, preview, entry.name, std::string()});
                        }
                        ScreenshotViewer::openWeb(std::move(pictures), (int) i);
                    }
                }
            }
            body.end();
        }
    }

    void draw(float x, bool covered) {
        left = x;
        Page::Column column(x);
        if (ThemeCatalog::status() == ThemeCatalog::Status::Idle) {
            ThemeCatalog::refresh();
        }
        const Entry* entry = opened.empty() ? nullptr : ThemeCatalog::find(opened);
        if (entry != nullptr) {
            detail(*entry, covered);
            return;
        }
        opened.clear();
        list(covered);
    }
}

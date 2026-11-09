#include "ui/imgui/screens/versions/VersionsScreen.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/screens/instances/InstancePage.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/widget/InputWidget.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/strings.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace VersionsScreen {

    namespace {
        constexpr float SEARCH_W = 260.0f;
        constexpr float SEARCH_H = 28.0f;
        constexpr float ROW = 46.0f;
        constexpr float GAP = 18.0f;
        const char* const SEARCH_ID = "versions#search";

        std::string detailOf(const Instances::Entry& entry) {
            if (!entry.info.valid) {
                return "无法读取版本信息";
            }
            std::string text = "Minecraft " + entry.info.minecraft_version;
            for (const auto& loader : entry.info.loaders) {
                if (!b_launcher_base::install::is_primary_loader(loader.type)) {
                    continue;
                }
                text += " · " + Installer::loaderName(loader.type);
                if (!loader.version.empty()) {
                    text += " " + loader.version;
                }
                break;
            }
            return text;
        }

        void row(const Instances::Entry& entry, float y, int corners, bool current, bool blocked) {
            float x = Page::left();
            float w = Page::width();
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW);
            float hover = UiState::motion("versions#row#" + entry.key, hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, ROW, 12.0f, UiTheme::overlay(Ui::round(12 * hover)), corners);
            }
            uint32_t image = InstancePage::icon(entry);
            if (image != 0) {
                UiRender::texture(image, x + 16.0f, y + (ROW - 24.0f) * 0.5f, 24.0f, 24.0f);
            } else {
                UiWidgets::icon(Ui::glyph(0xf071), x + 28.0f, y + ROW * 0.5f, UiTheme::danger(), 13.0f);
            }

            const float textX = x + 52.0f;
            const float tagRoom = 92.0f;
            std::string name = UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                              std::max(0.0f, w - 52.0f - tagRoom));
            UiRender::textY(name, textX, y + 7.0f, 20.0f,
                            entry.info.valid ? UiTheme::textPrimary() : UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            std::string detail = UiFont::fitEnd(detailOf(entry), UiFont::MEDIUM, UiWidgets::TEXT_SMALL,
                                                std::max(0.0f, w - 52.0f - tagRoom));
            UiRender::textY(detail, textX, y + 26.0f, 16.0f,
                            entry.info.valid ? UiTheme::textMuted() : UiTheme::danger(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);

            float right = x + w - 16.0f;
            if (current) {
                std::string tag = "使用中";
                float tagW = UiFont::width(tag, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(tag, right - tagW, y, ROW, UiTheme::accentText(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                UiWidgets::check(right - tagW - 14.0f, y + ROW * 0.5f, UiTheme::accentText(), 9.0f);
            } else if (Game::state(entry.key) != Game::State::Idle) {
                std::string tag = "运行中";
                float tagW = UiFont::width(tag, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(tag, right - tagW, y, ROW, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            } else {
                UiWidgets::icon(Ui::glyph(0xf054), right - 4.0f, y + ROW * 0.5f,
                                UiRender::mix(UiTheme::textFaint(), UiTheme::textPrimary(), hover), 9.0f);
            }

            if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
                NewUi::rightClick = false;
                UiSound::request(UiSound::PAGE);
                std::string key = entry.key;
                NewUi::openMenu(
                        "versions#menu#" + key,
                        [key] {
                            MenuItems items;
                            items.push_back(ActionMenuItem::make("切换到版本", 0xf00c, [key] {
                                                Instances::select(key);
                                                MainWindow::showHome();
                                            })->enabledIf([key] { return Instances::selected() != key; }));
                            items.push_back(ActionMenuItem::make("实例设置", 0xf013,
                                                                 [key] { MainWindow::openVersionSettings(key); }));
                            return items;
                        },
                        NewUi::mouseX, NewUi::mouseY, 170.0f);
            }
            if (!blocked && NewUi::click(x, y, w, ROW)) {
                if (!current) {
                    UiSound::request(UiSound::PAGE);
                    Instances::select(entry.key);
                }
                MainWindow::showHome();
            }
        }

        bool matches(const Instances::Entry& entry, const std::string& query) {
            return query.empty() || b_launcher_base::icontains(entry.info.id, query) ||
                   b_launcher_base::icontains(entry.info.minecraft_version, query);
        }
    }

    void open() {
        UiInput::set(SEARCH_ID, "");
    }

    void draw() {
        bool covered = NewUi::overlayCoversMouse();
        float left = Page::left();
        float right = Page::right();
        float width = right - left;

        size_t total = Instances::list().size();
        Page::title("选择版本", left, Page::top());
        if (total > 0) {
            std::string count = std::to_string(total) + " 个版本";
            float countW = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(count, right - countW, Page::top() + 4.0f, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }

        float searchY = Page::top() + 32.0f;
        float searchW = std::min(SEARCH_W, width);
        bool hasText = !UiInput::get(SEARCH_ID).empty();
        float clearRoom = hasText ? 26.0f : 0.0f;
        bool focused = UiInput::isFocused(SEARCH_ID);
        bool over = !covered && NewUi::hit(left, searchY, searchW, SEARCH_H);
        UiRender::roundRect(left, searchY, searchW, SEARCH_H, 8.0f, UiTheme::overlay(10));
        UiRender::outline(left, searchY, searchW, SEARCH_H, 8.0f,
                          focused ? UiTheme::accent()
                                  : UiRender::mix(UiTheme::border(UiTheme::outlineSoft()),
                                                  UiTheme::border(UiTheme::outline()), over ? 0.5f : 0.0f));
        std::string hint = hasText ? UiInput::get(SEARCH_ID) : std::string("搜索版本");
        UiWidgets::iconBeside(Ui::glyph(0xf002), left + 16.0f, hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                              searchY, SEARCH_H, focused ? UiTheme::textSecondary() : UiTheme::textFaint(), 11.0f);
        InputWidget input(SEARCH_ID);
        input.area(left + 31.0f, searchW - 43.0f - clearRoom, UiWidgets::TEXT_CONTROL, 14.0f)
                .limit(64)
                .hint("搜索版本")
                .bounds(left, searchY, searchW - clearRoom, SEARCH_H)
                .block(covered);
        input.paint();
        if (hasText) {
            float clearX = left + searchW - 16.0f;
            bool clearOver = !covered && NewUi::hit(clearX - 10.0f, searchY, 20.0f, SEARCH_H);
            UiWidgets::icon(Ui::glyph(0xf00d), clearX, searchY + SEARCH_H * 0.5f,
                            clearOver ? UiTheme::textPrimary() : UiTheme::textFaint(), 9.0f);
            if (clearOver && NewUi::click(clearX - 10.0f, searchY, 20.0f, SEARCH_H)) {
                UiSound::request(UiSound::RELEASE);
                UiInput::set(SEARCH_ID, "");
            }
        }

        std::string query = Ui::trim(UiInput::get(SEARCH_ID));
        const std::vector<Instances::Source>& sources = Instances::sources();
        std::vector<std::vector<const Instances::Entry*>> grouped(sources.size());
        size_t matched = 0;
        for (const Instances::Entry& entry : Instances::list()) {
            if (!matches(entry, query)) {
                continue;
            }
            matched++;
            for (size_t i = 0; i < sources.size(); i++) {
                if (sources[i].key == entry.source) {
                    grouped[i].push_back(&entry);
                }
            }
        }

        float listTop = searchY + SEARCH_H + 18.0f;
        float bottom = MainWindow::height() - 14.0f;
        if (total == 0) {
            if (Instances::loading()) {
                Page::spinner((left + right) * 0.5f, (listTop + bottom) * 0.5f, 10.0f, UiTheme::accent());
                return;
            }
            std::string text = "还没有安装任何版本";
            Page::centredText(text, (left + right) * 0.5f, (listTop + bottom) * 0.5f - 26.0f, 22.0f,
                              UiTheme::textMuted(), UiWidgets::TEXT_BODY);
            float buttonW = std::max(88.0f, Page::buttonWidth("下载游戏"));
            if (Page::button("versions#install", "下载游戏", (left + right) * 0.5f - buttonW * 0.5f,
                             (listTop + bottom) * 0.5f + 4.0f, buttonW, true, covered)) {
                MainWindow::startNewInstance();
            }
            return;
        }

        float contentH = 0.0f;
        for (size_t i = 0; i < sources.size(); i++) {
            if (grouped[i].empty() && !query.empty()) {
                continue;
            }
            contentH += Page::CAPTION_H + std::max<size_t>(1, grouped[i].size()) * ROW + GAP;
        }
        contentH = std::max(0.0f, contentH - GAP);

        Page::Scroll body("versions#list", listTop, bottom, contentH, left, width);
        bool outside = body.begin();
        bool blocked = covered || outside;
        float y = body.top - body.offset;
        for (size_t i = 0; i < sources.size(); i++) {
            const Instances::Source& source = sources[i];
            const std::vector<const Instances::Entry*>& entries = grouped[i];
            if (entries.empty() && !query.empty()) {
                continue;
            }
            if (Page::shown(y, Page::CAPTION_H + 1 * ROW)) {
                std::string label = source.label;
                float labelW = UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(label, left + 2.0f, y + 2.0f, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                std::string tail = source.hidden ? "已隐藏 · " + Platform::display(source.root)
                                                 : Platform::display(source.root);
                float tailX = left + 2.0f + labelW + 10.0f;
                float tailW = right - tailX;
                if (tailW > 24.0f) {
                    tail = UiFont::fitEnd(tail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, tailW);
                    float shownW = UiFont::width(tail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    UiRender::textY(tail, right - shownW, y + 2.0f, 16.0f, UiTheme::textFaint(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
            }
            float rowY = y + Page::CAPTION_H;
            if (entries.empty()) {
                std::string text = Instances::loading() ? "正在读取…" : "这里还没有版本";
                UiRender::textY(text, left + 16.0f, rowY, ROW, UiTheme::textFaint(), UiFont::MEDIUM,
                                UiWidgets::TEXT_BODY);
            } else {
                Page::listCard(left, rowY, width, ROW, (int) entries.size());
                for (size_t r = 0; r < entries.size(); r++) {
                    const Instances::Entry& entry = *entries[r];
                    bool current = Instances::selected() == entry.key;
                    row(entry, rowY + r * ROW, Page::rowCorners((int) r, (int) entries.size()), current, blocked);
                }
            }
            y += Page::CAPTION_H + std::max<size_t>(1, entries.size()) * ROW + GAP;
        }
        if (matched == 0 && !query.empty()) {
            std::string text = "没有匹配的版本";
            Page::centredText(text, (left + right) * 0.5f, body.top + 40.0f, 22.0f, UiTheme::textFaint(),
                              UiWidgets::TEXT_BODY);
        }
        body.end();
    }
}

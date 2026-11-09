#include "ui/imgui/screens/settings/ThemeManager.h"

#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"
#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/ShellLayout.h"
#include "ui/imgui/screens/theme/ThemeAssets.h"
#include "ui/imgui/screens/theme/ThemeFonts.h"
#include "ui/imgui/screens/theme/ThemeSounds.h"
#include "ui/imgui/screens/theme/ThemeImport.h"
#include "ui/imgui/screens/theme/ThemePreview.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeShaders.h"
#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/theme/ColorMath.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/SliderWidget.h"
#include "ui/imgui/widget/SwatchWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <core/base/strings.hpp>

#if defined(_WIN32)
#include "ui/exui/Skins.h"
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace ThemeManager {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float CARD_MIN = 196.0f;
        constexpr float CARD_GAP = 14.0f;
        constexpr float CARD_PAD = 8.0f;
        constexpr float NAME_H = 22.0f;
        constexpr float SELECT_W = 180.0f;
        constexpr float TRACK_W = 200.0f;
        constexpr float BAR_H = 4.0f;
        const char* const PROBLEMS_DIALOG = "theme#problems";
        const char* const DELETE_DIALOG = "theme#delete";

        std::string dialogTheme;
        float sectionLeft = 0.0f;
        float sectionWidth = 0.0f;

        float thumbHeight(float cardW) {
            return std::round((cardW - 2.0f * CARD_PAD) * MainWindow::MIN_HEIGHT / MainWindow::MIN_WIDTH);
        }

        float cardHeight(float cardW) {
            return CARD_PAD + thumbHeight(cardW) + 6.0f + NAME_H + CARD_PAD;
        }

        const char* const MELODY = "forgotten-melody";

        bool melodyChosen() {
            return std::string(UiStyles::current().key) == MELODY;
        }

        int melodyIndex(const std::vector<ThemeStore::Entry>& entries) {
#if defined(_WIN32)
            int index = 0;
            while (index < (int) entries.size() && entries[(size_t) index].builtIn) {
                index++;
            }
            return index;
#else
            (void) entries;
            return -1;
#endif
        }

        std::vector<std::string> currentProblems() {
            std::vector<std::string> out = ThemeRuntime::problems();
            for (const std::vector<std::string>& more :
                 {ThemeAssets::problems(), ThemeShaders::problems(), UiFont::problems(), ThemeSounds::problems()}) {
                out.insert(out.end(), more.begin(), more.end());
            }
            return out;
        }

        void select(const std::string& id, const std::shared_ptr<Choice>& choice, float rowY, bool blocked) {
            SelectWidget widget(id, choice);
            widget.bounds(Page::controlRight() - SELECT_W, rowY + Page::FIELD_Y, SELECT_W, Page::FIELD_H)
                    .block(blocked);
            widget.paint();
        }

        void toggle(const std::string& id, std::function<bool()> read, std::function<void(bool)> write, float rowY,
                    bool blocked) {
            auto flag = std::make_shared<FlagBinding>(id, std::move(read), std::move(write));
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(sectionLeft, rowY, sectionWidth, Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void dialogTitle(const std::string& text, float x, float y) {
            UiRender::textY(text, x + 20.0f, y + 16.0f, 24.0f, UiTheme::textPrimary(), UiFont::BOLD,
                            UiWidgets::TEXT_TITLE);
        }

        void problemsBody(float x, float y, float w, float h) {
            dialogTitle("主题的问题", x, y);
            const ThemePreview::Data& data = ThemePreview::of(dialogTheme);
            std::vector<std::string> lines = dialogTheme == ThemeRuntime::current() ? currentProblems() : data.problems;
            float lineY = y + 50.0f;
            float bottom = y + h - 24.0f - Page::FIELD_H;
            for (const std::string& line : lines) {
                if (lineY + 22.0f > bottom) {
                    break;
                }
                UiRender::textY(UiFont::fitEnd(line, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 40.0f), x + 20.0f,
                                lineY, 22.0f, UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                lineY += 22.0f;
            }
            if (Page::button("theme#problems#close", "关闭", x + w - 20.0f - 84.0f, y + h - 20.0f - Page::FIELD_H,
                             84.0f, true, false)) {
                Dialog::close();
            }
        }

        void deleteBody(float x, float y, float w, float h) {
            const ThemeStore::Entry* entry = ThemeStore::find(dialogTheme);
            dialogTitle("删除主题", x, y);
            std::string line = UiFont::fitEnd("删除 " + (entry != nullptr ? entry->name : dialogTheme) + " 后无法恢复",
                                              UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 40.0f);
            UiRender::textY(line, x + 20.0f, y + 50.0f, 22.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float buttonY = y + h - 20.0f - Page::FIELD_H;
            if (Page::button("theme#delete#cancel", "取消", x + w - 20.0f - 180.0f, buttonY, 84.0f, false, false)) {
                Dialog::close();
                return;
            }
            if (Page::button("theme#delete#ok", "删除", x + w - 20.0f - 88.0f, buttonY, 88.0f, true, false)) {
                std::string id = dialogTheme;
                if (ThemeRuntime::current() == id) {
                    Appearance::setTheme("plain");
                }
                if (ThemeStore::remove(id)) {
                    ThemePreview::forget();
                    Notices::add(Notices::Kind::Done, "已删除主题", 2500);
                } else {
                    Notices::add(Notices::Kind::Failed, "无法删除这个主题", 3000);
                }
                Dialog::close();
            }
        }

        void themeMenu(const ThemeStore::Entry& entry, float x, float y) {
            std::string id = entry.id;
            bool builtIn = entry.builtIn;
            NewUi::openMenu(
                    "settings#themes#menu#" + id,
                    [id, builtIn] {
                        MenuItems items;
                        if (ThemeRuntime::current() != id) {
                            items.push_back(ActionMenuItem::make("应用", 0xf00c, [id] { Appearance::setTheme(id); }));
                        }
                        items.push_back(ActionMenuItem::make("复制为新主题", 0xf0c5, [id] {
                            if (std::optional<std::string> copy = ThemeStore::duplicate(id)) {
                                ThemePreview::forget();
                                Notices::add(Notices::Kind::Done, "已复制为 " + *copy, 3000);
                            } else {
                                Notices::add(Notices::Kind::Failed, "无法复制这个主题", 3000);
                            }
                        }));
                        if (!builtIn) {
                            items.push_back(ActionMenuItem::make("打开所在文件夹", 0xf07c,
                                                                 [id] { Platform::open(ThemeStore::location(id)); }));
                        }
                        const ThemePreview::Data& data = ThemePreview::of(id);
                        bool troubled =
                                id == ThemeRuntime::current() ? !currentProblems().empty() : !data.problems.empty();
                        if (troubled) {
                            items.push_back(ActionMenuItem::make("查看问题", 0xf071, [id] {
                                dialogTheme = id;
                                Dialog::open(PROBLEMS_DIALOG, 520.0f, 300.0f, problemsBody);
                            }));
                        }
                        if (!builtIn) {
                            items.push_back(ActionMenuItem::make("删除", 0xf1f8, [id] {
                                                dialogTheme = id;
                                                Dialog::open(DELETE_DIALOG, 380.0f, 140.0f, deleteBody);
                                            })->dangerous());
                        }
                        return items;
                    },
                    x, y, 180.0f);
        }

#if defined(_WIN32)
        uint32_t melodyCover(int& width, int& height) {
            static uint32_t texture = 0;
            static int coverW = 0;
            static int coverH = 0;
            if (texture == 0) {
                std::span<const std::uint8_t> bytes = exui::SkinBytes(exui::Skin::MainBackground);
                texture = UiImages::uploadEncoded(bytes.data(), (int) bytes.size(), &coverW, &coverH);
            }
            width = coverW;
            height = coverH;
            return texture;
        }

        void melodyCard(float x, float y, float w, bool blocked) {
            float h = cardHeight(w);
            bool chosen = melodyChosen();
            std::string key = "settings#themes#card#melody";
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            float hover = UiState::motion(key + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
            Page::card(x, y, w, h);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::overlay(Ui::round(10 * hover)));
            }
            if (chosen) {
                UiRender::outline(x, y, w, h, 12.0f, UiTheme::accent(), 0xF, 2.0f);
            }
            float thumbW = w - 2.0f * CARD_PAD;
            float thumbH = thumbHeight(w);
            int coverW = 0;
            int coverH = 0;
            uint32_t cover = melodyCover(coverW, coverH);
            if (cover != 0 && coverW > 0 && coverH > 0) {
                float u = 1.0f;
                float v = 1.0f;
                float aspect = thumbW / thumbH;
                float image = (float) coverW / (float) coverH;
                if (aspect > image) {
                    v = image / aspect;
                } else {
                    u = aspect / image;
                }
                ShaderUtil::roundTex(UiRender::transformX(x + CARD_PAD), UiRender::transformY(y + CARD_PAD),
                                     UiRender::transformW(thumbW), UiRender::transformW(thumbH),
                                     UiRender::transformW(MainWindow::miniatureRadius(thumbW, thumbH)), cover,
                                     (1.0f - u) * 0.5f, (1.0f - v) * 0.5f, (1.0f + u) * 0.5f, (1.0f + v) * 0.5f,
                                     UiRender::applyAlpha(0xFFFFFFFFu));
            }
            float textY = y + CARD_PAD + thumbH + 6.0f;
            UiRender::textY("忘却的旋律", x + CARD_PAD + 2.0f, textY, NAME_H, UiTheme::textPrimary(), UiFont::BOLD,
                            UiWidgets::TEXT_CONTROL);
            float right = x + w - CARD_PAD;
            if (chosen && std::string(UiStyles::running().key) != MELODY) {
                std::string text = "立即重启";
                float buttonW = Page::buttonWidth(text, 0xf2f1);
                if (Page::button(key + "#restart", text, right - buttonW, textY, buttonW, true,
                                 blocked || Services::busy(), 0xf2f1, NAME_H)) {
                    if (!Platform::restart()) {
                        Notices::add(Notices::Kind::Failed, "重启失败，请手动重新打开启动器", 5000);
                    }
                    return;
                }
            } else {
                std::string author = "B Launcher";
                UiRender::textY(author, right - UiFont::width(author, UiFont::MEDIUM, UiWidgets::TEXT_SMALL), textY,
                                NAME_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (!blocked && !chosen && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::TICK);
                UiStyles::set(MELODY);
            }
        }
#endif

        void card(const ThemeStore::Entry& entry, float x, float y, float w, bool blocked) {
            float h = cardHeight(w);
            bool current = !melodyChosen() && ThemeRuntime::current() == entry.id;
            std::string key = "settings#themes#card#" + entry.id;
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            float hover = UiState::motion(key + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
            Page::card(x, y, w, h);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::overlay(Ui::round(10 * hover)));
            }
            if (current) {
                UiRender::outline(x, y, w, h, 12.0f, UiTheme::accent(), 0xF, 2.0f);
            }
            const ThemePreview::Data& data = ThemePreview::of(entry.id);
            float thumbW = w - 2.0f * CARD_PAD;
            float thumbH = thumbHeight(w);
            if (UiRender::alphaMul > 0.001f) {
                ThemePreview::Scope scope(entry.id);
                MainWindow::miniature(x + CARD_PAD, y + CARD_PAD, thumbW, thumbH, &scope.layout(), true);
            }
            float textY = y + CARD_PAD + thumbH + 6.0f;
            float dotsX = x + w - CARD_PAD - 26.0f;
            std::string author = data.facts.author;
            float nameRoom = dotsX - 6.0f - (x + CARD_PAD + 2.0f);
            float authorW = author.empty()
                    ? 0.0f
                    : std::min(nameRoom * 0.45f, UiFont::width(author, UiFont::MEDIUM, UiWidgets::TEXT_SMALL));
            std::string name = UiFont::fitEnd(entry.name, UiFont::BOLD, UiWidgets::TEXT_CONTROL,
                                              nameRoom - (authorW > 0.0f ? authorW + 8.0f : 0.0f));
            UiRender::textY(name, x + CARD_PAD + 2.0f, textY, NAME_H, UiTheme::textPrimary(), UiFont::BOLD,
                            UiWidgets::TEXT_CONTROL);
            if (authorW > 0.0f) {
                std::string shown = UiFont::fitEnd(author, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, authorW);
                UiRender::textY(shown, dotsX - 6.0f - UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_SMALL),
                                textY, NAME_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            bool menuOpen = NewUi::popupOpen && NewUi::menuOwner == "settings#themes#menu#" + entry.id;
            bool dotsHovered = !blocked && NewUi::hit(dotsX, textY, 26.0f, 22.0f);
            UiWidgets::dotsButton(key + "#dots", dotsX, textY, menuOpen, dotsHovered);
            if (!blocked && NewUi::click(dotsX, textY, 26.0f, 22.0f)) {
                UiSound::request(UiSound::TICK);
                themeMenu(entry, dotsX + 26.0f - 180.0f, textY + 26.0f);
                return;
            }
            if (hovered && NewUi::rightClick && !NewUi::overlayCoversMouse()) {
                NewUi::rightClick = false;
                themeMenu(entry, NewUi::mouseX, NewUi::mouseY);
                return;
            }
            if (!blocked && !current && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::TICK);
                if (melodyChosen()) {
                    UiStyles::set("default");
                }
                Appearance::setTheme(entry.id);
            }
        }

        int settingRows() {
            return (int) ThemeRuntime::settings().size();
        }

        void settingRow(const ThemeSetting& setting, const nlohmann::json& values, float y, bool blocked) {
            std::string id = "settings#themes#setting#" + setting.id;
            std::string name = setting.id;
            Page::label(setting.label.empty() ? setting.id : setting.label, y);
            if (setting.type == "toggle") {
                toggle(
                        id,
                        [name] {
                            nlohmann::json now = ThemeRuntime::settingValues();
                            auto it = now.find(name);
                            return it != now.end() && it->is_boolean() && it->get<bool>();
                        },
                        [name](bool on) { ThemeRuntime::setSetting(name, on); }, y, blocked);
            } else if (setting.type == "slider") {
                auto value = std::make_shared<NumberBinding>(
                        id,
                        [name, setting] {
                            nlohmann::json now = ThemeRuntime::settingValues();
                            auto it = now.find(name);
                            return it != now.end() && it->is_number() ? it->get<double>() : setting.min;
                        },
                        [name](double picked) { ThemeRuntime::setSetting(name, picked); }, setting.min, setting.max,
                        setting.step > 0.0 ? setting.step : (setting.max - setting.min) / 100.0, "");
                SliderWidget widget(id, value);
                widget.compactTrack(Page::controlRight() - TRACK_W, y + (Page::ROW_H - BAR_H) * 0.5f, TRACK_W);
                widget.block(blocked);
                widget.paint();
            } else if (setting.type == "select") {
                std::vector<std::string> options = setting.options;
                select(id,
                       std::make_shared<ModeBinding>(
                               id,
                               [name] {
                                   nlohmann::json now = ThemeRuntime::settingValues();
                                   auto it = now.find(name);
                                   return it != now.end() && it->is_string() ? it->get<std::string>() : std::string();
                               },
                               [name](const std::string& picked) { ThemeRuntime::setSetting(name, picked); },
                               [options] { return options; }),
                       y, blocked);
            } else if (setting.type == "color") {
                auto color = std::make_shared<ColorBinding>(
                        id,
                        [name] {
                            nlohmann::json now = ThemeRuntime::settingValues();
                            auto it = now.find(name);
                            return it != now.end() && it->is_number() ? (uint32_t) it->get<double>() : 0xFF808080u;
                        },
                        [name](uint32_t argb) { ThemeRuntime::setSetting(name, ColorMath::format(argb)); });
                float swatchX = Page::controlRight() - 15.0f;
                SwatchWidget swatch(color);
                swatch.swatchAt(swatchX, y + Page::SWATCH_Y).picker(swatchX - 214.0f - 12.0f, y - 120.0f).hoverRing();
                swatch.block(blocked);
                swatch.paint();
            }
            (void) values;
        }

        constexpr int DISPLAY_ROWS = 5;

        void displayRows(float y, bool blocked) {
            Page::label("布局", y);
            select("settings#layout",
                   std::make_shared<ModeBinding>(
                           "settings#layout", [] { return ShellLayout::choice(); },
                           [](const std::string& option) { ShellLayout::setChoice(option); },
                           [] { return ShellLayout::choices(); }),
                   y, blocked);
            y += Page::ROW_H;

            Page::label("主题动画", y);
            toggle(
                    "settings#themeAnimation", [] { return ThemeRuntime::animationEnabled(); },
                    [](bool on) { ThemeRuntime::setAnimationEnabled(on); }, y, blocked);
            y += Page::ROW_H;

            Page::label("动画帧率", y);
            select("settings#themeFrameCap",
                   std::make_shared<ModeBinding>(
                           "settings#themeFrameCap", [] { return std::to_string(ThemeRuntime::frameCap()) + " FPS"; },
                           [](const std::string& option) { ThemeRuntime::setFrameCap(std::atoi(option.c_str())); },
                           [] { return std::vector<std::string>{"24 FPS", "30 FPS", "60 FPS"}; }),
                   y, blocked);
            y += Page::ROW_H;

            Page::label("主题字体大小", y);
            select("settings#themeFontScale",
                   std::make_shared<ModeBinding>(
                           "settings#themeFontScale",
                           [] { return std::to_string((int) std::lround(ThemeFonts::userScale() * 100.0f)) + "%"; },
                           [](const std::string& option) {
                               ThemeFonts::setUserScale((float) std::atoi(option.c_str()) / 100.0f);
                           },
                           [] { return std::vector<std::string>{"90%", "95%", "100%", "105%", "110%", "115%"}; }),
                   y, blocked);
            y += Page::ROW_H;

            Page::label("自定义着色器", y);
            if (ThemeShaders::blocked()) {
                std::string text = "重新启用";
                float w = Page::buttonWidth(text, 0xf2f1);
                float x = Page::controlRight() - w;
                std::string note = "已暂停";
                UiRender::textY(note, x - 12.0f - UiFont::width(note, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL), y,
                                Page::ROW_H, UiTheme::warningInk(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (Page::button("settings#shaderUnblock", text, x, y + Page::FIELD_Y, w, false, blocked, 0xf2f1)) {
                    ThemeShaders::unblock();
                }
            } else {
                toggle(
                        "settings#themeShaders", [] { return ThemeShaders::enabled(); },
                        [](bool on) { ThemeShaders::setEnabled(on); }, y, blocked);
            }
        }

        void topButtons(float x, float y, bool blocked) {
            std::string importText = ThemeImport::busy() ? "正在导入" : "导入主题";
            float w = Page::buttonWidth(importText, 0xf56f);
            if (Page::button("settings#themes#import", importText, x, y, w, true, blocked || ThemeImport::busy(),
                             0xf56f)) {
                std::vector<std::filesystem::path> picked =
                        Platform::pickFiles(std::filesystem::path(), L"选择主题", L"主题压缩包", L"*.zip");
                if (!picked.empty()) {
                    ThemeImport::start(picked.front());
                }
            }
            x += w + 8.0f;
            std::string folderText = "打开主题文件夹";
            w = Page::buttonWidth(folderText, 0xf07c);
            if (Page::button("settings#themes#folder", folderText, x, y, w, false, blocked, 0xf07c)) {
                std::error_code ec;
                std::filesystem::create_directories(ThemeStore::folder(), ec);
                Platform::open(ThemeStore::folder());
            }
            x += w + 8.0f;
            std::string reloadText = "重新加载";
            w = Page::buttonWidth(reloadText, 0xf2f1);
            if (Page::button("settings#themes#reload", reloadText, x, y, w, false, blocked, 0xf2f1)) {
                ThemeStore::rescan();
                ThemePreview::forget();
                ThemeShaders::reset();
                ThemeAssets::reset();
                ThemeRuntime::reload();
            }
        }
    }

    void draw(float left, float width, bool covered) {
        sectionLeft = left;
        sectionWidth = width;
        ThemeStore::rescanIfStale();
        const std::vector<ThemeStore::Entry>& entries = ThemeStore::entries();
        int melody = melodyIndex(entries);
        int total = (int) entries.size() + (melody >= 0 ? 1 : 0);
        int columns = std::max(1, (int) ((width + CARD_GAP) / (CARD_MIN + CARD_GAP)));
        float cardW = std::floor((width - CARD_GAP * (columns - 1)) / columns);
        float cardH = cardHeight(cardW);
        int rows = (total + columns - 1) / columns;
        float gridH = rows * cardH + std::max(0, rows - 1) * CARD_GAP;
        int settings = settingRows();
        std::vector<std::string> problems = currentProblems();
        int problemRows = std::min((int) problems.size(), 12);
        float contentH = Page::FIELD_H + Page::GAP + gridH + Page::GAP + Page::CAPTION_H + DISPLAY_ROWS * Page::ROW_H;
        if (settings > 0) {
            contentH += Page::GAP + Page::CAPTION_H + settings * Page::ROW_H;
        }
        if (problemRows > 0) {
            contentH += Page::GAP + Page::CAPTION_H + problemRows * Page::ROW_H;
        }
        Page::Scroll body("settings#themes", Page::top(), MainWindow::height() - 14.0f, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y = body.top - body.offset;
        topButtons(left, y, blocked);
        y += Page::FIELD_H + Page::GAP;
        for (int i = 0; i < total; i++) {
            int column = i % columns;
            int row = i / columns;
            float cardX = left + column * (cardW + CARD_GAP);
            float cardY = y + row * (cardH + CARD_GAP);
            if (!Page::shown(cardY, cardH)) {
                continue;
            }
#if defined(_WIN32)
            if (i == melody) {
                melodyCard(cardX, cardY, cardW, blocked);
                continue;
            }
#endif
            ThemeStore::Entry entry = entries[(size_t) (melody >= 0 && i > melody ? i - 1 : i)];
            card(entry, cardX, cardY, cardW, blocked);
        }
        y += gridH + Page::GAP;
        if (settings > 0) {
            Page::caption("主题设置", y);
            y += Page::CAPTION_H;
            Page::card(y, settings);
            nlohmann::json values = ThemeRuntime::settingValues();
            for (const ThemeSetting& setting : ThemeRuntime::settings()) {
                settingRow(setting, values, y, blocked);
                y += Page::ROW_H;
            }
            y += Page::GAP;
        }
        Page::caption("显示", y);
        y += Page::CAPTION_H;
        Page::card(y, DISPLAY_ROWS);
        displayRows(y, blocked);
        y += DISPLAY_ROWS * Page::ROW_H;
        if (problemRows > 0) {
            y += Page::GAP;
            Page::caption("问题", y);
            y += Page::CAPTION_H;
            Page::card(y, problemRows);
            for (int i = 0; i < problemRows; i++) {
                float rowY = y + i * Page::ROW_H;
                UiWidgets::icon(Ui::glyph(0xf071), left + 22.0f, rowY + Page::ROW_H * 0.5f, UiTheme::warningInk(),
                                11.0f);
                std::string line =
                        UiFont::fitEnd(problems[(size_t) i], UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, width - 60.0f);
                UiRender::textY(line, left + 38.0f, rowY, Page::ROW_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
        }
        body.end();
    }
}

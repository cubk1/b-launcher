#include "app/shell/MainWindow.h"
#include "app/shell/Logo.h"
#include "app/shell/Drop.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/AccountsScreen.h"
#include "app/shell/Appearance.h"
#include "app/shell/Background.h"
#include "app/downloads/DownloadsScreen.h"
#include "app/launch/Game.h"
#include "app/launch/GameLog.h"
#include "app/home/Homepage.h"
#include "app/launch/LogWindow.h"
#include "app/system/Config.h"
#include "app/shell/Icons.h"
#include "app/instances/InstancePage.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/launch/LaunchButton.h"
#include "app/instances/NewInstanceScreen.h"
#include "app/shell/Page.h"
#include "app/settings/SettingsScreen.h"
#include "app/system/Platform.h"

#include "ui/gl/ShaderUtil.h"
#include "ui/menu/MenuItems.h"
#include "ui/overlay/UiOverlays.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/Notifications.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiInput.h"
#include "ui/panel/UiPageTransition.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"
#include "ui/widget/InputWidget.h"
#include "ui/widget/NavItemWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace MainWindow {

    bool closeRequested = false;
    bool minimizeRequested = false;
    bool maximizeRequested = false;

    static const float RAIL_W = 48.0f;
    static const float ITEM_X = 8.0f;
    static const float ITEM_W = SIDEBAR_WIDTH - 16.0f;
    static const float SCROLL_ROOM = 8.0f;
    static float sideW = SIDEBAR_WIDTH;
    static float itemW = ITEM_W;
    static float listW = ITEM_W;
    static float fullListW = ITEM_W;
    static float fold = 0.0f;
    static bool folded = false;
    static bool foldLoaded = false;
    static const float ITEM_H = 30.0f;
    static const float ITEM_STEP = 32.0f;
    static const float SEARCH_Y = 50.0f;
    static const float SEARCH_H = 28.0f;
    static const float LIST_TOP = SEARCH_Y + SEARCH_H + 8.0f;
    static const char* const SEARCH_ID = "sidebar#search";
    static const float ICON_CENTER = 16.0f;
    static const float LABEL_X = 31.0f;
    static const float FOOTER_H = 10.0f + 4 * ITEM_STEP + 10.0f;
    static const float GROUP_H = 26.0f;
    static const float GROUP_GAP = 6.0f;

    static const int PAGE_HOME = 0;
    static const int PAGE_NEW_INSTANCE = 1000;
    static const int PAGE_SETTINGS = 1001;
    static const int PAGE_ACCOUNTS = 1002;
    static const int PAGE_DOWNLOADS = 1003;
    static const int PAGE_EDIT_LOADERS = 1004;
    static const int INSTANCE_PAGES = 10000;

    static const float SHELL_RADIUS = 14.0f;
    static const float SIDEBAR_SHADE_DARK = 0.72f;
    static const float PANEL_SHADE_DARK = 0.60f;
    static const float SIDEBAR_SHADE_LIGHT = 0.80f;
    static const float PANEL_SHADE_LIGHT = 0.70f;

    static PageTransition pages(PAGE_HOME);
    static std::unordered_map<std::string, int> instancePages;
    static int nextInstancePage = INSTANCE_PAGES;
    static float listScroll = 0.0f;
    static float listSmooth = 0.0f;
    static std::set<std::string> collapsed;
    static bool collapsedLoaded = false;
    static bool scrollRestored = false;
    static float layoutWidth = MIN_WIDTH;
    static float layoutHeight = MIN_HEIGHT;
    static bool layoutMaximized = false;

    struct Rect {
        float x;
        float y;
        float w;
        float h;
    };

    static std::vector<Rect> titleControls;

    static bool windowButton(const char* id, int glyph, float x, float y, bool danger) {
        const float size = 26.0f;
        titleControls.push_back({x, y, size, size});
        bool hovered = NewUi::hit(x, y, size, size);
        float response = UiState::motion(id, hovered ? 1.0f : 0.0f, 18.0f);
        if (response > 0.001f) {
            UiRender::roundRect(x, y, size, size, 7.0f, UiTheme::overlay(Ui::round(16 * response)));
        }
        uint32_t tint =
                UiRender::mix(UiTheme::textSecondary(), danger ? UiTheme::danger() : UiTheme::textPrimary(), response);
        UiWidgets::icon(Ui::glyph(glyph), x + size * 0.5f, y + size * 0.5f, tint, danger ? 12.0f : 11.0f);
        bool clicked = NewUi::click(x, y, size, size);
        if (clicked && danger) {
            UiSound::request(UiSound::RELEASE);
        }
        return clicked;
    }

    static void maximizeButton(float x, float y) {
        const float size = 26.0f;
        titleControls.push_back({x, y, size, size});
        bool hovered = NewUi::hit(x, y, size, size);
        float response = UiState::motion("window#maximize", hovered ? 1.0f : 0.0f, 18.0f);
        if (response > 0.001f) {
            UiRender::roundRect(x, y, size, size, 7.0f, UiTheme::overlay(Ui::round(16 * response)));
        }
        uint32_t tint = UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), response);
        float stroke = std::max(1.0f, std::round(1.2f * UiRender::uiScale * UiRender::guiScale()));
        float cx = x + size * 0.5f;
        float cy = y + size * 0.5f;
        if (layoutMaximized) {
            UiRender::scissor(cx - 3.0f, cy - 6.0f, 9.0f, 3.25f);
            UiRender::outline(cx - 2.0f, cy - 5.0f, 7.0f, 7.0f, 1.5f, tint, 0xF, stroke);
            UiRender::unscissor();
            UiRender::scissor(cx + 2.75f, cy - 6.0f, 3.25f, 9.0f);
            UiRender::outline(cx - 2.0f, cy - 5.0f, 7.0f, 7.0f, 1.5f, tint, 0xF, stroke);
            UiRender::unscissor();
            UiRender::outline(cx - 5.0f, cy - 2.0f, 7.0f, 7.0f, 1.5f, tint, 0xF, stroke);
        } else {
            UiRender::outline(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f, 1.5f, tint, 0xF, stroke);
        }
        if (NewUi::click(x, y, size, size)) {
            maximizeRequested = true;
        }
    }

    void setSize(float w, float h, bool isMaximized) {
        layoutWidth = std::max(MIN_WIDTH, w);
        layoutHeight = std::max(MIN_HEIGHT, h);
        layoutMaximized = isMaximized;
    }

    float width() {
        return layoutWidth;
    }

    float height() {
        return layoutHeight;
    }

    float sidebarWidth() {
        return sideW;
    }

    bool maximized() {
        return layoutMaximized;
    }

    void save() {
        if (scrollRestored) {
            Config::data()["sidebarScroll"] = listScroll;
        }
    }

    static int pageOf(const std::string& id) {
        auto found = instancePages.find(id);
        if (found != instancePages.end()) {
            return found->second;
        }
        int page = nextInstancePage++;
        instancePages[id] = page;
        return page;
    }

    void showHome() {
        pages.go(PAGE_HOME);
    }

    void showAccounts() {
        pages.go(PAGE_ACCOUNTS);
    }

    void showNewInstance() {
        pages.go(PAGE_NEW_INSTANCE);
    }

    void showDownloads() {
        pages.go(PAGE_DOWNLOADS);
    }

    void showLoaderEdit() {
        pages.go(PAGE_EDIT_LOADERS);
    }

    void openVersionSettings(const std::string& id) {
        Instances::select(id);
        pages.go(pageOf(id));
    }

    std::string shownInstance() {
        if (pages.page < INSTANCE_PAGES) {
            return std::string();
        }
        for (const auto& entry : instancePages) {
            if (entry.second == pages.page) {
                return entry.first;
            }
        }
        return std::string();
    }

    void renamed(const std::string& from, const std::string& to) {
        auto found = instancePages.find(from);
        if (found == instancePages.end()) {
            return;
        }
        int page = found->second;
        instancePages.erase(found);
        instancePages[to] = page;
    }

    static float fading() {
        return std::max(0.0f, 1.0f - fold * 2.0f);
    }

    static float railing() {
        return std::clamp(fold * 2.0f - 1.0f, 0.0f, 1.0f);
    }

    static void setFolded(bool on) {
        folded = on;
        Config::data()["sidebarFolded"] = on;
        Config::save();
        if (on && UiInput::isFocused(SEARCH_ID)) {
            UiInput::blur();
        }
    }

    static void railTip(const std::string& text, float x, float y) {
        NewUi::tooltipText = text;
        NewUi::tooltipX = x;
        NewUi::tooltipY = y;
    }

    static bool navItem(const std::string& id, int glyph, const std::string& label, float x, float y, float w, int page,
                        bool blocked = false, uint32_t image = 0, const std::string& note = std::string()) {
        NavItemWidget item(id, Ui::glyph(glyph), label);
        item.selected(pages.page == page).note(note).folded(fold, ITEM_W).bounds(x, y, w, ITEM_H);
        if (image != 0) {
            item.image(image, 16.0f);
        }
        item.block(blocked);
        item.paint();
        if (item.pressed) {
            pages.go(page);
        }
        return item.pressed;
    }

    static void fadeGlyph(int glyph, float cx, float cy, uint32_t tint, float amount, float size);

    static int framesCounted = 0;
    static double fpsSince = -1.0;
    static int fpsShown = 0;

    static void countFrame() {
        double now = ImGui::GetTime();
        if (fpsSince < 0.0) {
            fpsSince = now;
            return;
        }
        framesCounted++;
        double elapsed = now - fpsSince;
        if (elapsed >= 0.5) {
            fpsShown = (int) std::lround(framesCounted / elapsed);
            framesCounted = 0;
            fpsSince = now;
        }
    }

    static std::string brandText() {
        std::string text = Appearance::brandTitle();
        const std::string mark = "{FPS}";
        for (size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at)) {
            std::string value = std::to_string(fpsShown);
            text.replace(at, mark.size(), value);
            at += value.size();
        }
        return text;
    }

    static void brand() {
        float logo = 20.0f;
        float logoY = (TITLE_HEIGHT - logo) * 0.5f;
        float cx = ITEM_X + ICON_CENTER;
        float cy = TITLE_HEIGHT * 0.5f;
        const float size = 28.0f;
        float x = cx - size * 0.5f;
        float y = cy - size * 0.5f;
        bool rail = fold > 0.5f;
        bool hovered = NewUi::hit(x, y, size, size);
        float wake = UiState::motion("sidebar#expand", hovered ? 1.0f : 0.0f, 18.0f);
        titleControls.push_back({x, y, size, size});
        if (wake > 0.001f) {
            UiRender::roundRect(x, y, size, size, 7.0f, UiTheme::overlay(Ui::round(16 * wake)));
        }
        if (hovered) {
            if (rail) {
                railTip("展开侧栏", x + size + 8.0f, y);
            } else {
                railTip("收起侧栏", x, y + size + 4.0f);
            }
        }
        if (NewUi::click(x, y, size, size)) {
            UiSound::request(UiSound::TICK);
            setFolded(!folded);
        }
        Logo::draw(cx - logo * 0.5f, logoY, logo, 1.0f - wake);
        fadeGlyph(folded ? 0xf101 : 0xf100, cx, cy, UiTheme::textPrimary(), wake, 11.0f);
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * fading();
        float room = SIDEBAR_WIDTH - ITEM_X - LABEL_X - (ITEM_X + ICON_CENTER + 20.0f);
        std::string title = UiFont::fitEnd(brandText(), UiFont::MEDIUM, 15.0f, room);
        UiRender::textY(title, ITEM_X + LABEL_X, logoY, logo, UiTheme::textPrimary(), UiFont::MEDIUM, 15.0f);
        UiRender::alphaMul = savedAlpha;
    }

    static void fadeGlyph(int glyph, float cx, float cy, uint32_t tint, float amount, float size) {
        if (amount < 0.01f) {
            return;
        }
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::alphaMul = savedAlpha * amount;
        UiRender::scaleFactor = savedScale * UiRender::lerp(0.4f, 1.0f, amount);
        UiRender::pivotX = cx * UiRender::uiScale;
        UiRender::pivotY = cy * UiRender::uiScale;
        UiWidgets::icon(Ui::glyph(glyph), cx, cy, tint, size);
        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;
    }

    static void themeSwitch() {
        if (fold > 0.5f) {
            return;
        }
        const float size = 28.0f;
        float cx = sideW - ITEM_X - ICON_CENTER;
        float cy = TITLE_HEIGHT * 0.5f;
        float x = cx - size * 0.5f;
        float y = cy - size * 0.5f;
        titleControls.push_back({x, y, size, size});
        bool light = UiTheme::isLight();
        float lit = UiState::motion("theme#hover", NewUi::hit(x, y, size, size) ? 1.0f : 0.0f, 18.0f);
        float sun = UiState::motionFrom("theme#sun", light ? 1.0f : 0.0f, 14.0f, light ? 1.0f : 0.0f);
        uint32_t tint = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
        fadeGlyph(0xf185, cx, cy, tint, sun * fading(), 13.0f);
        fadeGlyph(0xf186, cx, cy, tint, (1.0f - sun) * fading(), 13.0f);
        if (NewUi::click(x, y, size, size)) {
            UiSound::request(UiSound::TICK);
            Appearance::toggleLight();
        }
    }

    static bool isCollapsed(const std::string& source) {
        if (!collapsedLoaded) {
            collapsedLoaded = true;
            const nlohmann::json& doc = Config::data();
            if (auto it = doc.find("collapsedGroups"); it != doc.end() && it->is_array()) {
                for (const nlohmann::json& value : *it) {
                    if (value.is_string()) {
                        collapsed.insert(value.get<std::string>());
                    }
                }
            }
        }
        return collapsed.count(source) != 0;
    }

    static void toggleGroup(const std::string& source) {
        if (isCollapsed(source)) {
            collapsed.erase(source);
        } else {
            collapsed.insert(source);
        }
        Config::data()["collapsedGroups"] = std::vector<std::string>(collapsed.begin(), collapsed.end());
        Config::save();
    }

    static std::string searchQuery() {
        std::string query = Ui::trim(UiInput::get(SEARCH_ID));
        for (char& c : query) {
            if (c >= 'A' && c <= 'Z') {
                c = (char) (c - 'A' + 'a');
            }
        }
        return query;
    }

    static bool matches(const std::string& id, const std::string& query) {
        if (query.empty()) {
            return true;
        }
        std::string lower = id;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') {
                c = (char) (c - 'A' + 'a');
            }
        }
        return lower.find(query) != std::string::npos;
    }

    static void focusSearch() {
        UiInput::focus(SEARCH_ID, 64);
        UiInput::selectAll(SEARCH_ID);
    }

    static void searchBox() {
        float x = ITEM_X;
        float y = SEARCH_Y;
        float w = itemW;
        float h = SEARCH_H;
        if (fold > 0.5f) {
            bool over = NewUi::hit(x, y, w, h);
            float lit = UiState::motion("search#ring", over ? 0.5f : 0.0f, 18.0f);
            UiRender::roundRect(x, y, w, h, 8.0f, UiTheme::overlay(10));
            UiRender::outline(x, y, w, h, 8.0f, UiRender::mix(UiTheme::outlineSoft(), UiTheme::outline(), lit));
            UiWidgets::icon(Ui::glyph(0xf002), x + w * 0.5f, y + h * 0.5f,
                            over ? UiTheme::textSecondary() : UiTheme::textFaint(), 11.0f);
            if (over) {
                railTip("搜索实例", x + w + 8.0f, y + 2.0f);
            }
            if (NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::TICK);
                setFolded(false);
                focusSearch();
            }
            return;
        }
        bool focused = UiInput::isFocused(SEARCH_ID);
        bool hasText = !UiInput::get(SEARCH_ID).empty();
        float clearX = x + w - ICON_CENTER;
        float clearRoom = hasText ? w - (clearX - x) + 10.0f : 0.0f;
        bool hovered = NewUi::hit(x, y, w, h);
        float ring = UiState::motion("search#ring", focused ? 1.0f : (hovered ? 0.5f : 0.0f), 18.0f);
        UiRender::roundRect(x, y, w, h, 8.0f, UiTheme::overlay(10));
        UiRender::outline(x, y, w, h, 8.0f,
                          focused ? UiTheme::accent()
                                  : UiRender::mix(UiTheme::outlineSoft(), UiTheme::outline(), ring));
        std::string shown = hasText ? UiInput::get(SEARCH_ID) : std::string("搜索实例");
        UiWidgets::iconBeside(Ui::glyph(0xf002), x + ICON_CENTER, shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, y, h,
                              focused ? UiTheme::textSecondary() : UiTheme::textFaint(), 11.0f);
        InputWidget input(SEARCH_ID);
        input.area(x + LABEL_X, w - LABEL_X - 8.0f - clearRoom, UiWidgets::TEXT_CONTROL, 14.0f)
                .limit(64)
                .hint("搜索实例")
                .bounds(x, y, w - clearRoom, h);
        input.paint();
        if (hasText) {
            bool over = NewUi::hit(clearX - 10.0f, y, 20.0f, h);
            UiWidgets::iconBeside(Ui::glyph(0xf00d), clearX, shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, y, h,
                                  over ? UiTheme::textPrimary() : UiTheme::textFaint(), 9.0f);
            if (NewUi::click(clearX - 10.0f, y, 20.0f, h)) {
                UiSound::request(UiSound::RELEASE);
                UiInput::set(SEARCH_ID, "");
            }
        }
    }

    static int groupRows(const std::vector<const Instances::Entry*>& rows) {
        return rows.empty() ? 1 : (int) rows.size();
    }

    static void groupHeader(const Instances::Source& source, size_t count, float y, float open, bool blocked) {
        float x = ITEM_X;
        bool rail = fold > 0.5f;
        bool hovered = !blocked && !rail && NewUi::hit(x, y, listW, GROUP_H);
        float lit = UiState::motion("group#hover#" + source.key, hovered ? 1.0f : 0.0f, 18.0f);
        uint32_t tint = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
        if (railing() > 0.001f) {
            float savedRail = UiRender::alphaMul;
            UiRender::alphaMul = savedRail * railing();
            UiRender::hLine(x + 8.0f, x + listW - 8.0f, y + GROUP_H * 0.5f, UiTheme::divider());
            UiRender::alphaMul = savedRail;
        }
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * fading();
        UiRender::textY(source.label, x + 8.0f, y, GROUP_H, tint, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        float chevronX = ITEM_X + std::max(listW, fullListW) - ICON_CENTER;
        float cy = y + GROUP_H * 0.5f;
        if (count > 0) {
            std::string text = std::to_string(count);
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(text, chevronX - 12.0f - w, y, GROUP_H, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }
        fadeGlyph(0xf078, chevronX, cy, tint, open, 9.0f);
        fadeGlyph(0xf054, chevronX, cy, tint, 1.0f - open, 9.0f);
        UiRender::alphaMul = savedAlpha;
        if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
            NewUi::rightClick = false;
            std::string key = source.key;
            NewUi::openMenu(
                    "sidebar#group#" + key,
                    [key] {
                        MenuItems items;
                        const Instances::Source* found = Instances::source(key);
                        if (found == nullptr) {
                            return items;
                        }
                        std::filesystem::path root = found->root;
                        items.push_back(ActionMenuItem::make("打开文件夹", 0xf07c, [root] {
                                            Platform::open(root);
                                        })->enabled(found->exists));
                        items.push_back(ActionMenuItem::make("重新读取", 0xf2f1, [] { Instances::refresh(); }));
                        if (found->custom) {
                            items.push_back(
                                    ActionMenuItem::make("移除", 0xf2ed, [key] { Instances::removeFolder(key); })
                                            ->dangerous()
                                            ->confirmFirst());
                        }
                        return items;
                    },
                    NewUi::mouseX, NewUi::mouseY, 160.0f);
        }
        if (!blocked && !rail && NewUi::click(x, y, listW, GROUP_H)) {
            UiSound::request(UiSound::TICK);
            toggleGroup(source.key);
        }
    }

    static void addFolderRow(float y, bool blocked) {
        NavItemWidget item("nav#addFolder", Ui::glyph(0xf65e), "添加游戏目录");
        item.folded(fold, fullListW).bounds(ITEM_X, y, listW, ITEM_H).block(blocked);
        item.paint();
        if (!item.pressed) {
            return;
        }
        if (std::optional<std::filesystem::path> picked = Platform::pickFolder(Platform::exeDirectory())) {
            std::string problem = Instances::addFolder(*picked);
            if (!problem.empty()) {
                Instances::notify(problem, true);
            }
        }
    }

    static void instanceRow(const Instances::Entry& entry, float y, bool blocked) {
        const std::string& key = entry.key;
        if (!blocked && !NewUi::overlayCoversMouse() && NewUi::rightClick && NewUi::hit(ITEM_X, y, listW, ITEM_H)) {
            NewUi::rightClick = false;
            UiSound::request(UiSound::PAGE);
            openVersionSettings(key);
        }
        NavItemWidget item("nav#instance#" + key, Ui::glyph(0xf071), entry.info.id);
        bool current = Instances::selected() == key &&
                (pages.page == PAGE_HOME || pages.page == PAGE_EDIT_LOADERS || pages.page == pageOf(key));
        item.selected(current).folded(fold, fullListW).bounds(ITEM_X, y, listW, ITEM_H);
        uint32_t image = InstancePage::icon(entry);
        if (image != 0) {
            item.image(image, 16.0f);
        }
        item.block(blocked);
        item.paint();
        if (item.pressed) {
            Instances::select(key);
            pages.go(PAGE_HOME);
        }
    }

    static void placeholderRow(const Instances::Source& source, float y, bool blocked, bool searching) {
        float savedAlpha = UiRender::alphaMul;
        if (searching && !Instances::loading()) {
            UiRender::alphaMul = savedAlpha * fading();
            UiRender::textY("没有匹配的实例", ITEM_X + LABEL_X, y, ITEM_H, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
            UiRender::alphaMul = savedAlpha;
            return;
        }
        if (!source.exists && source.key == "local" && !Instances::loading()) {
            NavItemWidget create("nav#create#" + source.key, Ui::glyph(0xf65e), "新建 .minecraft");
            create.folded(fold, fullListW).bounds(ITEM_X, y, listW, ITEM_H).block(blocked);
            create.paint();
            if (create.pressed) {
                Instances::createFolder(source.key);
            }
            return;
        }
        std::string text = Instances::loading() ? "正在读取…" : (source.exists ? "没有实例" : "没有找到 .minecraft");
        if (Instances::loading()) {
            Page::spinner(ITEM_X + ICON_CENTER, y + ITEM_H * 0.5f, 6.0f, UiTheme::accent());
        }
        UiRender::alphaMul = savedAlpha * fading();
        UiRender::textY(text, ITEM_X + LABEL_X, y, ITEM_H, UiTheme::textFaint(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        UiRender::alphaMul = savedAlpha;
    }

    static void sidebar() {
        UiRender::scissor(0, 0, sideW, height());
        brand();
        themeSwitch();
        searchBox();

        std::string query = searchQuery();
        bool searching = !query.empty();
        const std::vector<Instances::Source>& sources = Instances::sources();
        std::vector<std::vector<const Instances::Entry*>> grouped(sources.size());
        for (const Instances::Entry& entry : Instances::list()) {
            if (!matches(entry.info.id, query)) {
                continue;
            }
            for (size_t i = 0; i < sources.size(); i++) {
                if (sources[i].key == entry.source) {
                    grouped[i].push_back(&entry);
                }
            }
        }
        std::vector<float> opens(sources.size());
        float contentH = 0.0f;
        for (size_t i = 0; i < sources.size(); i++) {
            opens[i] = UiState::motion("group#open#" + sources[i].key,
                                       isCollapsed(sources[i].key) && !searching ? 0.0f : 1.0f, 16.0f);
            contentH += GROUP_H + groupRows(grouped[i]) * ITEM_STEP * opens[i] + GROUP_GAP;
        }
        contentH += ITEM_STEP;

        float listBottom = height() - FOOTER_H - 10.0f;
        float viewH = listBottom - LIST_TOP;
        float maxScroll = std::max(0.0f, contentH - viewH);
        float room = UiState::motion("sidebar#scrollRoom", maxScroll > 0.5f ? 1.0f : 0.0f, 14.0f);
        fullListW = UiRender::snapLayout(ITEM_W - SCROLL_ROOM * room);
        listW = UiRender::snapLayout(itemW - SCROLL_ROOM * room * (1.0f - fold));
        if (!scrollRestored && !Instances::loading()) {
            scrollRestored = true;
            const nlohmann::json& doc = Config::data();
            if (auto it = doc.find("sidebarScroll"); it != doc.end() && it->is_number()) {
                listScroll = UiRender::clamp(it->get<float>(), 0, maxScroll);
                listSmooth = listScroll;
            }
        }
        if (NewUi::hit(0, LIST_TOP, sideW, viewH) && ImGui::GetIO().MouseWheel != 0.0f) {
            listScroll -= ImGui::GetIO().MouseWheel * ITEM_STEP;
        }
        listScroll = UiRender::clamp(listScroll, 0, maxScroll);
        listSmooth = UiRender::clamp(UiRender::approach(listSmooth, listScroll, 18.0f), 0, maxScroll);

        float cursor = LIST_TOP - UiRender::snapLayout(listSmooth);
        for (size_t i = 0; i < sources.size(); i++) {
            const Instances::Source& source = sources[i];
            UiRender::scissor(0, LIST_TOP, sideW, viewH);
            bool headerBlocked = !NewUi::rawHit(0, std::max(LIST_TOP, cursor), sideW,
                                                std::min(listBottom, cursor + GROUP_H) - std::max(LIST_TOP, cursor));
            groupHeader(source, grouped[i].size(), cursor, opens[i], headerBlocked);
            UiRender::unscissor();
            cursor += GROUP_H;

            int rows = groupRows(grouped[i]);
            float shownH = rows * ITEM_STEP * opens[i];
            float clipTop = std::max(LIST_TOP, cursor);
            float clipBottom = std::min(listBottom, cursor + shownH);
            if (clipBottom > clipTop + 0.5f) {
                UiRender::scissor(0, clipTop, sideW, clipBottom - clipTop);
                bool blocked = !NewUi::rawHit(0, clipTop, sideW, clipBottom - clipTop);
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * opens[i];
                float rowY = cursor;
                if (grouped[i].empty()) {
                    placeholderRow(source, rowY, blocked, searching);
                }
                for (const Instances::Entry* entry : grouped[i]) {
                    if (rowY + ITEM_H >= clipTop && rowY <= clipBottom) {
                        instanceRow(*entry, rowY, blocked);
                    }
                    rowY += ITEM_STEP;
                }
                UiRender::alphaMul = savedAlpha;
                UiRender::unscissor();
            }
            cursor += shownH + GROUP_GAP;
        }
        if (cursor + ITEM_H >= LIST_TOP && cursor <= listBottom) {
            UiRender::scissor(0, LIST_TOP, sideW, viewH);
            addFolderRow(cursor, !NewUi::rawHit(0, LIST_TOP, sideW, viewH));
            UiRender::unscissor();
        }

        if (maxScroll > 0.5f && fold < 0.5f) {
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * fading();
            float dragged = NewUi::listScrollbar("sidebar#scrollbar", sideW - 7.0f, LIST_TOP + 2.0f, viewH - 4.0f,
                                                 viewH, maxScroll, listSmooth, listScroll);
            UiRender::alphaMul = savedAlpha;
            if (dragged != listScroll) {
                listScroll = dragged;
                listSmooth = dragged;
            }
        }

        float footerY = height() - FOOTER_H;
        UiRender::hLine(12.0f, sideW - 12.0f, footerY, UiTheme::divider());
        float rowY = footerY + 10.0f;
        if (navItem("nav#new", 0xf067, "新建实例", ITEM_X, rowY, itemW, PAGE_NEW_INSTANCE) && !Installer::running()) {
            NewInstanceScreen::leaveModpack();
        }
        const b_launcher_base::auth::account* account = Accounts::current();
        navItem("nav#accounts", 0xf007, "账户", ITEM_X, rowY + ITEM_STEP, itemW, PAGE_ACCOUNTS, false, 0,
                account != nullptr ? account->name : std::string());
        if (navItem("nav#downloads", 0xf019, "下载附加件", ITEM_X, rowY + 2 * ITEM_STEP, itemW, PAGE_DOWNLOADS)) {
            DownloadsScreen::openFor(Instances::selected());
        }
        navItem("nav#settings", 0xf013, "设置", ITEM_X, rowY + 3 * ITEM_STEP, itemW, PAGE_SETTINGS);
        UiRender::unscissor();
    }

    static void followList() {
        if (Instances::loading() || !Instances::activity().empty()) {
            return;
        }
        const std::vector<Instances::Entry>& instances = Instances::list();
        if (Instances::find(Instances::selected()) == nullptr) {
            Instances::select(instances.empty() ? std::string() : instances.front().key);
        }
        if (pages.page >= INSTANCE_PAGES) {
            std::string shown = shownInstance();
            if (shown.empty() || Instances::find(shown) == nullptr) {
                pages.go(PAGE_HOME);
            }
        }
    }

    static const float LAUNCH_MARGIN = 28.0f;

    static void content() {
        pages.begin();
        if (pages.page == PAGE_HOME) {
            Homepage::draw(Page::left(), TITLE_HEIGHT, Page::right(), height() - 14.0f, LaunchButton::SIZE + 22.0f,
                           NewUi::overlayCoversMouse());
            float buttonX = width() - Page::RIGHT_MARGIN - LaunchButton::SIZE;
            LaunchButton::drawStatus(buttonX - 14.0f, height() - LAUNCH_MARGIN);
            LaunchButton::draw(buttonX, height() - LAUNCH_MARGIN - LaunchButton::SIZE);
        } else if (pages.page >= INSTANCE_PAGES) {
            InstancePage::draw(shownInstance());
        } else if (pages.page == PAGE_SETTINGS) {
            SettingsScreen::draw();
        } else if (pages.page == PAGE_NEW_INSTANCE || pages.page == PAGE_EDIT_LOADERS) {
            NewInstanceScreen::draw();
        } else if (pages.page == PAGE_ACCOUNTS) {
            AccountsScreen::draw();
        } else if (pages.page == PAGE_DOWNLOADS) {
            DownloadsScreen::draw();
        }
        pages.end();
    }

    static void searchShortcut() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl || io.KeyAlt || io.KeyShift || !ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            return;
        }
        if (NewUi::popupOpen || !UiOverlays::isEmpty()) {
            return;
        }
        if (folded) {
            setFolded(false);
        }
        focusSearch();
    }

    static void coverSpan(float w, float h, float textureW, float textureH, float& uSpan, float& vSpan) {
        float scale = std::max(w / textureW, h / textureH);
        uSpan = w / (textureW * scale);
        vSpan = h / (textureH * scale);
    }

    static void unfold() {
        if (!foldLoaded) {
            foldLoaded = true;
            folded = Config::data().value("sidebarFolded", false);
        }
        fold = UiState::motionFrom("sidebar#fold", folded ? 1.0f : 0.0f, 12.0f, folded ? 1.0f : 0.0f);
        sideW = UiRender::snapLayout(UiRender::lerp(SIDEBAR_WIDTH, RAIL_W, fold));
        itemW = sideW - 2.0f * ITEM_X;
    }

    void draw() {
        UiRender::reset();
        countFrame();
        titleControls.clear();
        unfold();
        followList();
        searchShortcut();

        float r = SHELL_RADIUS;
        uint32_t sidebarFill = UiTheme::sidebar();
        uint32_t panelFill = UiTheme::panel();
        if (Appearance::backgroundImage() && Background::sidebar() != 0 && Background::panel() != 0) {
            bool light = UiTheme::isLight();
            float uSpan = 1.0f;
            float vSpan = 1.0f;
            coverSpan(SIDEBAR_WIDTH + r, height(), SIDEBAR_WIDTH + r, MIN_HEIGHT, uSpan, vSpan);
            UiRender::texture(Background::sidebar(), 0, 0, sideW + r, height(), 0.0f, (1.0f - vSpan) * 0.5f,
                              uSpan * (sideW + r) / (SIDEBAR_WIDTH + r), (1.0f + vSpan) * 0.5f);
            coverSpan(width() - sideW, height(), MIN_WIDTH - SIDEBAR_WIDTH, MIN_HEIGHT, uSpan, vSpan);
            ShaderUtil::roundTex(UiRender::transformX(sideW), UiRender::transformY(0),
                                 UiRender::transformW(width() - sideW), UiRender::transformW(height()),
                                 UiRender::transformW(r), Background::panel(), 0.0f, (1.0f - vSpan) * 0.5f, uSpan,
                                 (1.0f + vSpan) * 0.5f, 0xFFFFFFFFu, 1 | 4);
            float sidebarShade = light ? SIDEBAR_SHADE_LIGHT : SIDEBAR_SHADE_DARK;
            float panelShade = light ? PANEL_SHADE_LIGHT : PANEL_SHADE_DARK;
            sidebarFill = (sidebarFill & 0x00FFFFFFu) | ((uint32_t) Ui::round(255 * sidebarShade) << 24);
            panelFill = (panelFill & 0x00FFFFFFu) | ((uint32_t) Ui::round(255 * panelShade) << 24);
        }
        UiRender::shell(0, 0, width(), height(), sideW, r, sidebarFill, panelFill, 0.0f);
        UiRender::outlineFade(sideW, 0, width() - sideW, height(), r, UiTheme::outline(), 1.0f, 0.5f);
        sidebar();
        content();

        float buttonY = (TITLE_HEIGHT - 26.0f) * 0.5f;
        if (windowButton("window#minimize", 0xf068, width() - 14 - 86, buttonY, false)) {
            minimizeRequested = true;
        }
        maximizeButton(width() - 14 - 56, buttonY);
        if (windowButton("window#close", 0xf00d, width() - 14 - 26, buttonY, true)) {
            closeRequested = true;
        }
        Drop::draw(width(), height());
        Notifications::render(width(), height(), pages.page == PAGE_HOME ? LAUNCH_MARGIN + LaunchButton::SIZE : 0.0f);
    }

    bool isDragArea(float x, float y) {
        if (NewUi::popupOpen || !UiOverlays::isEmpty()) {
            return false;
        }
        if (y < 0 || y >= TITLE_HEIGHT) {
            return false;
        }
        for (const Rect& rect : titleControls) {
            if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
                return false;
            }
        }
        return true;
    }
}

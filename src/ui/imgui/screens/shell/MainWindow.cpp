#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/Drop.h"
#include "app/accounts/Accounts.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "ui/imgui/screens/shell/Compact.h"
#include "ui/imgui/screens/downloads/DownloadsScreen.h"
#include "ui/imgui/screens/downloads/TransfersScreen.h"
#include "app/launch/Game.h"
#include "app/launch/GameLog.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "ui/imgui/screens/home/HomeLaunchPanel.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/launch/CrashDialog.h"
#include "ui/imgui/screens/launch/LogWindow.h"
#include "ui/imgui/screens/multiplayer/MultiplayerScreen.h"
#include "app/multiplayer/Multiplayer.h"
#include "ui/imgui/screens/onboarding/Onboarding.h"
#include "app/system/Config.h"
#include "app/system/Interface.h"
#include "app/system/Notices.h"
#include "app/system/Customization.h"
#include "app/system/Features.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/instances/InstancePage.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/launch/LaunchButton.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/ShellLayout.h"
#include "ui/imgui/screens/theme/ThemePaint.h"
#include "ui/imgui/screens/settings/SettingsScreen.h"
#include "ui/imgui/screens/versions/VersionsScreen.h"
#include "app/system/Platform.h"
#include "app/system/Transfers.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/overlay/UiOverlays.h"
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
#include "ui/imgui/theme/ThemeStyle.h"
#include "ui/imgui/widget/InputWidget.h"
#include "ui/imgui/widget/NavItemWidget.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/value/Bind.h"

#include "imgui.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace MainWindow {

    bool closeRequested = false;
    bool minimizeRequested = false;
    float cornerRadius = 0.0f;
    bool maximizeRequested = false;

    static const float RAIL_W = 48.0f;
    static const float ITEM_X = 8.0f;
    static const float SCROLL_ROOM = 8.0f;
    static float sideW = SIDEBAR_WIDTH;
    static float openW = SIDEBAR_WIDTH;
    static float openItemW = SIDEBAR_WIDTH - 16.0f;
    static float itemW = openItemW;
    static float listW = openItemW;
    static float fullListW = openItemW;
    static ShellLayout::Spec spec;
    static bool topbarShown = false;
    static bool launchColumn = false;
    static float titleH = TITLE_HEIGHT;
    static float fold = 0.0f;
    static bool previewing = false;
    static bool folded = false;
    static int searchClaimed = -10;
    static bool foldLoaded = false;
    static const float ITEM_H = 30.0f;
    static const float ITEM_STEP = 32.0f;
    static const float SEARCH_H = 28.0f;
    static const float SEARCH_GAP = 6.0f;
    static float searchY = 50.0f;
    static float listTop = searchY + SEARCH_H + 8.0f;
    static const char* const SEARCH_ID = "sidebar#search";
    static const float ICON_CENTER = 16.0f;
    static const float LABEL_X = 31.0f;
    static const float GROUP_H = 26.0f;
    static const float GROUP_GAP = 6.0f;

    static const int PAGE_HOME = 0;
    static const int PAGE_NEW_INSTANCE = 1000;
    static const int PAGE_SETTINGS = 1001;
    static const int PAGE_ACCOUNTS = 1002;
    static const int PAGE_DOWNLOADS = 1003;
    static const int PAGE_EDIT_LOADERS = 1004;
    static const int PAGE_MULTIPLAYER = 1005;
    static const int PAGE_VERSIONS = 1006;
    static const int PAGE_TRANSFERS = 1007;
    static const int INSTANCE_PAGES = 10000;

    static const float SHELL_RADIUS = 14.0f;

    static PageTransition pages(PAGE_HOME);
    static std::unordered_map<std::string, int> instancePages;
    static int nextInstancePage = INSTANCE_PAGES;
    static float listScroll = 0.0f;
    static float listSmooth = 0.0f;
    static std::set<std::string> collapsed;
    static bool collapsedLoaded = false;
    static bool scrollRestored = false;
    static const double HOLD_TIME = 0.4;
    static const double HOLD_QUIET = 0.12;
    static const float HOLD_SLOP = 6.0f;
    static const float DRAG_EDGE = 28.0f;
    static std::string holdKey;
    static double holdAt = 0.0;
    static std::string pressKey;
    static long long pressAt = 0;
    static float holdX = 0.0f;
    static float holdY = 0.0f;
    static std::string dragKey;
    static float dragGrab = 0.0f;
    static std::string landKey;
    static std::string arrangeSource;
    static std::vector<std::string> arrangeKeys;
    static float layoutWidth = MIN_WIDTH;
    static float layoutHeight = MIN_HEIGHT;
    static bool layoutMaximized = false;

    static std::vector<Rect> titleControls;

    static bool windowButton(const char* id, int glyph, float x, float y, bool danger) {
        const float size = 26.0f;
        titleControls.push_back({x, y, size, size});
        bool hovered = NewUi::hit(x, y, size, size);
        float response = UiState::motion(id, hovered ? 1.0f : 0.0f, 18.0f);
        if (response > 0.001f) {
            UiRender::roundRect(x, y, size, size, 7.0f, UiTheme::overlay(Ui::round(16 * response)));
        }
        uint32_t idle = topbarShown ? UiTheme::tokenOr(UiTheme::TITLE_MUTED, UiTheme::textSecondary())
                                    : UiTheme::textSecondary();
        uint32_t lit =
                topbarShown ? UiTheme::tokenOr(UiTheme::TITLE_TEXT, UiTheme::textPrimary()) : UiTheme::textPrimary();
        uint32_t tint = UiRender::mix(idle, danger ? UiTheme::danger() : lit, response);
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
        uint32_t idle = topbarShown ? UiTheme::tokenOr(UiTheme::TITLE_MUTED, UiTheme::textSecondary())
                                    : UiTheme::textSecondary();
        uint32_t lit =
                topbarShown ? UiTheme::tokenOr(UiTheme::TITLE_TEXT, UiTheme::textPrimary()) : UiTheme::textPrimary();
        uint32_t tint = UiRender::mix(idle, lit, response);
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

    static const Notifications::Style& noticeStyle(Notices::Kind kind) {
        switch (kind) {
            case Notices::Kind::Done: return Notifications::DONE;
            case Notices::Kind::Failed: return Notifications::FAILED;
            default: return Notifications::INFO;
        }
    }

    static void summon() {
        if (Compact::active()) {
            Compact::reveal();
        } else {
            Platform::raiseWindow();
        }
    }

    void init() {
        Notices::show({
                [](Notices::Kind kind, const std::string& text, long long durationMs) {
                    Notifications::add(noticeStyle(kind), text, durationMs);
                },
                [](Notices::Kind kind, const std::string& text) {
                    return Notifications::live(noticeStyle(kind), text);
                },
                [](const std::string& id, Notices::Kind kind, const std::string& text, long long durationMs) {
                    Notifications::finish(id, noticeStyle(kind), text, durationMs);
                },
        });
        Interface::connect({
                [] { LogWindow::open(); },
                [](const std::string& login, const std::string& authServer) {
                    showAccounts();
                    if (login == "yggdrasil") {
                        AccountsScreen::openServerLogin(b_launcher_base::auth::account_type::yggdrasil, authServer,
                                                        std::string());
                    } else if (login == "nide8") {
                        AccountsScreen::openServerLogin(b_launcher_base::auth::account_type::nide8, authServer,
                                                        std::string());
                    }
                },
                [](const std::string&, bool) {},
                [](const std::string&) { showHome(); },
                [](std::shared_ptr<const Crashes::Report> report) {
                    summon();
                    CrashDialog::open(std::move(report));
                },
                summon,
        });
    }

    void setSize(float w, float h, bool isMaximized) {
        bool compact = Compact::active();
        spec = ShellLayout::current();
        layoutWidth = compact ? w : std::max(MIN_WIDTH, w);
        layoutHeight = compact ? h : std::max(MIN_HEIGHT, h);
        layoutMaximized = isMaximized;
    }

    float width() {
        return layoutWidth;
    }

    float height() {
        return layoutHeight;
    }

    float titleHeight() {
        return titleH;
    }

    float contentTop() {
        return topbarShown ? titleH + 12.0f : titleH;
    }

    Rect panel() {
        if (topbarShown) {
            return {sideW, titleH, layoutWidth - sideW, layoutHeight - titleH};
        }
        return {sideW, 0.0f, layoutWidth - sideW, layoutHeight};
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

    void showSettings() {
        Compact::request(false);
        if (pages.page != PAGE_SETTINGS) {
            SettingsScreen::reset();
        }
        pages.go(PAGE_SETTINGS);
    }

    void showAccounts() {
        Compact::request(false);
        if (pages.page != PAGE_ACCOUNTS) {
            AccountsScreen::reset();
        }
        pages.go(PAGE_ACCOUNTS);
    }

    void showNewInstance() {
        Compact::request(false);
        pages.go(PAGE_NEW_INSTANCE);
    }

    void startNewInstance() {
        if (pages.page != PAGE_NEW_INSTANCE) {
            NewInstanceScreen::reset();
        } else {
            NewInstanceScreen::leaveModpack();
        }
        showNewInstance();
    }

    void showDownloads() {
        Compact::request(false);
        pages.go(PAGE_DOWNLOADS);
    }

    void showTransfers() {
        Compact::request(false);
        pages.go(PAGE_TRANSFERS);
    }

    void showVersions() {
        Compact::request(false);
        VersionsScreen::open();
        pages.go(PAGE_VERSIONS);
    }

    void showLoaderEdit() {
        Compact::request(false);
        pages.go(PAGE_EDIT_LOADERS);
    }

    void openVersionSettings(const std::string& id) {
        Compact::request(false);
        Instances::select(id);
        if (pages.page != pageOf(id)) {
            InstancePage::reset();
        }
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
        item.selected(pages.page == page).note(note).folded(fold, openItemW).bounds(x, y, w, ITEM_H);
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
    static void shell();

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
        std::string title = Appearance::customBrandTitle().empty() && !spec.brandText.empty()
                ? spec.brandText
                : Appearance::brandTitle();
        return b_launcher_base::replace_all(title, "{FPS}", std::to_string(fpsShown));
    }

    static float windowButtonsWidth() {
        return spec.maximizeButton ? 14.0f + 86.0f : 14.0f + 56.0f;
    }

    static float brandTextX() {
        return spec.brandIcon ? ITEM_X + LABEL_X : ITEM_X + 10.0f;
    }

    static float foldToggle(float x, float y, float size, float radius) {
        bool rail = fold > 0.5f;
        bool hovered = NewUi::hit(x, y, size, size);
        float wake = UiState::motion("sidebar#expand", hovered ? 1.0f : 0.0f, 18.0f);
        if (wake > 0.001f) {
            UiRender::roundRect(x, y, size, size, radius, UiTheme::overlay(Ui::round(16 * wake)));
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
        return wake;
    }

    static float foldButtonX() {
        return UiRender::lerp(ITEM_X + itemW - SEARCH_H, (sideW - SEARCH_H) * 0.5f, fold);
    }

    static void foldButton() {
        float x = foldButtonX();
        float wake = foldToggle(x, searchY, SEARCH_H, 8.0f);
        UiWidgets::icon(Ui::glyph(folded ? 0xf101 : 0xf100), x + SEARCH_H * 0.5f, searchY + SEARCH_H * 0.5f,
                        UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), wake), 11.0f);
    }

    static void brand(float room) {
        float logo = 20.0f;
        float logoY = (titleH - logo) * 0.5f;
        float cx = ITEM_X + ICON_CENTER;
        float cy = titleH * 0.5f;
        float wake = 0.0f;
        if (!topbarShown) {
            const float size = 28.0f;
            float x = cx - size * 0.5f;
            float y = cy - size * 0.5f;
            titleControls.push_back({x, y, size, size});
            wake = foldToggle(x, y, size, 7.0f);
        }
        if (spec.brandIcon) {
            Logo::brand(cx - logo * 0.5f, logoY, logo, 1.0f - wake);
        }
        fadeGlyph(folded ? 0xf101 : 0xf100, cx, cy, UiTheme::textPrimary(), wake, 11.0f);
        if (room <= 0.0f) {
            return;
        }
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * (topbarShown ? 1.0f : fading());
        std::string title = UiFont::fitEnd(brandText(), UiFont::MEDIUM, spec.brandSize, room);
        UiRender::textY(title, brandTextX(), logoY, logo,
                        topbarShown ? UiTheme::tokenOr(UiTheme::TITLE_TEXT, UiTheme::textPrimary())
                                    : UiTheme::textPrimary(),
                        UiFont::MEDIUM, spec.brandSize);
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

    bool isCollapsed(const std::string& source) {
        if (!collapsedLoaded) {
            collapsedLoaded = true;
            for (const std::string& group : b_launcher_base::json_strings(Config::data(), "collapsedGroups")) {
                collapsed.insert(group);
            }
        }
        return collapsed.count(source) != 0;
    }

    void toggleGroup(const std::string& source) {
        if (isCollapsed(source)) {
            collapsed.erase(source);
        } else {
            collapsed.insert(source);
        }
        Config::data()["collapsedGroups"] = std::vector<std::string>(collapsed.begin(), collapsed.end());
        Config::save();
    }

    static std::string searchQuery() {
        return Ui::trim(UiInput::get(SEARCH_ID));
    }

    static void focusSearch() {
        UiInput::focus(SEARCH_ID, 64);
        UiInput::selectAll(SEARCH_ID);
    }

    static void searchBox() {
        if (Customization::singleInstance()) {
            return;
        }
        bool framed = Appearance::systemFrameRunning() || topbarShown;
        float x = ITEM_X;
        float y = searchY;
        float w = framed ? foldButtonX() - SEARCH_GAP - ITEM_X : itemW;
        float h = SEARCH_H;
        if (fold > 0.5f) {
            if (framed) {
                return;
            }
            bool over = NewUi::hit(x, y, w, h);
            float lit = UiState::motion("search#ring", over ? 0.5f : 0.0f, 18.0f);
            UiRender::roundRect(x, y, w, h, 8.0f, UiTheme::overlay(10));
            UiRender::outline(
                    x, y, w, h, 8.0f,
                    UiRender::mix(UiTheme::border(UiTheme::outlineSoft()), UiTheme::border(UiTheme::outline()), lit));
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
        float savedAlpha = UiRender::alphaMul;
        if (framed) {
            UiRender::alphaMul = savedAlpha * fading();
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
                                  : UiRender::mix(UiTheme::border(UiTheme::outlineSoft()),
                                                  UiTheme::border(UiTheme::outline()), ring));
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
        UiRender::alphaMul = savedAlpha;
    }

    static int groupRows(const std::vector<const Instances::Entry*>& rows) {
        return rows.empty() ? 1 : (int) rows.size();
    }

    static void addFolder() {
        if (std::optional<std::filesystem::path> picked = Platform::pickFolder(Platform::userDirectory())) {
            std::string problem = Instances::addFolder(*picked);
            if (!problem.empty()) {
                Instances::notify(problem, true);
            }
        }
    }

    static void folderItems(MenuItems& items) {
        items.push_back(ActionMenuItem::make("添加游戏目录", 0xf65e, [] { addFolder(); }));
        MenuItems hidden;
        for (const Instances::Source& source : Instances::sources()) {
            if (!source.hidden) {
                continue;
            }
            std::string key = source.key;
            hidden.push_back(ActionMenuItem::make(source.label, 0xf06e, [key] { Instances::setHidden(key, false); }));
        }
        if (!hidden.empty()) {
            items.push_back(ActionMenuItem::make("已隐藏的目录", 0xf070)->nest(hidden));
        }
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
                        items.push_back(ActionMenuItem::make("上移", 0xf062, [key] { Instances::moveSource(key, -1); }));
                        items.push_back(ActionMenuItem::make("下移", 0xf063, [key] { Instances::moveSource(key, 1); }));
                        items.push_back(ActionMenuItem::make("在侧栏隐藏", 0xf06e,
                                                             [key] { Instances::setHidden(key, true); }));
                        folderItems(items);
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

    static float playingWidth(const std::string& key) {
        float shown = UiState::getOrDefault("nav#playing#" + key, 0.0f);
        if (shown < 0.01f) {
            return 0.0f;
        }
        size_t count = 0;
        for (const Game::Session& session : Game::sessionsOf(key)) {
            count += session.state == Game::State::Running ? 1 : 0;
        }
        float width =
                count > 1 ? 17.0f + UiFont::width(std::to_string(count), UiFont::MEDIUM, UiWidgets::TEXT_SMALL) : 13.0f;
        return width * shown;
    }

    static void playing(const std::string& key, float y) {
        Game::State state = Game::state(key);
        float shown = UiState::motion("nav#playing#" + key, state != Game::State::Idle ? 1.0f : 0.0f, 14.0f);
        if (shown < 0.01f) {
            return;
        }
        size_t count = 0;
        for (const Game::Session& session : Game::sessionsOf(key)) {
            count += session.state == Game::State::Running ? 1 : 0;
        }
        float cx = UiRender::lerp(ITEM_X + listW - 13.0f, ITEM_X + listW - 7.0f, fold);
        float cy = UiRender::lerp(y + ITEM_H * 0.5f, y + 7.0f, fold);
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * shown;
        if (state == Game::State::Preparing) {
            Page::spinner(cx, cy, 4.5f, UiTheme::accent());
        } else {
            UiRender::circle(cx, cy, 3.5f, UiTheme::success());
        }
        if (count > 1 && fold < 0.5f) {
            std::string text = std::to_string(count);
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(text, cx - 9.0f - textW, y, ITEM_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }
        UiRender::alphaMul = savedAlpha;
    }

    static std::vector<std::string> keysOf(const std::string& source) {
        std::vector<std::string> keys;
        for (const Instances::Entry& entry : Instances::list()) {
            if (entry.source == source) {
                keys.push_back(entry.key);
            }
        }
        return keys;
    }

    // where: 0 to the top of its folder, -1 one up, 1 one down. Order beyond a folder would mean moving files.
    static void moveVersion(const std::string& source, const std::string& key, int where) {
        std::vector<std::string> keys = keysOf(source);
        auto at = std::find(keys.begin(), keys.end(), key);
        if (at == keys.end()) {
            return;
        }
        size_t from = (size_t) (at - keys.begin());
        size_t to = where == 0 ? 0
                              : where < 0 ? (from == 0 ? 0 : from - 1)
                                          : std::min(keys.size() - 1, from + 1);
        if (from == to) {
            return;
        }
        keys.erase(keys.begin() + (long long) from);
        keys.insert(keys.begin() + (long long) to, key);
        Instances::arrange(source, keys);
    }

    static void instanceRow(const Instances::Entry& entry, float y, bool blocked) {
        const std::string& key = entry.key;
        if (!blocked && !NewUi::overlayCoversMouse() && NewUi::rightClick && NewUi::hit(ITEM_X, y, listW, ITEM_H)) {
            NewUi::rightClick = false;
            UiSound::request(UiSound::PAGE);
            std::string source = entry.source;
            NewUi::openMenu(
                    "sidebar#instance#" + key,
                    [key, source] {
                        MenuItems items;
                        auto idle = [key] {
                            const Instances::Entry* found = Instances::find(key);
                            return found != nullptr && found->info.valid && Game::state(key) == Game::State::Idle;
                        };
                        items.push_back(ActionMenuItem::make("启动", 0xf04b, [key] {
                                            LaunchButton::start(key);
                                        })->enabledIf(idle));
                        items.push_back(ActionMenuItem::make("实例设置", 0xf013, [key] { openVersionSettings(key); }));
                        items.push_back(ActionMenuItem::make("打开游戏目录", 0xf07c, [key] {
                            if (const Instances::Entry* found = Instances::find(key)) {
                                Platform::open(found->gameDirectory);
                            }
                        }));
                        items.push_back(
                                ActionMenuItem::make("置顶", 0xf062, [source, key] { moveVersion(source, key, 0); }));
                        items.push_back(
                                ActionMenuItem::make("上移", 0xf077, [source, key] { moveVersion(source, key, -1); }));
                        items.push_back(
                                ActionMenuItem::make("下移", 0xf078, [source, key] { moveVersion(source, key, 1); }));
                        return items;
                    },
                    NewUi::mouseX, NewUi::mouseY, 160.0f);
        }
        NavItemWidget item("nav#instance#" + key, Ui::glyph(0xf071), entry.info.id);
        bool current = Instances::selected() == key &&
                (pages.page == PAGE_HOME || pages.page == PAGE_EDIT_LOADERS || pages.page == pageOf(key));
        item.selected(current).folded(fold, fullListW).reserve(playingWidth(key)).bounds(ITEM_X, y, listW, ITEM_H);
        uint32_t image = InstancePage::icon(entry);
        if (image != 0) {
            item.image(image, 16.0f);
        }
        item.block(blocked);
        item.paint();
        playing(key, y);
        if (item.pressStarted) {
            bool second = pressKey == key && Ui::millis() - pressAt < 400;
            holdKey = second ? std::string() : key;
            holdAt = ImGui::GetTime();
            holdX = NewUi::mouseX;
            holdY = NewUi::mouseY;
        }
        if (item.pressed) {
            long long now = Ui::millis();
            bool twice = pressKey == key && now - pressAt < 400;
            pressKey = twice ? std::string() : key;
            pressAt = now;
            Instances::select(key);
            pages.go(PAGE_HOME);
            if (twice) {
                LaunchButton::start(key);
            }
        }
    }

    static std::string shiftId(const std::string& key) {
        return "sidebar#shift#" + key;
    }

    static void raised(float y, float amount) {
        if (amount <= 0.001f) {
            return;
        }
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * amount;
        UiRender::shadow(ITEM_X, y, listW, ITEM_H, 8.0f, 6.0f, 2.0f, UiTheme::shadow());
        UiRender::roundRect(ITEM_X, y, listW, ITEM_H, 8.0f, UiTheme::surfaceRaised());
        UiRender::alphaMul = savedAlpha;
    }

    static float holding() {
        float t = (float) ((ImGui::GetTime() - holdAt - HOLD_QUIET) / (HOLD_TIME - HOLD_QUIET));
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static void carry(const std::string& source, const std::vector<const Instances::Entry*>& rows, int from, int to,
                      float top, float viewH) {
        int last = (int) rows.size() - 1;
        float y = UiRender::clamp(NewUi::mouseY - dragGrab, top - 6.0f, top + last * ITEM_STEP + 6.0f);
        UiState::put("sidebar#lift", 1.0f);
        UiRender::scissor(0, listTop, sideW, viewH);
        raised(y, 1.0f);
        instanceRow(*rows[from], y, true);
        UiRender::unscissor();
        bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (NewUi::mouseDown && !cancel) {
            return;
        }
        if (cancel) {
            to = from;
        }
        UiState::put(shiftId(rows[from]->key), y - (top + to * ITEM_STEP));
        for (int r = 0; r <= last; r++) {
            if (r == from) {
                continue;
            }
            int moved = from < to && r > from && r <= to ? -1 : (from > to && r >= to && r < from ? 1 : 0);
            std::string id = shiftId(rows[r]->key);
            UiState::put(id, UiState::getOrDefault(id, 0.0f) - moved * ITEM_STEP);
        }
        landKey = dragKey;
        dragKey.clear();
        UiSound::request(UiSound::RELEASE);
        if (from == to) {
            return;
        }
        std::vector<std::string> keys;
        for (const Instances::Entry* row : rows) {
            keys.push_back(row->key);
        }
        std::string moved = keys[from];
        keys.erase(keys.begin() + from);
        keys.insert(keys.begin() + to, moved);
        arrangeSource = source;
        arrangeKeys = std::move(keys);
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

    struct Destination {
        const char* id;
        int glyph;
        const char* label;
        int page;
    };

    static const Destination HOME_DESTINATION = {"nav#home", 0xf015, "主页", PAGE_HOME};
    static const Destination DESTINATIONS[] = {
            {"nav#new", 0xf067, "新建实例", PAGE_NEW_INSTANCE},
            {"nav#downloads", 0xf019, "下载附加件", PAGE_DOWNLOADS},
            {"nav#transfers", 0xf0ae, "下载列表", PAGE_TRANSFERS},
            {"nav#accounts", 0xf007, "账户", PAGE_ACCOUNTS},
            {"nav#multiplayer", 0xf6ff, "联机", PAGE_MULTIPLAYER},
            {"nav#settings", 0xf013, "选项", PAGE_SETTINGS},
    };

    static bool pageAllowed(int page) {
        switch (page) {
            case PAGE_NEW_INSTANCE:
                return Features::enabled(Features::NEW_INSTANCE) && !Customization::singleInstance();
            case PAGE_EDIT_LOADERS: return Features::enabled(Features::INSTANCE_EDIT);
            case PAGE_DOWNLOADS: return Features::enabled(Features::DOWNLOADS);
            case PAGE_TRANSFERS: return Features::enabled(Features::TRANSFERS);
            case PAGE_ACCOUNTS: return Features::enabled(Features::ACCOUNTS);
            case PAGE_MULTIPLAYER: return Features::enabled(Features::MULTIPLAYER);
            case PAGE_VERSIONS: return Features::enabled(Features::VERSIONS);
            default: return true;
        }
    }

    static std::vector<const Destination*> shownDestinations() {
        std::vector<const Destination*> out;
        for (const Destination& to : DESTINATIONS) {
            if (pageAllowed(to.page)) {
                out.push_back(&to);
            }
        }
        return out;
    }

    static float footerHeight() {
        return 10.0f + (float) shownDestinations().size() * ITEM_STEP + 10.0f;
    }

    static std::string destinationNote(int page);
    static void arrive(int page, int from);

    static void footer() {
        if (topbarShown) {
            return;
        }
        float footerY = height() - footerHeight();
        UiRender::hLine(12.0f, sideW - 12.0f, footerY, UiTheme::divider());
        float rowY = footerY + 10.0f;
        int from = pages.page;
        std::vector<const Destination*> shown = shownDestinations();
        for (int k = 0; k < (int) shown.size(); k++) {
            const Destination& to = *shown[(size_t) k];
            if (navItem(to.id, to.glyph, to.label, ITEM_X, rowY + k * ITEM_STEP, itemW, to.page, false, 0,
                        destinationNote(to.page))) {
                arrive(to.page, from);
            }
        }
    }

    static const float PILL_H = 30.0f;
    static const float PILL_PAD = 10.0f;
    static const float PILL_ICON = 18.0f;
    static const float PILL_GAP = 2.0f;
    static const int PILL_FULL = 0;
    static const int PILL_TEXT = 1;
    static const int PILL_GLYPH = 2;

    static std::string destinationNote(int page) {
        if (page == PAGE_TRANSFERS) {
            int transferring = Transfers::running();
            return transferring > 0 ? std::to_string(transferring) : std::string();
        }
        if (page == PAGE_ACCOUNTS) {
            const b_launcher_base::auth::account* account = Accounts::current();
            return account != nullptr ? account->name : std::string();
        }
        if (page == PAGE_MULTIPLAYER) {
            return Multiplayer::active() ? Multiplayer::state().code : std::string();
        }
        return std::string();
    }

    static void arrive(int page, int from) {
        if (page == PAGE_NEW_INSTANCE) {
            if (from != PAGE_NEW_INSTANCE) {
                NewInstanceScreen::reset();
            } else {
                NewInstanceScreen::leaveModpack();
            }
        } else if (page == PAGE_DOWNLOADS) {
            DownloadsScreen::openFor(Instances::selected());
        } else if (page == PAGE_ACCOUNTS && from != PAGE_ACCOUNTS) {
            AccountsScreen::reset();
        } else if (page == PAGE_SETTINGS && from != PAGE_SETTINGS) {
            SettingsScreen::reset();
        }
    }

    bool launchColumnShown() {
        return launchColumn;
    }

    static void sidebar() {
        if (launchColumn) {
            if (sideW > 1.0f) {
                UiRender::scissor(0, titleH, sideW, height() - titleH);
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * std::clamp(sideW / std::max(1.0f, openW), 0.0f, 1.0f);
                HomeLaunchPanel::column(sideW - openW, titleH, openW, height(), NewUi::overlayCoversMouse(),
                                        spec.launchAvatar, spec.launchAvatarRadius);
                UiRender::alphaMul = savedAlpha;
                UiRender::unscissor();
            }
            return;
        }
        UiRender::scissor(0, 0, sideW, height());
        if (Appearance::systemFrameRunning() || topbarShown) {
            searchBox();
            foldButton();
        } else {
            brand(openItemW - LABEL_X);
            searchBox();
        }

        if (Customization::singleInstance()) {
            footer();
            UiRender::unscissor();
            return;
        }
        std::string query = searchQuery();
        bool searching = !query.empty();
        const std::vector<Instances::Source>& sources = Instances::sources();
        std::vector<std::vector<const Instances::Entry*>> grouped(sources.size());
        for (const Instances::Entry& entry : Instances::list()) {
            if (!b_launcher_base::icontains(entry.info.id, query)) {
                continue;
            }
            for (size_t i = 0; i < sources.size(); i++) {
                if (sources[i].key == entry.source && !sources[i].hidden) {
                    grouped[i].push_back(&entry);
                }
            }
        }
        std::vector<float> opens(sources.size());
        float contentH = 0.0f;
        for (size_t i = 0; i < sources.size(); i++) {
            if (sources[i].hidden) {
                continue;
            }
            opens[i] = UiState::motion("group#open#" + sources[i].key,
                                       isCollapsed(sources[i].key) && !searching ? 0.0f : 1.0f, 16.0f);
            contentH += GROUP_H + groupRows(grouped[i]) * ITEM_STEP * opens[i] + GROUP_GAP;
        }

        float listBottom = height() - (topbarShown ? 0.0f : footerHeight()) - 10.0f;
        float viewH = listBottom - listTop;
        float maxScroll = std::max(0.0f, contentH - viewH);
        if (searching) {
            holdKey.clear();
            dragKey.clear();
        }
        if (!holdKey.empty() &&
            (!NewUi::mouseDown || std::abs(NewUi::mouseX - holdX) > HOLD_SLOP ||
             std::abs(NewUi::mouseY - holdY) > HOLD_SLOP)) {
            holdKey.clear();
        }
        bool lifting = !holdKey.empty() && dragKey.empty() && ImGui::GetTime() - holdAt >= HOLD_TIME;
        bool dragging = !dragKey.empty();
        float room = UiState::motion("sidebar#scrollRoom", maxScroll > 0.5f ? 1.0f : 0.0f, 14.0f);
        fullListW = UiRender::snapLayout(openItemW - SCROLL_ROOM * room);
        listW = UiRender::snapLayout(itemW - SCROLL_ROOM * room * (1.0f - fold));
        if (!scrollRestored && !Instances::loading()) {
            scrollRestored = true;
            const nlohmann::json& doc = Config::data();
            if (auto it = doc.find("sidebarScroll"); it != doc.end() && it->is_number()) {
                listScroll = UiRender::clamp(it->get<float>(), 0, maxScroll);
                listSmooth = listScroll;
            }
        }
        if (NewUi::hit(0, listTop, sideW, viewH) && NewUi::wheel() != 0.0f) {
            listScroll -= NewUi::wheel() * ITEM_STEP;
        }
        if (dragging) {
            float step = 10.0f * ImGui::GetIO().DeltaTime;
            if (NewUi::mouseY < listTop + DRAG_EDGE) {
                listScroll -= std::min(60.0f, listTop + DRAG_EDGE - NewUi::mouseY) * step;
            } else if (NewUi::mouseY > listBottom - DRAG_EDGE) {
                listScroll += std::min(60.0f, NewUi::mouseY - (listBottom - DRAG_EDGE)) * step;
            }
        }
        listScroll = UiRender::clamp(listScroll, 0, maxScroll);
        listSmooth = UiRender::clamp(UiRender::approach(listSmooth, listScroll, 18.0f), 0, maxScroll);

        float cursor = listTop - UiRender::snapLayout(listSmooth);
        bool carried = false;
        for (size_t i = 0; i < sources.size(); i++) {
            const Instances::Source& source = sources[i];
            if (source.hidden) {
                continue;
            }
            UiRender::scissor(0, listTop, sideW, viewH);
            bool headerBlocked = !NewUi::rawHit(0, std::max(listTop, cursor), sideW,
                                                std::min(listBottom, cursor + GROUP_H) - std::max(listTop, cursor));
            groupHeader(source, grouped[i].size(), cursor, opens[i], headerBlocked || dragging);
            UiRender::unscissor();
            cursor += GROUP_H;

            const std::vector<const Instances::Entry*>& entries = grouped[i];
            int rows = groupRows(entries);
            float shownH = rows * ITEM_STEP * opens[i];
            int from = -1;
            int to = -1;
            for (size_t r = 0; dragging && r < entries.size(); r++) {
                if (entries[r]->key == dragKey) {
                    from = (int) r;
                    to = std::clamp(Ui::round((NewUi::mouseY - dragGrab - cursor) / ITEM_STEP), 0,
                                    (int) entries.size() - 1);
                }
            }
            float clipTop = std::max(listTop, cursor);
            float clipBottom = std::min(listBottom, cursor + shownH);
            if (clipBottom > clipTop + 0.5f) {
                UiRender::scissor(0, clipTop, sideW, clipBottom - clipTop);
                bool blocked = !NewUi::rawHit(0, clipTop, sideW, clipBottom - clipTop);
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * opens[i];
                float rowY = cursor;
                if (entries.empty()) {
                    placeholderRow(source, rowY, blocked, searching);
                }
                for (size_t r = 0; r < entries.size(); r++) {
                    const Instances::Entry& entry = *entries[r];
                    int index = (int) r;
                    if (lifting && entry.key == holdKey) {
                        dragKey = holdKey;
                        dragGrab = NewUi::mouseY - rowY;
                        holdKey.clear();
                        lifting = false;
                        from = index;
                        to = index;
                        landKey.clear();
                        UiSound::request(UiSound::PRESS);
                    }
                    float target = 0.0f;
                    if (from >= 0 && from < to && index > from && index <= to) {
                        target = -ITEM_STEP;
                    } else if (from >= 0 && from > to && index >= to && index < from) {
                        target = ITEM_STEP;
                    }
                    float y = rowY + UiState::motion(shiftId(entry.key), target, 20.0f);
                    if (index != from && y + ITEM_H >= clipTop && y <= clipBottom) {
                        if (entry.key == holdKey) {
                            raised(y, holding());
                        } else if (entry.key == landKey) {
                            float settle = UiState::motion("sidebar#lift", 0.0f, 14.0f);
                            raised(y, settle);
                            if (settle <= 0.001f) {
                                landKey.clear();
                            }
                        }
                        instanceRow(entry, y, blocked || dragging || from >= 0);
                    }
                    rowY += ITEM_STEP;
                }
                UiRender::alphaMul = savedAlpha;
                UiRender::unscissor();
            }
            if (from >= 0) {
                carried = true;
                carry(source.key, entries, from, to, cursor, viewH);
            }
            cursor += shownH + GROUP_GAP;
        }
        if (dragging && !carried) {
            dragKey.clear();
        }
        if (!arrangeSource.empty()) {
            Instances::arrange(arrangeSource, arrangeKeys);
            arrangeSource.clear();
            arrangeKeys.clear();
        }
        if (!dragging && NewUi::rightClick && !NewUi::overlayCoversMouse() && NewUi::hit(0, listTop, sideW, viewH)) {
            NewUi::rightClick = false;
            UiSound::request(UiSound::PAGE);
            NewUi::openMenu(
                    "sidebar#list",
                    [] {
                        MenuItems items;
                        folderItems(items);
                        return items;
                    },
                    NewUi::mouseX, NewUi::mouseY, 160.0f);
        }

        if (maxScroll > 0.5f && fold < 0.5f) {
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * fading();
            float dragged = NewUi::listScrollbar("sidebar#scrollbar", sideW - 7.0f, listTop + 2.0f, viewH - 4.0f, viewH,
                                                 maxScroll, listSmooth, listScroll);
            UiRender::alphaMul = savedAlpha;
            if (dragged != listScroll) {
                listScroll = dragged;
                listSmooth = dragged;
            }
        }

        footer();
        UiRender::unscissor();
    }

    static float pillWidth(const char* label, int mode) {
        if (mode == PILL_GLYPH) {
            return PILL_H + 4.0f;
        }
        return PILL_PAD * 2.0f + (mode == PILL_FULL ? PILL_ICON : 0.0f) +
                UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
    }

    static void navPill(const Destination& to, float x, float y, float w, int mode, int from, bool blocked) {
        std::string label = to.label;
        std::string note = destinationNote(to.page);
        titleControls.push_back({x, y, w, PILL_H});
        bool hovered = !blocked && NewUi::hit(x, y, w, PILL_H);
        float lit = UiState::motion(std::string(to.id) + "#pill",
                                    pages.page == to.page ? 1.0f : (hovered ? 0.48f : 0.0f), 16.0f);
        ThemePaint::selectedFill(ThemePaintSpec::NAV_ACTIVE, std::string(to.id) + "#pill", x, y, w, PILL_H,
                                 PILL_H * 0.5f, pages.page == to.page, hovered, lit);
        uint32_t muted = UiTheme::tokenOr(UiTheme::TITLE_MUTED, UiTheme::textMuted());
        uint32_t glyphTint = UiRender::mix(muted, UiTheme::accent(), lit);
        if (mode == PILL_GLYPH) {
            UiWidgets::icon(Ui::glyph(to.glyph), x + w * 0.5f, y + PILL_H * 0.5f, glyphTint, UiWidgets::TEXT_ICON);
        } else {
            float textX = x + PILL_PAD;
            if (mode == PILL_FULL) {
                UiWidgets::iconBeside(Ui::glyph(to.glyph), textX + 7.0f, label, UiFont::MEDIUM, UiWidgets::TEXT_BODY, y,
                                      PILL_H, glyphTint, UiWidgets::TEXT_ICON);
                textX += PILL_ICON;
            }
            UiRender::textY(label, textX, y, PILL_H,
                            UiRender::mix(muted, UiTheme::tokenOr(UiTheme::TITLE_ACTIVE, UiTheme::textPrimary()), lit),
                            UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        }
        if (hovered && (mode == PILL_GLYPH || !note.empty())) {
            NewUi::tooltipText = note.empty() ? label : label + "\xEF\xBC\x88" + note + "\xEF\xBC\x89";
            NewUi::tooltipX = x;
            NewUi::tooltipY = y + PILL_H + 4.0f;
        }
        if (!blocked && NewUi::click(x, y, w, PILL_H)) {
            UiSound::request(UiSound::TICK);
            pages.go(to.page);
            arrive(to.page, from);
        }
    }

    static void topbar() {
        std::vector<const Destination*> row = {&HOME_DESTINATION};
        for (const Destination* to : shownDestinations()) {
            row.push_back(to);
        }
        auto span = [&row](int mode) {
            float sum = PILL_GAP * (float) (row.size() - 1);
            for (const Destination* to : row) {
                sum += pillWidth(to->label, mode);
            }
            return sum;
        };
        float right = width() - (Appearance::systemFrameRunning() ? 14.0f : windowButtonsWidth() + 10.0f);
        float brandRoom = openItemW - LABEL_X;
        float brandEnd =
                brandTextX() + std::min(brandRoom, UiFont::width(brandText(), UiFont::MEDIUM, spec.brandSize)) + 12.0f;
        float left = launchColumn ? brandEnd : std::max(brandEnd, openW + 8.0f);
        int mode = ThemeStyle::icons() ? PILL_FULL : PILL_TEXT;
        while (mode < PILL_GLYPH && span(mode) > right - left) {
            mode++;
        }
        float total = span(mode);
        if (total > right - left) {
            brandRoom = 0.0f;
            left = ITEM_X + ICON_CENTER + 18.0f;
        }
        float x = spec.navCentered ? std::clamp((width() - total) * 0.5f, left, std::max(left, right - total)) : left;
        brand(brandRoom);
        float y = UiRender::snapLayout((titleH - PILL_H) * 0.5f);
        int from = pages.page;
        bool blocked = NewUi::overlayCoversMouse();
        for (const Destination* to : row) {
            float w = pillWidth(to->label, mode);
            navPill(*to, x, y, w, mode, from, blocked);
            x += w + PILL_GAP;
        }
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
    static const float BANNER_H = 48.0f;
    static const float BANNER_GAP = 12.0f;

    static float setupBanner(float y) {
        bool visible = Onboarding::suggested() && !Onboarding::drawing();
        float shown = UiState::motion("home#setupBanner", visible ? 1.0f : 0.0f, 14.0f);
        if (shown <= 0.001f) {
            return 0.0f;
        }
        float eased = shown * shown * (3.0f - 2.0f * shown);
        float x = Page::left();
        float w = Page::width();
        float saved = UiRender::alphaMul;
        UiRender::alphaMul = saved * eased;
        UiRender::scissor(x - 2.0f, y - 2.0f, w + 4.0f, (BANNER_H + 4.0f) * eased);
        bool blocked = !visible || NewUi::overlayCoversMouse();
        UiRender::roundRect(x, y, w, BANNER_H, 12.0f, UiTheme::accentSoft(30));
        UiRender::outline(x, y, w, BANNER_H, 12.0f, (UiTheme::accent() & 0x00FFFFFFu) | 0x59000000u);
        UiWidgets::icon(Ui::glyph(0xf0d0), x + 22.0f, y + BANNER_H * 0.5f, UiTheme::accentText(), 12.0f);
        float closeX = x + w - 10.0f - 28.0f;
        if (Page::iconButton("home#setupBanner#close", 0xf00d, "不再显示", closeX, y + (BANNER_H - 28.0f) * 0.5f,
                             blocked)) {
            Onboarding::dismiss();
        }
        std::string action = "开始设置";
        float actionW = std::max(88.0f, Page::buttonWidth(action, 0xf061));
        float actionX = closeX - 8.0f - actionW;
        if (Page::button("home#setupBanner#start", action, actionX, y + (BANNER_H - Page::FIELD_H) * 0.5f, actionW,
                         true, blocked, 0xf061)) {
            Onboarding::start();
        }
        std::string text = UiFont::fitEnd("第一次使用？花一普朗克时间设置主题和主页", UiFont::MEDIUM,
                                          UiWidgets::TEXT_CONTROL, std::max(0.0f, actionX - 16.0f - (x + 40.0f)));
        UiRender::textY(text, x + 40.0f, y, BANNER_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        UiRender::unscissor();
        UiRender::alphaMul = saved;
        return (BANNER_H + BANNER_GAP) * eased;
    }

    static void homeExtras(bool floating) {
        bool blocked = NewUi::overlayCoversMouse();
        float y = height() - 14.0f - Page::FIELD_H - 8.0f;
        float x = Page::left();
        const std::vector<Customization::Link>& links = Customization::links();
        for (size_t i = 0; i < links.size(); i++) {
            const Customization::Link& link = links[i];
            std::string label = link.label.empty() ? link.url : link.label;
            float w = Page::buttonWidth(label, 0xf0c1);
            if (x + w > Page::right()) {
                break;
            }
            if (Page::button("home#link#" + std::to_string(i), label, x, y, w, false, blocked, 0xf0c1)) {
                Platform::openUrl(link.url);
            }
            x += w + 8.0f;
        }
        const std::vector<Customization::Server>& servers = Customization::servers();
        if (Customization::autoJoin() != "choose" || servers.empty()) {
            return;
        }
        const float w = 210.0f;
        float sx = floating ? width() - Page::RIGHT_MARGIN - w : Page::right() - w;
        float sy = floating ? height() - LAUNCH_MARGIN - LaunchButton::SIZE - 12.0f - Page::FIELD_H : y;
        auto choice = std::make_shared<ModeBinding>(
                "home#server",
                [] {
                    std::string chosen = b_launcher_base::json_text(Config::data(), "packServer");
                    for (const Customization::Server& server : Customization::servers()) {
                        if (server.address == chosen) {
                            return server.name.empty() ? server.address : server.name;
                        }
                    }
                    const Customization::Server& first = Customization::servers().front();
                    return first.name.empty() ? first.address : first.name;
                },
                [](const std::string& picked) {
                    for (const Customization::Server& server : Customization::servers()) {
                        if ((server.name.empty() ? server.address : server.name) == picked) {
                            Config::data()["packServer"] = server.address;
                            Config::save();
                        }
                    }
                },
                [] {
                    std::vector<std::string> names;
                    for (const Customization::Server& server : Customization::servers()) {
                        names.push_back(server.name.empty() ? server.address : server.name);
                    }
                    return names;
                });
        SelectWidget widget("home#server", choice);
        widget.bounds(sx, sy, w, Page::FIELD_H).block(blocked);
        widget.paint();
    }

    static void featureShortcut() {
        if (!ImGui::IsKeyPressed(ImGuiKey_F12, false) || NewUi::bindingKey != nullptr ||
            NewUi::recordingShortcut != nullptr || !Features::anyHidden()) {
            return;
        }
        Features::toggleReveal();
        Notices::add(Notices::Kind::Info, Features::revealed() ? "已临时显示隐藏的功能" : "已恢复隐藏的功能", 2500);
    }

    static void home() {
        bool floating = !Homepage::launchShown() && !launchColumn;
        float top = contentTop() + setupBanner(contentTop());
        Homepage::draw(Page::left(), top, Page::right(), height() - 14.0f, floating ? LaunchButton::SIZE + 22.0f : 0.0f,
                       NewUi::overlayCoversMouse());
        if (floating) {
            float buttonX = width() - Page::RIGHT_MARGIN - LaunchButton::SIZE;
            LaunchButton::drawStatus(buttonX - 14.0f, height() - LAUNCH_MARGIN);
            LaunchButton::draw(buttonX, height() - LAUNCH_MARGIN - LaunchButton::SIZE);
        }
        if (Customization::active()) {
            homeExtras(floating);
        }
    }

    static void content() {
        pages.begin();
        if (pages.page == PAGE_HOME) {
            home();
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
        } else if (pages.page == PAGE_TRANSFERS) {
            TransfersScreen::draw();
        } else if (pages.page == PAGE_MULTIPLAYER) {
            MultiplayerScreen::draw();
        } else if (pages.page == PAGE_VERSIONS) {
            VersionsScreen::draw();
        }
        pages.end();
    }

    void claimSearch() {
        searchClaimed = ImGui::GetFrameCount();
    }

    static void searchShortcut() {
        if (ImGui::GetFrameCount() - searchClaimed <= 1) {
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl || io.KeyAlt || io.KeyShift || !ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            return;
        }
        if (NewUi::popupOpen || !UiOverlays::isEmpty()) {
            return;
        }
        if (launchColumn) {
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
            folded = b_launcher_base::json_bool(Config::data(), "sidebarFolded");
        }
        launchColumn = spec.preset == ShellLayout::Preset::Launch;
        topbarShown = spec.preset == ShellLayout::Preset::Topbar || launchColumn;
        titleH = std::round(topbarShown ? spec.titleHeight : spec.sidebarTitle);
        openW = std::round(launchColumn ? spec.launchWidth : topbarShown ? spec.columnWidth : spec.sidebarWidth);
        openItemW = openW - 2.0f * ITEM_X;
        float target = folded ? 1.0f : 0.0f;
        fold = UiState::motionFrom("sidebar#fold", target, 12.0f, target);
        sideW = UiRender::snapLayout(UiRender::lerp(openW, RAIL_W, fold));
        if (launchColumn) {
            fold = 0.0f;
            float open = previewing || pages.page == PAGE_HOME ? 1.0f : 0.0f;
            sideW = UiRender::snapLayout(openW * UiState::motion("sidebar#launch", open, 12.0f));
        }
        itemW = sideW - 2.0f * ITEM_X;
        searchY = topbarShown ? titleH + 10.0f : Appearance::systemFrameRunning() ? 10.0f : titleH + 6.0f;
        listTop = searchY + SEARCH_H + 8.0f;
    }

    void draw() {
        UiRender::reset();
        countFrame();
        titleControls.clear();
        if (!Onboarding::covering()) {
            unfold();
            followList();
            searchShortcut();
            featureShortcut();
            if (!pageAllowed(pages.page)) {
                pages.go(PAGE_HOME);
            }
            Compact::shortcut();
            shell();
        }
        if (Onboarding::drawing()) {
            Onboarding::draw();
        }
        // Keep notifications visible and expiring while the guide covers the shell.
        bool underTitle = Onboarding::drawing() || (pages.page == PAGE_HOME && HomeWidgets::alignBottom());
        bool floating = pages.page == PAGE_HOME && !Homepage::launchShown() && !launchColumn;
        float inset = floating ? LAUNCH_MARGIN + LaunchButton::SIZE : 0.0f;
        float columnTop = 0.0f;
        float columnBottom = 0.0f;
        if (pages.page == PAGE_HOME && !underTitle && !floating && Homepage::column(columnTop, columnBottom) &&
            columnBottom + 8.0f > height() - Notifications::clearance()) {
            inset = Notifications::insetAbove(height(), columnTop);
        }
        Notifications::render(width(), height(), inset, underTitle, titleH + 6.0f);
        if (!Appearance::systemFrameRunning()) {
            float buttonY = (titleH - 26.0f) * 0.5f;
            if (windowButton("window#minimize", 0xf068, width() - windowButtonsWidth(), buttonY, false)) {
                minimizeRequested = true;
            }
            if (spec.maximizeButton) {
                maximizeButton(width() - 14 - 56, buttonY);
            }
            if (windowButton("window#close", 0xf00d, width() - 14 - 26, buttonY, true)) {
                closeRequested = true;
            }
        }
    }

    struct Panes {
        ShaderUtil::Shape plate;
        ShaderUtil::Shape main;
        int plateCodes = 0;
        int mainCodes = 0;
        float inner = 0.0f;
    };

    static Panes panes(float outer) {
        using ShaderUtil::SHELL_INNER;
        using ShaderUtil::SHELL_OUTER;
        Rect main = panel();
        float mainW = UiRender::transformW(main.w);
        float mainH = UiRender::transformW(main.h);
        Panes out;
        out.inner = ShaderUtil::styledRadius(UiRender::transformW(spec.innerRadius), mainW, mainH);
        float rim = ShaderUtil::styledRadius(UiRender::transformW(outer), UiRender::transformW(width()),
                                             UiRender::transformW(height()));
        out.plateCodes = topbarShown ? SHELL_OUTER | SHELL_OUTER << 2 | SHELL_OUTER << 4 | SHELL_OUTER << 6
                                     : SHELL_OUTER | SHELL_OUTER << 4;
        int bottomLeft = sideW < 1.0f ? SHELL_OUTER : SHELL_INNER;
        out.mainCodes = topbarShown ? SHELL_INNER | bottomLeft << 4 | SHELL_OUTER << 6
                                    : SHELL_INNER | SHELL_OUTER << 2 | SHELL_INNER << 4 | SHELL_OUTER << 6;
        float plateW = topbarShown ? width() : sideW + main.w * 0.5f;
        float plateRect[4] = {UiRender::transformX(0), UiRender::transformY(0), UiRender::transformW(plateW),
                              UiRender::transformW(height())};
        float mainRect[4] = {UiRender::transformX(main.x), UiRender::transformY(main.y), mainW, mainH};
        out.plate = ShaderUtil::paneShape(plateRect, out.plateCodes, out.inner, rim);
        out.main = ShaderUtil::paneShape(mainRect, out.mainCodes, out.inner, rim);
        return out;
    }

    static ShaderUtil::Shape band(float x, float y, float w, float h, float topLeft, float topRight, float bottomLeft,
                                  float bottomRight) {
        return {{x, y, w, h}, {topLeft, topRight, bottomLeft, bottomRight}};
    }

    static void paneImage(uint32_t texture, const ShaderUtil::Shape& shape, const ShaderUtil::Shape* hole) {
        float uSpan = 1.0f;
        float vSpan = 1.0f;
        coverSpan(width(), height(), MIN_WIDTH, MIN_HEIGHT, uSpan, vSpan);
        ShaderUtil::Paint paint;
        paint.kind = ShaderUtil::PAINT_IMAGE;
        paint.texture = texture;
        paint.frame[0] = UiRender::transformX(0);
        paint.frame[1] = UiRender::transformY(0);
        paint.frame[2] = UiRender::transformW(width());
        paint.frame[3] = UiRender::transformW(height());
        paint.uv[0] = uSpan;
        paint.uv[1] = vSpan;
        paint.uv[2] = (1.0f - uSpan) * 0.5f;
        paint.uv[3] = (1.0f - vSpan) * 0.5f;
        paint.tint = UiRender::applyAlpha(0xFFFFFFFFu);
        ShaderUtil::paint(shape, nullptr, hole, paint);
    }

    static void paneSolid(uint32_t color, const ShaderUtil::Shape& shape, const ShaderUtil::Shape* clip,
                          const ShaderUtil::Shape* hole) {
        ShaderUtil::Paint paint;
        paint.count = 1;
        paint.colors[0] = UiRender::applyAlpha(color);
        ShaderUtil::paint(shape, clip, hole, paint);
    }

    static void frame(float outer) {
        float r = spec.innerRadius;
        uint32_t sidebarFill = UiTheme::sidebar();
        uint32_t panelFill = UiTheme::panel();
        bool classic = !topbarShown && openW == SIDEBAR_WIDTH;
        float opacity = std::clamp(UiRender::alphaMul, 0.0f, 1.0f);
        if (ThemePaint::custom(ThemePaintSpec::WINDOW)) {
            ShaderUtil::Shape window = ShaderUtil::roundShape(
                    UiRender::transformX(0), UiRender::transformY(0), UiRender::transformW(width()),
                    UiRender::transformW(height()), UiRender::transformW(cornerRadius), 0xF);
            ThemePaint::layers(ThemePaintSpec::WINDOW, window, nullptr, nullptr, opacity);
        }
        Panes shapes = panes(outer);
        auto shade = [&]() {
            float sidebarShade = UiTheme::scalar(UiTheme::SIDEBAR_SHADE);
            float panelShade = UiTheme::scalar(UiTheme::PANEL_SHADE);
            sidebarFill = (sidebarFill & 0x00FFFFFFu) | ((uint32_t) Ui::round(255 * sidebarShade) << 24);
            panelFill = (panelFill & 0x00FFFFFFu) | ((uint32_t) Ui::round(255 * panelShade) << 24);
        };
        if (!previewing && Appearance::backgroundImage() && !classic) {
            uint32_t strong = Background::wholeSidebar();
            uint32_t light = Background::wholePanel();
            if (strong != 0 && light != 0 && !ShaderUtil::paintBroken()) {
                paneImage(strong, shapes.plate, nullptr);
                paneImage(light, shapes.main, nullptr);
                shade();
            }
        } else if (!previewing && Appearance::backgroundImage() && Background::sidebar() != 0 &&
                   Background::panel() != 0) {
            float uSpan = 1.0f;
            float vSpan = 1.0f;
            coverSpan(SIDEBAR_WIDTH + r, height(), SIDEBAR_WIDTH + r, MIN_HEIGHT, uSpan, vSpan);
            if (outer > 0.0f) {
                ShaderUtil::roundTex(UiRender::transformX(0), UiRender::transformY(0), UiRender::transformW(sideW + r),
                                     UiRender::transformW(height()), UiRender::transformW(outer), Background::sidebar(),
                                     0.0f, (1.0f - vSpan) * 0.5f, uSpan * (sideW + r) / (SIDEBAR_WIDTH + r),
                                     (1.0f + vSpan) * 0.5f, UiRender::applyAlpha(0xFFFFFFFFu), 0xF);
            } else {
                UiRender::texture(Background::sidebar(), 0, 0, sideW + r, height(), 0.0f, (1.0f - vSpan) * 0.5f,
                                  uSpan * (sideW + r) / (SIDEBAR_WIDTH + r), (1.0f + vSpan) * 0.5f);
            }
            coverSpan(width() - sideW, height(), MIN_WIDTH - SIDEBAR_WIDTH, MIN_HEIGHT, uSpan, vSpan);
            ShaderUtil::roundTex(UiRender::transformX(sideW), UiRender::transformY(0),
                                 UiRender::transformW(width() - sideW), UiRender::transformW(height()),
                                 UiRender::transformW(r), Background::panel(), 0.0f, (1.0f - vSpan) * 0.5f, uSpan,
                                 (1.0f + vSpan) * 0.5f, UiRender::applyAlpha(0xFFFFFFFFu), outer > 0.0f ? 0xF : 1 | 4);
            shade();
        }
        const float* plate = shapes.plate.rect;
        const float* pane = shapes.main.rect;
        float bottom = plate[1] + plate[3];
        float columnW = pane[0] - plate[0] + shapes.inner;
        const float* rim = shapes.plate.radii;
        ShaderUtil::Shape title = band(plate[0], plate[1], plate[2], pane[1] - plate[1], rim[0], rim[1], 0.0f, 0.0f);
        ShaderUtil::Shape column = topbarShown
                ? band(plate[0], pane[1], columnW, bottom - pane[1], 0.0f, 0.0f, rim[2], 0.0f)
                : band(plate[0], plate[1], columnW, plate[3], rim[0], 0.0f, rim[2], 0.0f);
        bool recapture = true;
        auto blurPane = [&](ThemePaintSpec::Region region, const ShaderUtil::Shape& shape,
                            const ShaderUtil::Shape* hole) {
            float amount = ThemePaint::blur(region);
            if (amount <= 0.0f || opacity <= 0.0f || shape.rect[2] <= 0.0f || shape.rect[3] <= 0.0f) {
                return;
            }
            ShaderUtil::kawaseBlur(shape, hole, amount, UiRender::applyAlpha(0xFFFFFFFFu), recapture);
            recapture = false;
        };
        if (topbarShown) {
            blurPane(ThemePaintSpec::TITLEBAR, title, nullptr);
        }
        blurPane(ThemePaintSpec::SIDEBAR, column, &shapes.main);
        blurPane(ThemePaintSpec::CONTENT, shapes.main, nullptr);

        bool titleCustom = topbarShown && ThemePaint::custom(ThemePaintSpec::TITLEBAR);
        bool sideCustom = ThemePaint::custom(ThemePaintSpec::SIDEBAR);
        bool contentCustom = ThemePaint::custom(ThemePaintSpec::CONTENT);
        uint32_t plateFill = titleCustom || sideCustom ? 0u : sidebarFill;
        uint32_t mainFill = contentCustom ? 0u : panelFill;
        Rect main = panel();
        if (topbarShown) {
            float plateRect[4] = {0.0f, 0.0f, width(), height()};
            float paneRect[4] = {main.x, main.y, main.w, main.h};
            UiRender::shellPanes(plateRect, shapes.plateCodes, paneRect, shapes.mainCodes, r, outer, plateFill,
                                 mainFill);
        } else {
            UiRender::shell(0, 0, width(), height(), sideW, r, plateFill, mainFill, outer);
        }
        if (topbarShown && (titleCustom || sideCustom)) {
            ShaderUtil::Shape above = band(plate[0] - 1.0f, plate[1] - 1.0f, plate[2] + 2.0f, pane[1] - plate[1] + 1.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f);
            ShaderUtil::Shape below =
                    band(plate[0] - 1.0f, pane[1], columnW + 1.0f, bottom - pane[1] + 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            if (titleCustom) {
                ThemePaint::layers(ThemePaintSpec::TITLEBAR, shapes.plate, &above, &shapes.main, opacity);
            } else {
                paneSolid(sidebarFill, shapes.plate, &above, &shapes.main);
            }
            if (sideCustom) {
                ThemePaint::layers(ThemePaintSpec::SIDEBAR, shapes.plate, &below, &shapes.main, opacity);
            } else {
                paneSolid(sidebarFill, shapes.plate, &below, &shapes.main);
            }
        } else if (sideCustom) {
            ShaderUtil::Shape within =
                    band(plate[0] - 1.0f, plate[1] - 1.0f, columnW + 1.0f, plate[3] + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            ThemePaint::layers(ThemePaintSpec::SIDEBAR, shapes.plate, &within, &shapes.main, opacity);
        }
        if (contentCustom) {
            ThemePaint::layers(ThemePaintSpec::CONTENT, shapes.main, nullptr, nullptr, opacity);
        }
        UiRender::outlineFade(main.x, main.y, main.w, main.h, r,
                              UiTheme::border(UiTheme::tokenOr(UiTheme::SHELL_OUTLINE, UiTheme::outline())), 1.0f,
                              0.5f);
    }

    std::string pageName() {
        switch (pages.page) {
            case PAGE_HOME: return "home";
            case PAGE_NEW_INSTANCE: return "new";
            case PAGE_SETTINGS: return "settings";
            case PAGE_ACCOUNTS: return "accounts";
            case PAGE_DOWNLOADS: return "downloads";
            case PAGE_EDIT_LOADERS: return "loaders";
            case PAGE_MULTIPLAYER: return "multiplayer";
            case PAGE_VERSIONS: return "versions";
            case PAGE_TRANSFERS: return "transfers";
            default: return "instance";
        }
    }

    static void decorate(bool above, float outer) {
        if (ThemePaintSpec::current().decorations.empty()) {
            return;
        }
        ThemePaint::Frames frames = {
                {{0.0f, 0.0f, width(), height()},
                 {0.0f, 0.0f, width(), titleH},
                 {0.0f, topbarShown ? titleH : 0.0f, sideW, height() - (topbarShown ? titleH : 0.0f)},
                 {0.0f, 0.0f, 0.0f, 0.0f}}};
        Rect main = panel();
        frames.rects[ThemePaintSpec::CONTENT][0] = main.x;
        frames.rects[ThemePaintSpec::CONTENT][1] = main.y;
        frames.rects[ThemePaintSpec::CONTENT][2] = main.w;
        frames.rects[ThemePaintSpec::CONTENT][3] = main.h;
        ShaderUtil::Shape window =
                ShaderUtil::roundShape(UiRender::transformX(0), UiRender::transformY(0), UiRender::transformW(width()),
                                       UiRender::transformW(height()), UiRender::transformW(cornerRadius), 0xF);
        ShaderUtil::Shape shellShape =
                ShaderUtil::roundShape(UiRender::transformX(0), UiRender::transformY(0), UiRender::transformW(width()),
                                       UiRender::transformW(height()), UiRender::transformW(outer), 0xF);
        ThemePaint::decorations(above, pageName(), frames, window, shellShape);
    }

    static void shell() {
        float outer = cornerRadius;
        frame(outer);
        decorate(false, outer);
        if (topbarShown) {
            topbar();
        }
        sidebar();
        content();

        // Every page but the home page offers the way back in the title bar, where no page keeps its own controls.
        if (pages.page != PAGE_HOME && !topbarShown) {
            std::string back = "返回主页";
            float backW = std::max(76.0f, Page::buttonWidth(back, 0xf015));
            float backY = (titleH - Page::FIELD_H) * 0.5f;
            titleControls.push_back({sideW + 20.0f, backY, backW, Page::FIELD_H});
            if (Page::button("topbar#home", back, sideW + 20.0f, backY, backW, false, NewUi::overlayCoversMouse(),
                             0xf015)) {
                pages.go(PAGE_HOME);
            }
        }

        decorate(true, outer);
        Drop::draw(width(), height());
    }

    float miniatureRadius(float w, float h) {
        return SHELL_RADIUS * std::min(w / MIN_WIDTH, h / MIN_HEIGHT);
    }

    static void placeholderContent() {
        Rect main = panel();
        float x = main.x + 22.0f;
        float w = main.w - 44.0f;
        float y = contentTop() + 16.0f;
        uint32_t ink = (UiTheme::textPrimary() & 0x00FFFFFFu) | 0x50000000u;
        uint32_t faint = (UiTheme::textMuted() & 0x00FFFFFFu) | 0x40000000u;
        UiRender::roundRect(x, y, std::min(180.0f, w * 0.4f), 16.0f, 8.0f, ink);
        y += 36.0f;
        for (int card = 0; card < 2; card++) {
            float h = card == 0 ? 120.0f : 84.0f;
            ThemePaint::fill(ThemePaintSpec::CARD, x, y, w, h, 12.0f, 0xF, UiTheme::surface());
            UiRender::outline(x, y, w, h, 12.0f, UiTheme::border(UiTheme::outlineSoft()));
            for (int row = 0; row < (card == 0 ? 3 : 2); row++) {
                float rowY = y + 18.0f + row * 34.0f;
                UiRender::roundRect(x + 18.0f, rowY, w * 0.3f, 12.0f, 6.0f, faint);
                if (card == 0 && row == 0) {
                    ThemePaint::primaryFill(x + w - 128.0f, rowY - 8.0f, 110.0f, 28.0f, 7.0f, 0.0f);
                } else {
                    UiRender::roundRect(x + w - 128.0f, rowY - 8.0f, 110.0f, 28.0f, 7.0f, UiTheme::surfaceRaised());
                }
            }
            y += h + 16.0f;
        }
    }

    void miniature(float x, float y, float w, float h) {
        miniature(x, y, w, h, nullptr, false);
    }

    void miniature(float x, float y, float w, float h, const ShellLayout::Spec* layout, bool placeholder) {
        float scale = std::min(w / MIN_WIDTH, h / MIN_HEIGHT);
        if (scale <= 0.0f) {
            return;
        }
        float savedWidth = layoutWidth;
        float savedHeight = layoutHeight;
        float savedUi = UiRender::uiScale;
        float savedX = UiRender::offsetX;
        float savedY = UiRender::offsetY;
        float savedMouseX = NewUi::mouseX;
        float savedMouseY = NewUi::mouseY;
        bool savedDown = NewUi::mouseDown;
        float savedScroll = listScroll;
        float savedSmooth = listSmooth;
        bool savedRestored = scrollRestored;
        size_t savedControls = titleControls.size();
        ShellLayout::Spec savedSpec = spec;
        bool savedTopbar = topbarShown;
        bool savedLaunch = launchColumn;
        float savedTitle = titleH;
        float savedOpen = openW;
        float savedOpenItem = openItemW;
        float savedFold = fold;
        float savedSide = sideW;
        float savedItem = itemW;
        float savedSearch = searchY;
        float savedListTop = listTop;
        float savedFull = fullListW;
        float savedList = listW;
        Page::ScrollScope previewScroll("miniature");

        UiRender::scissor(x, y, w, h);
        layoutWidth = w / scale;
        layoutHeight = h / scale;
        UiRender::offsetX = savedX + x * savedUi;
        UiRender::offsetY = savedY + y * savedUi;
        UiRender::uiScale = savedUi * scale;
        NewUi::mouseX = -1.0e6f;
        NewUi::mouseY = -1.0e6f;
        NewUi::mouseDown = false;
        if (layout != nullptr) {
            spec = ShellLayout::effective(*layout);
        }
        previewing = true;
        UiState::setFrozen(true);

        unfold();
        frame(SHELL_RADIUS);
        if (topbarShown) {
            topbar();
        }
        sidebar();
        if (placeholder) {
            placeholderContent();
        } else {
            home();
        }
        if (!Appearance::systemFrameRunning()) {
            float buttonY = (titleH - 26.0f) * 0.5f;
            windowButton("window#minimize", 0xf068, width() - windowButtonsWidth(), buttonY, false);
            if (spec.maximizeButton) {
                maximizeButton(width() - 14 - 56, buttonY);
            }
            windowButton("window#close", 0xf00d, width() - 14 - 26, buttonY, true);
        }

        UiState::setFrozen(false);
        previewing = false;
        spec = savedSpec;
        topbarShown = savedTopbar;
        launchColumn = savedLaunch;
        titleH = savedTitle;
        openW = savedOpen;
        openItemW = savedOpenItem;
        fold = savedFold;
        sideW = savedSide;
        itemW = savedItem;
        searchY = savedSearch;
        listTop = savedListTop;
        fullListW = savedFull;
        listW = savedList;
        layoutWidth = savedWidth;
        layoutHeight = savedHeight;
        UiRender::uiScale = savedUi;
        UiRender::offsetX = savedX;
        UiRender::offsetY = savedY;
        NewUi::mouseX = savedMouseX;
        NewUi::mouseY = savedMouseY;
        NewUi::mouseDown = savedDown;
        listScroll = savedScroll;
        listSmooth = savedSmooth;
        scrollRestored = savedRestored;
        titleControls.resize(savedControls);
        UiRender::unscissor();
    }

    bool isDragArea(float x, float y) {
        if (Compact::active() || NewUi::popupOpen || !UiOverlays::isEmpty()) {
            return false;
        }
        if (y < 0 || y >= titleH) {
            return false;
        }
        if (Appearance::systemFrameRunning() && x < sideW) {
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

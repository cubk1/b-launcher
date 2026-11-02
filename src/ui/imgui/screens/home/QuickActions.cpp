#include "ui/imgui/screens/home/QuickActions.h"
#include "ui/imgui/screens/home/HomeOptions.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/downloads/DownloadsScreen.h"
#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "app/system/Platform.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <algorithm>
#include <string>
#include <vector>

namespace QuickActions {

    namespace {
        constexpr float ROW_H = 48.0f;
        constexpr float ICON_X = 26.0f;
        constexpr float TEXT_X = 46.0f;
        const char* const SECTION = "quickActions";

        using Action = HomeOptions::Item;

        const Action ACTIONS[] = {
                {"newInstance", "下载游戏", 0xf019},        {"switchVersion", "切换版本", 0xf362},
                {"addAccount", "添加账号", 0xf234},         {"downloadMods", "下载模组", 0xf12e},
                {"downloadModpacks", "下载整合包", 0xf187}, {"importModpack", "导入整合包", 0xf56f},
                {"openFolder", "打开游戏目录", 0xf07c},
        };

        HomeOptions options(SECTION, ACTIONS, {"newInstance", "addAccount", "downloadMods"}, "关闭");

        void perform(const std::string& id) {
            if (id == "newInstance") {
                if (!Installer::running()) {
                    NewInstanceScreen::leaveModpack();
                }
                MainWindow::showNewInstance();
            } else if (id == "switchVersion") {
                MainWindow::showVersions();
            } else if (id == "addAccount") {
                MainWindow::showAccounts();
                AccountsScreen::openAdd();
            } else if (id == "downloadMods") {
                DownloadsScreen::open(Addons::Kind::Mod);
            } else if (id == "downloadModpacks") {
                DownloadsScreen::open(Addons::Kind::Modpack);
            } else if (id == "importModpack") {
                std::vector<std::filesystem::path> picked =
                        Platform::pickFiles(Platform::exeDirectory(), L"导入整合包", L"整合包", L"*.zip;*.mrpack");
                if (!picked.empty()) {
                    Drops::importPack(picked.front());
                }
            } else if (id == "openFolder") {
                const Instances::Entry* entry = Instances::find(Instances::selected());
                if (entry == nullptr) {
                    Notifications::add(Notifications::FAILED, "还没有选择实例", 2500);
                } else {
                    Platform::open(entry->gameDirectory);
                }
            }
        }

        void row(const Action& action, float x, float y, float w, int index, int count, bool blocked) {
            std::string id = std::string("home#quick#") + action.id;
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW_H);
            float hover = UiState::motion(id, hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                int corners = Page::rowCorners(index, count);
                UiRender::roundRect(x, y, w, ROW_H, 12.0f, UiTheme::overlay(Ui::round(14 * hover)), corners);
            }
            UiWidgets::icon(Ui::glyph(action.glyph), x + ICON_X, y + ROW_H * 0.5f, UiTheme::textPrimary(), 13.0f);
            UiRender::textY(action.label, x + TEXT_X, y, ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            UiWidgets::icon(Ui::glyph(0xf054), x + w - 20.0f, y + ROW_H * 0.5f,
                            UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), hover), 9.0f);
            if (!blocked && NewUi::click(x, y, w, ROW_H)) {
                UiSound::request(UiSound::PAGE);
                perform(action.id);
            }
        }
    }

    std::shared_ptr<Choice> choice() {
        return options.choice();
    }

    float height() {
        return options.shown().size() * ROW_H;
    }

    void draw(float x, float y, float w, bool blocked) {
        std::vector<const Action*> list = options.shown();
        if (list.empty()) {
            return;
        }
        HomeWidgets::panel(x, y, w, list.size() * ROW_H);
        for (size_t i = 1; i < list.size(); i++) {
            UiRender::hLine(x + 14.0f, x + w - 14.0f, y + i * ROW_H, UiTheme::outlineSoft());
        }
        for (size_t i = 0; i < list.size(); i++) {
            row(*list[i], x, y + i * ROW_H, w, (int) i, (int) list.size(), blocked);
        }
    }
}

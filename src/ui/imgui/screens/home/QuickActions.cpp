#include "ui/imgui/screens/home/QuickActions.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "app/system/Config.h"
#include "ui/imgui/screens/downloads/DownloadsScreen.h"
#include "app/instances/Drops.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/shell/MainWindow.h"
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

        struct Action {
            const char* id;
            const char* label;
            int glyph;
        };

        const Action ACTIONS[] = {
                {"newInstance", "下载游戏", 0xf019},     {"addAccount", "添加账号", 0xf234},
                {"downloadMods", "下载模组", 0xf12e},    {"downloadModpacks", "下载整合包", 0xf187},
                {"importModpack", "导入整合包", 0xf56f}, {"openFolder", "打开游戏目录", 0xf07c},
        };

        const char* const DEFAULTS[] = {"newInstance", "addAccount", "downloadMods"};

        bool loaded = false;
        std::vector<std::string> order;
        std::vector<std::string> enabled;

        const Action* byId(const std::string& id) {
            for (const Action& action : ACTIONS) {
                if (id == action.id) {
                    return &action;
                }
            }
            return nullptr;
        }

        const Action* byLabel(const std::string& label) {
            for (const Action& action : ACTIONS) {
                if (label == action.label) {
                    return &action;
                }
            }
            return nullptr;
        }

        std::vector<std::string> knownIds(const nlohmann::json& list) {
            std::vector<std::string> out;
            if (!list.is_array()) {
                return out;
            }
            for (const nlohmann::json& item : list) {
                if (item.is_string() && byId(item.get<std::string>()) != nullptr &&
                    std::find(out.begin(), out.end(), item.get<std::string>()) == out.end()) {
                    out.push_back(item.get<std::string>());
                }
            }
            return out;
        }

        void load() {
            if (loaded) {
                return;
            }
            loaded = true;
            const nlohmann::json& doc = Config::data();
            auto saved = doc.find(SECTION);
            bool present = saved != doc.end() && saved->is_object();
            order = present && saved->contains("order") ? knownIds((*saved)["order"]) : std::vector<std::string>();
            for (const Action& action : ACTIONS) {
                if (std::find(order.begin(), order.end(), action.id) == order.end()) {
                    order.push_back(action.id);
                }
            }
            if (present && saved->contains("enabled")) {
                enabled = knownIds((*saved)["enabled"]);
            } else {
                enabled.assign(std::begin(DEFAULTS), std::end(DEFAULTS));
            }
        }

        void save() {
            Config::data()[SECTION] = {{"order", order}, {"enabled", enabled}};
            Config::save();
        }

        bool on(const std::string& id) {
            return std::find(enabled.begin(), enabled.end(), id) != enabled.end();
        }

        std::vector<const Action*> shown() {
            load();
            std::vector<const Action*> out;
            for (const std::string& id : order) {
                if (on(id)) {
                    out.push_back(byId(id));
                }
            }
            return out;
        }

        void perform(const std::string& id) {
            if (id == "newInstance") {
                if (!Installer::running()) {
                    NewInstanceScreen::leaveModpack();
                }
                MainWindow::showNewInstance();
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

        class ActionChoice final : public Choice {
        public:
            ActionChoice() : Choice("settings#quickActions") {}

            std::vector<std::string> options() override {
                load();
                std::vector<std::string> out;
                for (const std::string& id : order) {
                    out.push_back(byId(id)->label);
                }
                return out;
            }

            bool multi() override {
                return true;
            }

            bool selected(const std::string& option) override {
                load();
                const Action* action = byLabel(option);
                return action != nullptr && on(action->id);
            }

            void choose(const std::string& option) override {
                load();
                const Action* action = byLabel(option);
                if (action == nullptr) {
                    return;
                }
                auto found = std::find(enabled.begin(), enabled.end(), action->id);
                if (found != enabled.end()) {
                    enabled.erase(found);
                } else {
                    enabled.push_back(action->id);
                }
                save();
            }

            std::string label() override {
                std::string out;
                for (const Action* action : shown()) {
                    out += (out.empty() ? "" : "、") + std::string(action->label);
                }
                return out.empty() ? std::string("关闭") : out;
            }

            bool reorderable() override {
                return true;
            }

            void reorder(int from, int to) override {
                load();
                int size = (int) order.size();
                if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
                    return;
                }
                std::string moved = order[from];
                order.erase(order.begin() + from);
                order.insert(order.begin() + to, moved);
                save();
            }

            std::string optionIcon(const std::string& option) override {
                const Action* action = byLabel(option);
                return action != nullptr ? Ui::glyph(action->glyph) : std::string();
            }
        };

        void row(const Action& action, float x, float y, float w, int index, int count, bool blocked) {
            std::string id = std::string("home#quick#") + action.id;
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW_H);
            float hover = UiState::motion(id, hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                int corners = (index == 0 ? 1 | 2 : 0) | (index == count - 1 ? 4 | 8 : 0);
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
        return std::make_shared<ActionChoice>();
    }

    float height() {
        return shown().size() * ROW_H;
    }

    void draw(float x, float y, float w, bool blocked) {
        std::vector<const Action*> list = shown();
        if (list.empty()) {
            return;
        }
        float h = list.size() * ROW_H;
        if (Appearance::backgroundImage()) {
            UiRender::blur(x, y, w, h, 12.0f, 8, true);
            UiRender::roundRect(x, y, w, h, 12.0f, (UiTheme::surface() & 0x00FFFFFFu) | 0x80000000u);
        } else {
            UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::surface());
        }
        UiRender::outline(x, y, w, h, 12.0f, UiTheme::outlineSoft());
        for (size_t i = 1; i < list.size(); i++) {
            UiRender::hLine(x + 14.0f, x + w - 14.0f, y + i * ROW_H, UiTheme::outlineSoft());
        }
        for (size_t i = 0; i < list.size(); i++) {
            row(*list[i], x, y + i * ROW_H, w, (int) i, (int) list.size(), blocked);
        }
    }
}

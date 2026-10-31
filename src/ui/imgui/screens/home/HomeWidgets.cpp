#include "ui/imgui/screens/home/HomeWidgets.h"
#include "app/accounts/Accounts.h"
#include "app/system/Config.h"
#include "app/system/ServerStatus.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/home/QuickActions.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace HomeWidgets {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float GAP = 12.0f;
        constexpr float PAD = 14.0f;
        constexpr float SERVER_H = 80.0f;
        constexpr float SERVER_ICON = 44.0f;
        constexpr float TITLE_LINE = 20.0f;
        constexpr float SMALL_LINE = 18.0f;
        constexpr float ACCOUNT_H = 60.0f;
        constexpr float FACE = 34.0f;
        constexpr float MENU_W = 200.0f;
        const char* const SECTION = "homeWidgets";
        const char* const SERVER_SECTION = "homeServer";

        struct Kind {
            const char* id;
            const char* label;
            int glyph;
        };

        const Kind KINDS[] = {
                {QUICK, "快捷操作", 0xf0e7},
                {SERVER, "服务器状态", 0xf233},
                {ACCOUNT, "当前账号", 0xf007},
        };

        bool loaded = false;
        std::vector<std::string> order;
        std::vector<std::string> chosen;

        const Kind* byId(const std::string& id) {
            for (const Kind& kind : KINDS) {
                if (id == kind.id) {
                    return &kind;
                }
            }
            return nullptr;
        }

        const Kind* byLabel(const std::string& label) {
            for (const Kind& kind : KINDS) {
                if (label == kind.label) {
                    return &kind;
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
            for (const Kind& kind : KINDS) {
                if (std::find(order.begin(), order.end(), kind.id) == order.end()) {
                    order.push_back(kind.id);
                }
            }
            if (present && saved->contains("enabled")) {
                chosen = knownIds((*saved)["enabled"]);
            } else {
                chosen = {QUICK};
            }
        }

        void save() {
            Config::data()[SECTION] = {{"order", order}, {"enabled", chosen}};
            Config::save();
        }

        bool on(const std::string& id) {
            return std::find(chosen.begin(), chosen.end(), id) != chosen.end();
        }

        class WidgetChoice final : public Choice {
        public:
            WidgetChoice() : Choice("settings#homeWidgets") {}

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
                const Kind* kind = byLabel(option);
                return kind != nullptr && on(kind->id);
            }

            void choose(const std::string& option) override {
                load();
                const Kind* kind = byLabel(option);
                if (kind == nullptr) {
                    return;
                }
                auto found = std::find(chosen.begin(), chosen.end(), kind->id);
                if (found != chosen.end()) {
                    chosen.erase(found);
                } else {
                    chosen.push_back(kind->id);
                }
                save();
            }

            std::string label() override {
                load();
                std::string out;
                for (const std::string& id : order) {
                    if (on(id)) {
                        out += (out.empty() ? "" : "、") + std::string(byId(id)->label);
                    }
                }
                return out.empty() ? std::string("只显示标志") : out;
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
                const Kind* kind = byLabel(option);
                return kind != nullptr ? Ui::glyph(kind->glyph) : std::string();
            }
        };

        std::string serverField(const char* key) {
            const nlohmann::json& doc = Config::data();
            auto section = doc.find(SERVER_SECTION);
            if (section == doc.end() || !section->is_object()) {
                return std::string();
            }
            auto field = section->find(key);
            return field != section->end() && field->is_string() ? field->get<std::string>() : std::string();
        }

        void setServerField(const char* key, const std::string& value) {
            if (serverField(key) == value) {
                return;
            }
            nlohmann::json& doc = Config::data();
            if (!doc[SERVER_SECTION].is_object()) {
                doc[SERVER_SECTION] = nlohmann::json::object();
            }
            doc[SERVER_SECTION][key] = value;
            Config::save();
        }

        void wash(const std::string& id, float x, float y, float w, float h, bool hovered) {
            float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::overlay(Ui::round(14 * lit)));
            }
        }

        void picture(uint32_t texture, float x, float y, float size, float radius) {
            ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                 UiRender::transformW(size), UiRender::transformW(radius), texture, 0.0f, 0.0f, 1.0f,
                                 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        void placeholder(int glyph, float x, float y, float size, float radius) {
            UiRender::roundRect(x, y, size, size, radius, UiTheme::surfaceRaised());
            UiWidgets::icon(Ui::glyph(glyph), x + size * 0.5f, y + size * 0.5f, UiTheme::textMuted(), size * 0.36f);
        }

        void letter(const std::string& name, float x, float y, float size, float radius) {
            UiRender::roundRect(x, y, size, size, radius, UiTheme::surfaceRaised());
            std::string text = name.empty() ? "?" : std::string(1, (char) std::toupper((unsigned char) name[0]));
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(text, x + (size - textW) * 0.5f, y, size, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        std::string fit(const std::string& text, float size, float room) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, room));
        }

        std::string oneLine(const std::string& text) {
            std::string out;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                std::string line =
                        Ui::trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (!line.empty()) {
                    out += (out.empty() ? "" : "  ") + line;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            std::string collapsed;
            for (char c : out) {
                if (c == ' ' && collapsed.size() >= 2 && collapsed.back() == ' ' &&
                    collapsed[collapsed.size() - 2] == ' ') {
                    continue;
                }
                collapsed += c;
            }
            return collapsed;
        }

        uint32_t latencyColor(int latency) {
            bool light = UiTheme::isLight();
            if (latency < 120) {
                return light ? 0xFF2F9E5Cu : 0xFF5CC98Au;
            }
            if (latency < 300) {
                return light ? 0xFFB7791Fu : 0xFFE3B34Du;
            }
            return UiTheme::danger();
        }

        void serverMenu(const std::string& address, float x, float y) {
            NewUi::openMenu(
                    "home#server#menu",
                    [address] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("刷新", 0xf2f1, [] { ServerStatus::refresh(); }));
                        items.push_back(ActionMenuItem::make("复制地址", 0xf0c5, [address] {
                            UiInput::setClipboard(address);
                            Notifications::add(Notifications::INFO, "已复制服务器地址", 2500);
                        }));
                        return items;
                    },
                    x, y, 160.0f);
        }

        void serverCard(float x, float y, float w, bool blocked) {
            const float h = SERVER_H;
            panel(x, y, w, h);
            std::string address = Ui::trim(serverAddress());
            std::string name = Ui::trim(serverName());
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            wash("home#server", x, y, w, h, hovered);
            float iconX = x + PAD;
            float iconY = y + (h - SERVER_ICON) * 0.5f;
            float textX = iconX + SERVER_ICON + 12.0f;
            float right = x + w - PAD;
            if (address.empty()) {
                float topY = y + (h - TITLE_LINE - SMALL_LINE) * 0.5f;
                placeholder(0xf233, iconX, iconY, SERVER_ICON, 10.0f);
                UiRender::textY(
                        fit(name.empty() ? std::string("服务器状态") : name, UiWidgets::TEXT_CONTROL, right - textX),
                        textX, topY, TITLE_LINE, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(fit("在设置的主页一栏填写服务器地址", UiWidgets::TEXT_SMALL, right - textX), textX,
                                topY + TITLE_LINE, SMALL_LINE, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                return;
            }
            const ServerStatus::State& state = ServerStatus::of(address);
            const ServerStatus::Result& result = state.result;
            uint32_t icon = result.ok ? Textures::of(result.favicon) : 0;
            if (icon != 0) {
                picture(icon, iconX, iconY, SERVER_ICON, 8.0f);
            } else {
                placeholder(0xf233, iconX, iconY, SERVER_ICON, 10.0f);
            }

            std::string players;
            std::string version;
            if (state.done && result.ok) {
                if (result.online >= 0) {
                    players = std::to_string(result.online) +
                            (result.max >= 0 ? " / " + std::to_string(result.max) : std::string()) + " 人在线";
                }
                version = result.version;
            } else if (!name.empty()) {
                players = address;
            }
            bool third = !players.empty() || !version.empty();
            const float titleY = y + (h - TITLE_LINE - SMALL_LINE - (third ? SMALL_LINE : 0.0f)) * 0.5f;
            const float detailY = titleY + TITLE_LINE;
            const float thirdY = detailY + SMALL_LINE;
            float titleRight = right;
            if (!state.done) {
                Page::spinner(right - 7.0f, titleY + TITLE_LINE * 0.5f, 6.0f, UiTheme::accent());
                titleRight = right - 22.0f;
            } else if (result.ok) {
                std::string text = std::to_string(result.latency) + " ms";
                uint32_t tint = latencyColor(result.latency);
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(text, right - textW, titleY, TITLE_LINE, tint, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiWidgets::icon(Ui::glyph(0xf012), right - textW - 12.0f, titleY + TITLE_LINE * 0.5f, tint, 10.0f);
                titleRight = right - textW - 26.0f;
            } else {
                std::string text = "离线";
                float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(text, right - textW, titleY, TITLE_LINE, UiTheme::danger(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                titleRight = right - textW - 10.0f;
            }
            UiRender::textY(fit(name.empty() ? address : name, UiWidgets::TEXT_CONTROL, titleRight - 8.0f - textX),
                            textX, titleY, TITLE_LINE, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

            std::string detail = !state.done ? std::string("正在连接…")
                    : result.ok              ? oneLine(result.motd)
                                             : result.error;
            UiRender::textY(fit(detail, UiWidgets::TEXT_SMALL, right - textX), textX, detailY, SMALL_LINE,
                            UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);

            float versionW = 0.0f;
            if (!version.empty()) {
                version = fit(version, UiWidgets::TEXT_SMALL, (right - textX) * 0.5f);
                versionW = UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(version, right - versionW, thirdY, SMALL_LINE, UiTheme::textFaint(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            if (!players.empty()) {
                UiRender::textY(fit(players, UiWidgets::TEXT_SMALL, right - textX - versionW - 12.0f), textX, thirdY,
                                SMALL_LINE, UiTheme::textFaint(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }

            if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
                NewUi::rightClick = false;
                serverMenu(address, NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::TICK);
                ServerStatus::refresh();
            }
        }

        void accountMenu(float x, float y) {
            NewUi::openMenu(
                    "home#account#menu",
                    [] {
                        MenuItems items;
                        const blb::auth::account* current = Accounts::current();
                        std::string currentKey = current != nullptr ? Accounts::keyOf(*current) : std::string();
                        for (const blb::auth::account& account : Accounts::list()) {
                            std::string key = Accounts::keyOf(account);
                            std::shared_ptr<ActionMenuItem> item = ActionMenuItem::make(account.name, 0xf007, [key] {
                                                                       Accounts::select(key);
                                                                   })->keys(Accounts::kindOf(account));
                            uint32_t face = Textures::of(Accounts::face(account));
                            if (face != 0) {
                                item->image(face, 4.0f);
                            }
                            if (key == currentKey) {
                                item->trailing(0xf00c);
                            }
                            items.push_back(item);
                        }
                        items.push_back(ActionMenuItem::make("添加账号", 0xf234, [] {
                            MainWindow::showAccounts();
                            AccountsScreen::openAdd();
                        }));
                        items.push_back(ActionMenuItem::make("管理账号", 0xf013, [] { MainWindow::showAccounts(); }));
                        return items;
                    },
                    x, y, MENU_W);
        }

        void accountCard(float x, float y, float w, bool blocked) {
            const float h = ACCOUNT_H;
            panel(x, y, w, h);
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            wash("home#account", x, y, w, h, hovered);
            float lit = UiState::getOrDefault("home#account", 0.0f);
            float faceX = x + PAD;
            float faceY = y + (h - FACE) * 0.5f;
            float textX = faceX + FACE + 12.0f;
            float room = x + w - 34.0f - textX;
            uint32_t chevron = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
            const blb::auth::account* account = Accounts::current();
            if (account == nullptr) {
                placeholder(0xf007, faceX, faceY, FACE, 8.0f);
                UiRender::textY("还没有账号", textX, y + 11.0f, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                UiRender::textY("点击添加账号", textX, y + 31.0f, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                UiWidgets::icon(Ui::glyph(0xf054), x + w - 20.0f, y + h * 0.5f, chevron, 9.0f);
                if (!blocked && NewUi::click(x, y, w, h)) {
                    UiSound::request(UiSound::PAGE);
                    MainWindow::showAccounts();
                    AccountsScreen::openAdd();
                }
                return;
            }
            std::string key = Accounts::keyOf(*account);
            uint32_t face = Textures::of(Accounts::face(*account));
            if (face != 0) {
                picture(face, faceX, faceY, FACE, 8.0f);
            } else {
                letter(account->name, faceX, faceY, FACE, 8.0f);
            }
            UiRender::textY(fit(account->name, UiWidgets::TEXT_CONTROL, room), textX, y + 11.0f, 20.0f,
                            UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            bool expired = Accounts::expired(key);
            std::string kind = expired ? std::string("需要重新登录") : Accounts::kindOf(*account);
            UiRender::textY(fit(kind, UiWidgets::TEXT_SMALL, room), textX, y + 31.0f, 18.0f,
                            expired ? UiTheme::danger() : UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (Accounts::busy(key)) {
                Page::spinner(x + w - 20.0f, y + h * 0.5f, 6.0f, UiTheme::accent());
            } else {
                UiWidgets::icon(Ui::glyph(0xf078), x + w - 20.0f, y + h * 0.5f, chevron, 9.0f);
            }
            if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
                NewUi::rightClick = false;
                accountMenu(NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::TICK);
                accountMenu(x + w - MENU_W, y + h + 6.0f);
            }
        }

        float heightOf(const std::string& id) {
            if (id == QUICK) {
                return QuickActions::height();
            }
            if (id == SERVER) {
                return SERVER_H;
            }
            if (id == ACCOUNT) {
                return ACCOUNT_H;
            }
            return 0.0f;
        }

        std::vector<std::string> visible() {
            load();
            std::vector<std::string> out;
            for (const std::string& id : order) {
                if (on(id) && heightOf(id) > 0.0f) {
                    out.push_back(id);
                }
            }
            return out;
        }
    }

    std::shared_ptr<Choice> choice() {
        return std::make_shared<WidgetChoice>();
    }

    bool enabled(const std::string& id) {
        load();
        return on(id);
    }

    std::string serverAddress() {
        return serverField("address");
    }

    void setServerAddress(const std::string& value) {
        setServerField("address", value);
    }

    std::string serverName() {
        return serverField("name");
    }

    void setServerName(const std::string& value) {
        setServerField("name", value);
    }

    void panel(float x, float y, float w, float h) {
        if (Appearance::backgroundImage()) {
            UiRender::blur(x, y, w, h, 12.0f, 8, true);
            UiRender::roundRect(x, y, w, h, 12.0f, (UiTheme::surface() & 0x00FFFFFFu) | 0x80000000u);
        } else {
            UiRender::roundRect(x, y, w, h, 12.0f, UiTheme::surface());
        }
        UiRender::outline(x, y, w, h, 12.0f, UiTheme::outlineSoft());
    }

    float height() {
        std::vector<std::string> list = visible();
        float total = 0.0f;
        for (const std::string& id : list) {
            total += heightOf(id);
        }
        return list.empty() ? 0.0f : total + GAP * (float) (list.size() - 1);
    }

    void draw(float x, float y, float w, bool blocked) {
        for (const std::string& id : visible()) {
            float h = heightOf(id);
            if (id == QUICK) {
                QuickActions::draw(x, y, w, blocked);
            } else if (id == SERVER) {
                serverCard(x, y, w, blocked);
            } else if (id == ACCOUNT) {
                accountCard(x, y, w, blocked);
            }
            y += h + GAP;
        }
    }
}

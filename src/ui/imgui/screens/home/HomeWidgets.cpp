#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/home/HomeLaunchPanel.h"
#include "ui/imgui/screens/home/HomeOptions.h"
#include "app/instances/Instances.h"
#include "app/instances/Screenshots.h"
#include "app/system/Config.h"
#include "app/system/ServerStatus.h"
#include "ui/imgui/screens/home/QuickActions.h"
#include "ui/imgui/screens/instances/InstancePage.h"
#include "ui/imgui/screens/instances/ScreenshotViewer.h"
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

#include <core/base/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

namespace HomeWidgets {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float GAP = 12.0f;
        constexpr float PAD = 14.0f;
        constexpr float COLUMN_MIN = 320.0f;
        constexpr float COLUMN_MAX = 520.0f;
        constexpr float COLUMN_NARROW = 360.0f;
        constexpr float SERVER_H = 80.0f;
        constexpr float SERVER_ICON = 44.0f;
        constexpr float TITLE_LINE = 20.0f;
        constexpr float SMALL_LINE = 18.0f;
        const char* const SECTION = "homeWidgets";
        const char* const SERVER_SECTION = "homeServer";
        const char* const ALIGN = "homeAlign";

        using Kind = HomeOptions::Item;

        const Kind KINDS[] = {
                {ACCOUNT, "当前账号", 0xf007},  {LAUNCH, "启动游戏", 0xf04b}, {QUICK, "快捷操作", 0xf0e7},
                {SERVER, "服务器状态", 0xf233}, {SHOTS, "最近截图", 0xf030},
        };

        HomeOptions options(SECTION, KINDS, {ACCOUNT, LAUNCH, SHOTS}, "只显示标志");

        constexpr float SHOTS_PAD = 12.0f;
        constexpr float SHOTS_GAP = 8.0f;
        constexpr float SHOTS_HEADER = 38.0f;
        constexpr float SHOTS_RADIUS = 8.0f;
        constexpr size_t SHOTS_COUNT = 3;

        std::string shotsInstance;
        std::chrono::steady_clock::time_point nextShotsScan;

        float shotsTileW(float w) {
            return (w - 2.0f * SHOTS_PAD - SHOTS_GAP * (float) (SHOTS_COUNT - 1)) / (float) SHOTS_COUNT;
        }

        float shotsHeight(float w) {
            return SHOTS_HEADER + shotsTileW(w) * 9.0f / 16.0f + SHOTS_PAD;
        }

        bool shotsAvailable() {
            const std::string& key = Instances::selected();
            if (key.empty() || Instances::find(key) == nullptr) {
                shotsInstance.clear();
                return false;
            }
            if (shotsInstance != key) {
                shotsInstance = key;
                nextShotsScan = {};
            }
            auto now = std::chrono::steady_clock::now();
            if (now >= nextShotsScan && !Screenshots::of(key).loading) {
                nextShotsScan = now + std::chrono::seconds(5);
                Screenshots::scan(key);
            }
            return !Screenshots::of(key).entries.empty();
        }

        void shotsTile(const std::string& key, const Screenshots::Entry& entry, int index, float x, float y, float w,
                       float h, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            float lit = UiState::motion("home#shots#" + std::to_string(index), hovered ? 1.0f : 0.0f, 20.0f);
            std::shared_ptr<const Pixels> thumb = Screenshots::thumbnail(entry.path, entry.modified);
            uint32_t texture = Textures::of(thumb);
            if (texture != 0 && thumb->width > 0 && thumb->height > 0) {
                float imageAspect = (float) thumb->width / (float) thumb->height;
                float tileAspect = w / h;
                float zoom = 1.0f - 0.06f * lit;
                float spanU = (imageAspect > tileAspect ? tileAspect / imageAspect : 1.0f) * zoom;
                float spanV = (imageAspect > tileAspect ? 1.0f : imageAspect / tileAspect) * zoom;
                float u0 = (1.0f - spanU) * 0.5f;
                float v0 = (1.0f - spanV) * 0.5f;
                ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(w),
                                     UiRender::transformW(h), UiRender::transformW(SHOTS_RADIUS), texture, u0, v0,
                                     u0 + spanU, v0 + spanV, UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                UiRender::roundRect(x, y, w, h, SHOTS_RADIUS, UiTheme::surfaceRaised());
                Page::spinner(x + w * 0.5f, y + h * 0.5f, 6.0f, UiTheme::accent());
            }
            UiRender::outline(x, y, w, h, SHOTS_RADIUS, UiRender::mix(UiTheme::outlineSoft(), UiTheme::accent(), lit));
            if (!blocked && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::PAGE);
                ScreenshotViewer::open(key, entry.path);
            }
        }

        void shotsCard(float x, float y, float w, bool blocked) {
            const std::string& key = Instances::selected();
            const Screenshots::List& list = Screenshots::of(key);
            if (list.entries.empty()) {
                return;
            }
            panel(x, y, w, shotsHeight(w));
            float left = x + SHOTS_PAD;
            float right = x + w - SHOTS_PAD;
            UiRender::textY("最近截图", left, y + 9.0f, 22.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);

            std::string more = "查看全部";
            float moreW = UiFont::width(more, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float linkX = right - 14.0f - moreW;
            float linkW = x + w - linkX + 6.0f;
            bool linkHovered = !blocked && NewUi::hit(linkX - 6.0f, y, linkW, SHOTS_HEADER - 4.0f);
            float linkLit = UiState::motion("home#shots#more", linkHovered ? 1.0f : 0.0f, 20.0f);
            uint32_t linkColor = UiRender::mix(UiTheme::textMuted(), UiTheme::accentText(), linkLit);
            UiRender::textY(more, linkX, y + 9.0f, 22.0f, linkColor, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiWidgets::icon(Ui::glyph(0xf054), right - 4.0f, y + 20.0f, linkColor, 9.0f);
            if (!blocked && NewUi::click(linkX - 6.0f, y, linkW, SHOTS_HEADER - 4.0f)) {
                UiSound::request(UiSound::PAGE);
                InstancePage::showScreenshots(key);
            }

            float tileW = shotsTileW(w);
            float tileH = tileW * 9.0f / 16.0f;
            float tileY = y + SHOTS_HEADER;
            size_t count = std::min(SHOTS_COUNT, list.entries.size());
            for (size_t i = 0; i < count; i++) {
                shotsTile(key, list.entries[i], (int) i, left + (float) i * (tileW + SHOTS_GAP), tileY, tileW, tileH,
                          blocked);
            }
        }

        std::string serverField(const char* key) {
            const nlohmann::json& doc = Config::data();
            auto section = doc.find(SERVER_SECTION);
            if (section == doc.end() || !section->is_object()) {
                return std::string();
            }
            return blb::json_text(*section, key);
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

        float heightOf(const std::string& id, float w) {
            if (id == ACCOUNT) {
                return HomeLaunchPanel::ACCOUNT_H;
            }
            if (id == LAUNCH) {
                return HomeLaunchPanel::LAUNCH_H;
            }
            if (id == QUICK) {
                return QuickActions::height();
            }
            if (id == SERVER) {
                return SERVER_H;
            }
            if (id == SHOTS) {
                return shotsAvailable() ? shotsHeight(w) : 0.0f;
            }
            return 0.0f;
        }

        std::vector<std::string> visible(float w) {
            std::vector<std::string> out;
            for (const auto* item : options.shown()) {
                if (heightOf(item->id, w) > 0.0f) {
                    out.push_back(item->id);
                }
            }
            return out;
        }
    }

    std::shared_ptr<Choice> choice() {
        return options.choice();
    }

    bool enabled(const std::string& id) {
        return options.enabled(id);
    }

    const std::vector<WidgetKind>& widgetKinds() {
        static const std::vector<WidgetKind> all = [] {
            std::vector<WidgetKind> out;
            for (const Kind& kind : KINDS) {
                out.push_back({kind.id, kind.label, kind.glyph});
            }
            return out;
        }();
        return all;
    }

    void setWidgetEnabled(const std::string& id, bool on) {
        if (options.enabled(id) == on) {
            return;
        }
        for (const WidgetKind& kind : widgetKinds()) {
            if (id == kind.id) {
                options.choice()->choose(kind.label);
                return;
            }
        }
    }

    bool alignBottom() {
        return blb::json_text(Config::data(), ALIGN) == "bottom";
    }

    void setAlignBottom(bool bottom) {
        if (alignBottom() == bottom) {
            return;
        }
        Config::data()[ALIGN] = bottom ? "bottom" : "center";
        Config::save();
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

    float width(float left, float right) {
        if (!enabled(LAUNCH)) {
            return COLUMN_NARROW;
        }
        return std::clamp(right - left - 80.0f, COLUMN_MIN, COLUMN_MAX);
    }

    float height(float w) {
        std::vector<std::string> list = visible(w);
        float total = 0.0f;
        for (const std::string& id : list) {
            total += heightOf(id, w);
        }
        return list.empty() ? 0.0f : total + GAP * (float) (list.size() - 1);
    }

    void draw(float x, float y, float w, bool blocked) {
        for (const std::string& id : visible(w)) {
            float h = heightOf(id, w);
            if (id == ACCOUNT) {
                HomeLaunchPanel::account(x, y, w, blocked);
            } else if (id == LAUNCH) {
                HomeLaunchPanel::launch(x, y, w, blocked);
            } else if (id == QUICK) {
                QuickActions::draw(x, y, w, blocked);
            } else if (id == SERVER) {
                serverCard(x, y, w, blocked);
            } else if (id == SHOTS) {
                shotsCard(x, y, w, blocked);
            }
            y += h + GAP;
        }
    }
}

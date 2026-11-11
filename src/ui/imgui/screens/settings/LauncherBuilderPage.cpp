#include "ui/imgui/screens/settings/LauncherBuilderPage.h"
#include "app/system/Customization.h"
#include "app/system/Features.h"
#include "app/system/LauncherBuilder.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/theme/ThemeStore.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <core/base/strings.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace LauncherBuilderPage {

    namespace blb = b_launcher_base;
    using LauncherBuilder::Project;

    namespace {
        constexpr float WIDE = 300.0f;
        constexpr float SELECT_W = 180.0f;
        constexpr int TAB_BASIC = 0;
        constexpr int TAB_HOME = 1;
        constexpr int TAB_SERVERS = 2;
        constexpr int TAB_LIMITS = 3;
        const char* const HOME_DEFAULT = "不指定";
        PageTransition tabs(TAB_BASIC);

        Project& project() {
            return LauncherBuilder::project();
        }

        void save() {
            LauncherBuilder::save();
        }

        void textField(const std::string& id, std::function<std::string()> read,
                       std::function<void(const std::string&)> write, const std::string& placeholder, float x, float y,
                       float w, bool blocked) {
            auto value = std::make_shared<TextBinding>(id, std::move(read), std::move(write), placeholder);
            value->limit = 300;
            TextFieldWidget widget(value);
            widget.box(x, y, w, Page::FIELD_H).bounds(x, y, w, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void textRow(const char* key, const char* label, std::string Project::* member, const std::string& placeholder,
                     float w, float y, bool blocked) {
            Page::label(label, y);
            textField(
                    std::string("builder#") + key, [member] { return project().*member; },
                    [member](const std::string& text) {
                        project().*member = Ui::trim(text);
                        save();
                    },
                    placeholder, Page::controlRight() - w, y + Page::FIELD_Y, w, blocked);
        }

        void toggleRow(const char* key, const char* label, std::function<bool()> read, std::function<void(bool)> write,
                       float y, bool blocked) {
            Page::label(label, y);
            std::string id = std::string("builder#") + key;
            auto flag = std::make_shared<FlagBinding>(id, std::move(read), std::move(write));
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, y + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::columnLeft(), y, Page::columnWidth(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void flagRow(const char* key, const char* label, bool Project::* member, float y, bool blocked) {
            toggleRow(
                    key, label, [member] { return project().*member; },
                    [member](bool on) {
                        project().*member = on;
                        save();
                    },
                    y, blocked);
        }

        void selectRow(const char* key, const char* label, const std::shared_ptr<Choice>& choice, float y,
                       bool blocked) {
            Page::label(label, y);
            SelectWidget widget(std::string("builder#") + key, choice);
            widget.bounds(Page::controlRight() - SELECT_W, y + Page::FIELD_Y, SELECT_W, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void fileRow(const char* key, const char* label, std::string Project::* member,
                     const std::function<std::optional<std::filesystem::path>(const std::filesystem::path&)>& pick,
                     float y, bool blocked) {
            Page::label(label, y);
            std::string id = std::string("builder#") + key;
            std::string path = project().*member;
            float pickX = Page::controlRight() - 27.0f;
            float right = pickX - 8.0f;
            if (!path.empty()) {
                float resetX = pickX - 30.0f;
                if (Page::iconButton(id + "#clear", 0xf2ed, "移除", resetX, y + 6.0f, blocked)) {
                    project().*member = std::string();
                    save();
                }
                right = resetX - 8.0f;
            }
            if (Page::iconButton(id + "#pick", 0xf07c, "选择文件", pickX, y + 6.0f, blocked)) {
                std::filesystem::path start =
                        path.empty() ? Platform::userDirectory() : blb::path_from_utf8(path).parent_path();
                if (std::optional<std::filesystem::path> picked = pick(start)) {
                    project().*member = blb::path_to_utf8(*picked);
                    save();
                }
            }
            float labelRight =
                    Page::columnLeft() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            std::string shown = path.empty()
                    ? std::string("未选择")
                    : UiFont::fitStart(Platform::display(blb::path_from_utf8(path)), UiFont::MEDIUM,
                                       UiWidgets::TEXT_CONTROL, std::max(0.0f, right - labelRight));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        std::optional<std::filesystem::path> pickXaml(const std::filesystem::path& start) {
            std::vector<std::filesystem::path> picked =
                    Platform::pickFiles(start, L"选择主页文件", L"XAML 文件", L"*.xaml;*.xml;*.txt");
            if (picked.empty()) {
                return std::nullopt;
            }
            return picked.front();
        }

        class FeatureChoice final : public Choice {
        public:
            FeatureChoice() : Choice("builder#hidden") {}

            std::vector<std::string> options() override {
                std::vector<std::string> out;
                for (const Features::Info& info : Features::all()) {
                    out.push_back(info.label);
                }
                return out;
            }

            bool multi() override {
                return true;
            }

            bool selected(const std::string& option) override {
                const Features::Info* info = find(option);
                std::vector<std::string>& hidden = project().hidden;
                return info != nullptr && std::find(hidden.begin(), hidden.end(), info->key) != hidden.end();
            }

            void choose(const std::string& option) override {
                const Features::Info* info = find(option);
                if (info == nullptr) {
                    return;
                }
                std::vector<std::string>& hidden = project().hidden;
                auto at = std::find(hidden.begin(), hidden.end(), info->key);
                if (at == hidden.end()) {
                    hidden.push_back(info->key);
                } else {
                    hidden.erase(at);
                }
                save();
            }

            std::string label() override {
                size_t count = project().hidden.size();
                return count == 0 ? std::string("无") : "已隐藏 " + std::to_string(count) + " 项";
            }

        private:
            static const Features::Info* find(const std::string& label) {
                for (const Features::Info& info : Features::all()) {
                    if (label == info.label) {
                        return &info;
                    }
                }
                return nullptr;
            }
        };

        std::shared_ptr<Choice> themeChoice() {
            return std::make_shared<ModeBinding>(
                    "builder#theme",
                    [] {
                        for (const Appearance::Theme& theme : Appearance::themes()) {
                            if (theme.key == project().theme) {
                                return theme.label;
                            }
                        }
                        return std::string("不指定");
                    },
                    [](const std::string& label) {
                        project().theme.clear();
                        for (const Appearance::Theme& theme : Appearance::themes()) {
                            if (theme.label == label) {
                                project().theme = theme.key;
                                break;
                            }
                        }
                        save();
                    },
                    [] {
                        std::vector<std::string> out = {"不指定"};
                        for (const Appearance::Theme& theme : Appearance::themes()) {
                            out.push_back(theme.label);
                        }
                        return out;
                    });
        }

        std::shared_ptr<Choice> homepageChoice() {
            return std::make_shared<ModeBinding>(
                    "builder#homepage",
                    [] {
                        const Project& p = project();
                        std::vector<std::string> choices = Homepage::choices();
                        if (p.homepage == "none") {
                            return choices.front();
                        }
                        if (p.homepage == "announcement") {
                            return choices[1];
                        }
                        if (p.homepage == "file") {
                            return choices[choices.size() - 2];
                        }
                        if (p.homepage == "url") {
                            return choices.back();
                        }
                        if (p.homepage == "preset") {
                            return p.homepagePreset;
                        }
                        return std::string(HOME_DEFAULT);
                    },
                    [](const std::string& label) {
                        Project& p = project();
                        std::vector<std::string> choices = Homepage::choices();
                        if (label == HOME_DEFAULT) {
                            p.homepage.clear();
                        } else if (label == choices.front()) {
                            p.homepage = "none";
                        } else if (label == choices[1]) {
                            p.homepage = "announcement";
                        } else if (label == choices[choices.size() - 2]) {
                            p.homepage = "file";
                        } else if (label == choices.back()) {
                            p.homepage = "url";
                        } else {
                            p.homepage = "preset";
                            p.homepagePreset = label;
                        }
                        save();
                    },
                    [] {
                        std::vector<std::string> out = {HOME_DEFAULT};
                        for (const std::string& choice : Homepage::choices()) {
                            out.push_back(choice);
                        }
                        return out;
                    });
        }

        std::shared_ptr<Choice> joinChoice() {
            return std::make_shared<ModeBinding>(
                    "builder#autoJoin",
                    [] {
                        const Project& p = project();
                        if (p.autoJoin == "choose") {
                            return std::string("让玩家选择");
                        }
                        for (const LauncherBuilder::Server& server : p.servers) {
                            if (!server.address.empty() && server.address == p.autoJoin) {
                                return server.name.empty() ? server.address : server.name;
                            }
                        }
                        return std::string("不自动进服");
                    },
                    [](const std::string& label) {
                        Project& p = project();
                        p.autoJoin.clear();
                        if (label == "让玩家选择") {
                            p.autoJoin = "choose";
                        } else {
                            for (const LauncherBuilder::Server& server : p.servers) {
                                if (!server.address.empty() &&
                                    label == (server.name.empty() ? server.address : server.name)) {
                                    p.autoJoin = server.address;
                                    break;
                                }
                            }
                        }
                        save();
                    },
                    [] {
                        std::vector<std::string> out = {"不自动进服", "让玩家选择"};
                        for (const LauncherBuilder::Server& server : project().servers) {
                            if (!server.address.empty()) {
                                out.push_back(server.name.empty() ? server.address : server.name);
                            }
                        }
                        return out;
                    });
        }

        void stopEditing(const std::string& prefix) {
            if (NewUi::textEditing.rfind(prefix, 0) == 0) {
                NewUi::cancelTextEdit();
            }
        }

        template <class Item>
        float listRows(const char* key, std::vector<Item>& items, std::string Item::* first, std::string Item::* second,
                       const char* firstHint, const char* secondHint, const char* addLabel, int extra, float y,
                       bool blocked) {
            int count = (int) items.size() + 1;
            Page::card(y, count + extra);
            std::string prefix = std::string("builder#") + key + "#";
            float left = Page::columnLeft() + 16.0f;
            float deleteX = Page::controlRight() - 27.0f;
            float firstW = 150.0f;
            float secondX = left + firstW + 10.0f;
            float secondW = std::max(80.0f, deleteX - 8.0f - secondX);
            int removed = -1;
            for (int i = 0; i < (int) items.size(); i++) {
                float rowY = y + i * Page::ROW_H;
                std::string row = prefix + std::to_string(i);
                textField(
                        row + "#a", [&items, i, first] { return i < (int) items.size() ? items[i].*first : ""; },
                        [&items, i, first](const std::string& text) {
                            if (i < (int) items.size()) {
                                items[i].*first = Ui::trim(text);
                                save();
                            }
                        },
                        firstHint, left, rowY + Page::FIELD_Y, firstW, blocked);
                textField(
                        row + "#b", [&items, i, second] { return i < (int) items.size() ? items[i].*second : ""; },
                        [&items, i, second](const std::string& text) {
                            if (i < (int) items.size()) {
                                items[i].*second = Ui::trim(text);
                                save();
                            }
                        },
                        secondHint, secondX, rowY + Page::FIELD_Y, secondW, blocked);
                if (Page::iconButton(row + "#delete", 0xf2ed, "删除", deleteX, rowY + 6.0f, blocked)) {
                    removed = i;
                }
            }
            if (removed >= 0) {
                stopEditing(prefix);
                items.erase(items.begin() + removed);
                save();
            }
            float addY = y + (count - 1) * Page::ROW_H;
            if (Page::navRow(prefix + "add", addLabel, "", addY, blocked, Page::rowCorners(count - 1, count + extra),
                             0xf067)) {
                stopEditing(prefix);
                items.push_back(Item{});
                save();
            }
            return count * Page::ROW_H;
        }

        int homeRows(const Project& p) {
            return 2 + (p.homepage == "url" || p.homepage == "file" ? 1 : 0);
        }

        int joinRows(const Project& p) {
            return p.servers.empty() ? 0 : 1;
        }

        int loginRows(const Project& p) {
            return 3 + (p.yggdrasil ? 1 : 0);
        }

        int lockRows(const Project& p) {
            return (p.theme.empty() ? 0 : 1) + (p.homepage.empty() ? 0 : 1) + (p.hidden.empty() ? 0 : 1) +
                    (p.copySettings ? 1 : 0);
        }

        float contentHeight(int tab, const Project& p) {
            int rows = 0;
            int captions = 2;
            int gaps = 1;
            switch (tab) {
                case TAB_HOME: rows = homeRows(p) + (int) p.links.size() + 1; break;
                case TAB_SERVERS:
                    rows = (int) p.servers.size() + 1 + joinRows(p);
                    captions = 0;
                    gaps = 0;
                    break;
                case TAB_LIMITS: {
                    int locks = lockRows(p);
                    rows = loginRows(p) + 1 + locks;
                    captions += locks > 0 ? 1 : 0;
                    gaps += locks > 0 ? 1 : 0;
                    break;
                }
                default:
                    rows = 9;
                    captions = 3;
                    gaps = 2;
                    break;
            }
            return (float) captions * Page::CAPTION_H + (float) rows * Page::ROW_H + (float) gaps * Page::GAP + 6.0f;
        }

        void basicTab(float y, bool blocked) {
            Page::caption("品牌", y);
            y += Page::CAPTION_H;
            Page::card(y, 4);
            textRow("windowTitle", "窗口标题", &Project::windowTitle, "B Launcher", WIDE, y, blocked);
            textRow("brandTitle", "侧栏标题", &Project::brandTitle, "B Launcher", WIDE, y + Page::ROW_H, blocked);
            fileRow("logo", "Logo", &Project::logoFile, Platform::pickLogo, y + 2 * Page::ROW_H, blocked);
            fileRow("icon", "程序图标", &Project::iconFile, Platform::pickIcon, y + 3 * Page::ROW_H, blocked);
            y += 4 * Page::ROW_H + Page::GAP;

            Page::caption("默认设置", y);
            y += Page::CAPTION_H;
            Page::card(y, 2);
            selectRow("theme", "主题", themeChoice(), y, blocked);
            flagRow("copySettings", "沿用当前启动器设置", &Project::copySettings, y + Page::ROW_H, blocked);
            y += 2 * Page::ROW_H + Page::GAP;

            Page::caption("版本", y);
            y += Page::CAPTION_H;
            Page::card(y, 3);
            textRow("by", "定制者", &Project::by, "无", 200.0f, y, blocked);
            textRow("version", "版本号", &Project::version, "1.0.0", 140.0f, y + Page::ROW_H, blocked);
            flagRow("updates", "检查启动器更新", &Project::updates, y + 2 * Page::ROW_H, blocked);
        }

        void homeTab(float y, bool blocked) {
            Project& p = project();
            int rows = homeRows(p);
            Page::caption("主页", y);
            y += Page::CAPTION_H;
            Page::card(y, rows);
            float rowY = y;
            selectRow("homepage", "主页内容", homepageChoice(), rowY, blocked);
            rowY += Page::ROW_H;
            if (p.homepage == "url") {
                textRow("homepageUrl", "主页网址", &Project::homepageUrl, "https://", WIDE, rowY, blocked);
                rowY += Page::ROW_H;
            } else if (p.homepage == "file") {
                fileRow("homepageFile", "主页文件", &Project::homepageFile, pickXaml, rowY, blocked);
                rowY += Page::ROW_H;
            }
            textRow("announcement", "公告地址", &Project::announcement, "默认公告", WIDE, rowY, blocked);
            y += rows * Page::ROW_H + Page::GAP;

            Page::caption("链接", y);
            y += Page::CAPTION_H;
            listRows("link", p.links, &LauncherBuilder::Link::label, &LauncherBuilder::Link::url, "名称", "https://",
                     "添加链接", 0, y, blocked);
        }

        void serversTab(float y, bool blocked) {
            Project& p = project();
            int join = joinRows(p);
            y += listRows("server", p.servers, &LauncherBuilder::Server::name, &LauncherBuilder::Server::address,
                          "名称", "服务器地址", "添加服务器", join, y, blocked);
            if (join > 0) {
                selectRow("autoJoin", "自动进服", joinChoice(), y, blocked);
            }
        }

        void limitsTab(float y, bool blocked) {
            Project& p = project();
            int logins = loginRows(p);
            Page::caption("登录方式", y);
            y += Page::CAPTION_H;
            Page::card(y, logins);
            flagRow("microsoft", "微软账户", &Project::microsoft, y, blocked);
            flagRow("yggdrasil", "外置登录", &Project::yggdrasil, y + Page::ROW_H, blocked);
            float loginY = y + 2 * Page::ROW_H;
            if (p.yggdrasil) {
                textRow("yggdrasilServer", "认证服务器", &Project::yggdrasilServer, "不限制", WIDE, loginY, blocked);
                loginY += Page::ROW_H;
            }
            flagRow("offline", "离线账户", &Project::offline, loginY, blocked);
            y += logins * Page::ROW_H + Page::GAP;

            Page::caption("功能", y);
            y += Page::CAPTION_H;
            Page::card(y, 1);
            selectRow("hidden", "隐藏的功能", std::make_shared<FeatureChoice>(), y, blocked);
            y += Page::ROW_H + Page::GAP;

            int locks = lockRows(p);
            if (locks == 0) {
                return;
            }
            Page::caption("禁止玩家修改", y);
            y += Page::CAPTION_H;
            Page::card(y, locks);
            if (!p.theme.empty()) {
                flagRow("themeLock", "主题", &Project::themeLock, y, blocked);
                y += Page::ROW_H;
            }
            if (!p.homepage.empty()) {
                flagRow("homepageLock", "主页", &Project::homepageLock, y, blocked);
                y += Page::ROW_H;
            }
            if (!p.hidden.empty()) {
                flagRow("featuresLock", "隐藏的功能", &Project::featuresLock, y, blocked);
                y += Page::ROW_H;
            }
            if (p.copySettings) {
                flagRow("settingsLock", "启动器设置", &Project::settingsLock, y, blocked);
            }
        }

        void generateBar(bool covered) {
            float y = MainWindow::height() - 28.0f - Page::FIELD_H;
            bool running = LauncherBuilder::running();
            std::string label = running ? "取消" : "生成";
            float w = std::max(88.0f, Page::buttonWidth(label, running ? 0 : 0xf019));
            float x = Page::controlRight() - w;
            if (running) {
                std::string now = LauncherBuilder::status();
                float nowW = UiFont::width(now, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(now, x - 14.0f - nowW, y, Page::FIELD_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                Page::spinner(x - 14.0f - nowW - 14.0f, y + Page::FIELD_H * 0.5f, 6.0f, UiTheme::accent());
            }
            if (Page::button("builder#go", label, x, y, w, !running, covered, running ? 0 : 0xf019)) {
                if (running) {
                    LauncherBuilder::cancel();
                } else {
                    LauncherBuilder::start(themeFiles());
                }
            }
        }
    }

    LauncherBuilder::ThemeFiles themeFiles() {
        LauncherBuilder::ThemeFiles out;
        std::set<std::string> seen;
        std::string id = project().theme;
        for (int depth = 0; depth < 8 && !id.empty() && seen.insert(id).second; depth++) {
            std::vector<std::pair<std::string, std::string>> files = ThemeStore::files(id);
            if (files.empty()) {
                break;
            }
            std::string next;
            for (auto& [path, data] : files) {
                if (path == "theme.json") {
                    nlohmann::json doc = nlohmann::json::parse(data, nullptr, false, true);
                    if (doc.is_object() && doc.contains("extends") && doc["extends"].is_string()) {
                        next = doc["extends"].get<std::string>();
                    }
                }
                out.emplace_back("themes/" + id + "/" + path, std::move(data));
            }
            id = next;
        }
        return out;
    }

    bool draw(bool covered, bool embedded) {
        bool back = Page::back("builder#back", "返回", Page::columnLeft() - 4.0f, Page::top() - 2.0f, covered);
        Page::title("自定义启动器", Page::columnLeft() + 30.0f, Page::top());

        float tabsY = Page::top() + 40.0f;
        std::vector<DownloadsParts::Segment> items = {
                {TAB_BASIC, "基本"}, {TAB_HOME, "主页"}, {TAB_SERVERS, "服务器"}, {TAB_LIMITS, "限制"}};
        int next = DownloadsParts::segments("builder", tabs.page, items, Page::columnLeft(), tabsY, covered,
                                            Page::columnWidth());
        if (next != tabs.page) {
            if (NewUi::textEditing.rfind("builder#", 0) == 0) {
                NewUi::commitTextEdit();
            }
            tabs.go(next);
        }

        float bottom = embedded ? MainWindow::height() - 14.0f : MainWindow::height() - 42.0f - Page::FIELD_H;
        tabs.begin();
        Page::Scroll body("builder#body#" + std::to_string(tabs.page), tabsY + DownloadsParts::SEGMENT_H + 14.0f,
                          bottom, contentHeight(tabs.page, project()));
        bool outside = body.begin();
        bool blocked = outside || covered || LauncherBuilder::running();
        float y = body.top - body.offset;
        switch (tabs.page) {
            case TAB_HOME: homeTab(y, blocked); break;
            case TAB_SERVERS: serversTab(y, blocked); break;
            case TAB_LIMITS: limitsTab(y, blocked); break;
            default: basicTab(y, blocked); break;
        }
        body.end();
        tabs.end();

        if (!embedded) {
            generateBar(covered);
        }
        return back;
    }
}

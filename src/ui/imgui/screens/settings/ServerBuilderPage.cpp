#include "ui/imgui/screens/settings/ServerBuilderPage.h"
#include "app/instances/Instances.h"
#include "app/instances/ServerBuilds.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/instances/FileTreeView.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

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

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ServerBuilderPage {

    namespace blb = b_launcher_base;
    using ServerBuilds::Options;

    namespace {
        constexpr float WIDE = 300.0f;
        constexpr float SELECT_W = 180.0f;
        constexpr float PICKER_W = 220.0f;
        const char* const NEW_WORLD = "不导入世界";
        constexpr int TAB_BASIC = 0;
        constexpr int TAB_SERVER = 1;
        constexpr int TAB_FILES = 2;
        PageTransition tabs(TAB_BASIC);
        std::string chosen;

        const Instances::Entry* usable(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            return entry != nullptr && entry->info.valid ? entry : nullptr;
        }

        const Instances::Entry* current() {
            if (const Instances::Entry* entry = usable(chosen)) {
                return entry;
            }
            const Instances::Entry* entry = usable(Instances::selected());
            if (entry == nullptr) {
                for (const Instances::Entry& candidate : Instances::list()) {
                    if (candidate.info.valid) {
                        entry = &candidate;
                        break;
                    }
                }
            }
            if (entry != nullptr) {
                chosen = entry->key;
                ServerBuilds::open(chosen);
            }
            return entry;
        }

        std::string instanceLabel(const Instances::Entry& entry) {
            int same = 0;
            for (const Instances::Entry& other : Instances::list()) {
                if (other.info.id == entry.info.id) {
                    same++;
                }
            }
            if (same <= 1) {
                return entry.info.id;
            }
            const Instances::Source* source = Instances::source(entry.source);
            return entry.info.id + "（" + (source != nullptr ? source->label : entry.source) + "）";
        }

        void choose(const std::string& key);

        std::shared_ptr<Choice> instanceChoice() {
            return std::make_shared<ModeBinding>(
                    "server#instance",
                    [] {
                        const Instances::Entry* entry = Instances::find(chosen);
                        return entry != nullptr ? instanceLabel(*entry) : std::string();
                    },
                    [](const std::string& text) {
                        for (const Instances::Entry& entry : Instances::list()) {
                            if (entry.info.valid && instanceLabel(entry) == text) {
                                choose(entry.key);
                                return;
                            }
                        }
                    },
                    [] {
                        std::vector<std::string> out;
                        for (const Instances::Entry& entry : Instances::list()) {
                            if (entry.info.valid) {
                                out.push_back(instanceLabel(entry));
                            }
                        }
                        return out;
                    });
        }

        void choose(const std::string& key) {
            if (usable(key) == nullptr || key == chosen) {
                return;
            }
            if (NewUi::textEditing.rfind("server#", 0) == 0) {
                NewUi::commitTextEdit();
            }
            chosen = key;
            ServerBuilds::open(key);
        }

        void textRow(const std::string& key, const char* field, const char* label, const std::string& placeholder,
                     float w, std::function<std::string(const Options&)> read,
                     std::function<void(Options&, const std::string&)> write, float rowY, bool blocked) {
            Page::label(label, rowY);
            auto value = std::make_shared<TextBinding>(
                    std::string("server#") + field + "#" + key,
                    [key, read] { return read(ServerBuilds::options(key)); },
                    [key, write](const std::string& text) {
                        write(ServerBuilds::options(key), Ui::trim(text));
                        ServerBuilds::save(key);
                    },
                    placeholder);
            value->limit = 200;
            float x = Page::controlRight() - w;
            float y = rowY + Page::FIELD_Y;
            TextFieldWidget widget(value);
            widget.box(x, y, w, Page::FIELD_H).bounds(x, y, w, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void numberRow(const std::string& key, const char* field, const char* label, int fallback,
                       int Options::* member, int low, int high, float rowY, bool blocked) {
            textRow(
                    key, field, label, std::to_string(fallback), 120.0f,
                    [member](const Options& o) { return o.*member > 0 ? std::to_string(o.*member) : std::string(); },
                    [member, low, high, fallback](Options& o, const std::string& text) {
                        int value = 0;
                        for (char c : text) {
                            if (c < '0' || c > '9' || value > 10000000) {
                                value = -1;
                                break;
                            }
                            value = value * 10 + (c - '0');
                        }
                        o.*member = text.empty() || value < 0 ? fallback : std::clamp(value, low, high);
                    },
                    rowY, blocked);
        }

        void toggleRow(const std::string& key, const char* field, const char* label, bool Options::* member, float rowY,
                       bool blocked) {
            Page::label(label, rowY);
            std::string id = std::string("server#") + field + "#" + key;
            auto flag = std::make_shared<FlagBinding>(
                    id, [key, member] { return ServerBuilds::options(key).*member; },
                    [key, member](bool on) {
                        ServerBuilds::options(key).*member = on;
                        ServerBuilds::save(key);
                    });
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::columnLeft(), rowY, Page::columnWidth(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void selectRow(const std::string& id, const char* label, const std::shared_ptr<Choice>& choice, float rowY,
                       bool blocked) {
            Page::label(label, rowY);
            SelectWidget widget(id, choice);
            widget.bounds(Page::controlRight() - SELECT_W, rowY + Page::FIELD_Y, SELECT_W, Page::FIELD_H)
                    .block(blocked);
            widget.paint();
        }

        std::shared_ptr<Choice> javaChoice(const std::string& key) {
            return ModeBinding::mapped<std::string>(
                    "server#java#" + key, [key] { return ServerBuilds::options(key).java; },
                    [key](std::string value) {
                        ServerBuilds::options(key).java = value;
                        ServerBuilds::save(key);
                    },
                    {{"instance", "跟随实例"}, {"system", "系统 Java"}, {"custom", "自选路径"}}, "跟随实例");
        }

        std::shared_ptr<Choice> worldChoice(const Instances::Entry& entry) {
            std::string key = entry.key;
            std::vector<std::string> saves = ServerBuilds::worlds(entry);
            return std::make_shared<ModeBinding>(
                    "server#world#" + key,
                    [key] {
                        const std::string& world = ServerBuilds::options(key).world;
                        return world.empty() ? std::string(NEW_WORLD) : world;
                    },
                    [key](const std::string& value) {
                        ServerBuilds::options(key).world = value == NEW_WORLD ? std::string() : value;
                        ServerBuilds::save(key);
                    },
                    [saves] {
                        std::vector<std::string> out = {NEW_WORLD};
                        out.insert(out.end(), saves.begin(), saves.end());
                        return out;
                    });
        }

        void javaPathRow(const std::string& key, float rowY, bool blocked) {
            Page::label("Java 路径", rowY);
            std::string path = ServerBuilds::options(key).javaPath;
            float pickX = Page::controlRight() - 27.0f;
            if (Page::iconButton("server#javaPick#" + key, 0xf07c, "选择 Java", pickX, rowY + 6.0f, blocked)) {
                std::filesystem::path start =
                        path.empty() ? Platform::userDirectory() : blb::path_from_utf8(path).parent_path();
                if (std::optional<std::filesystem::path> picked = Platform::pickJava(start)) {
                    ServerBuilds::options(key).javaPath = blb::path_to_utf8(*picked);
                    ServerBuilds::save(key);
                }
            }
            float right = pickX - 8.0f;
            float labelRight = Page::columnLeft() + 16.0f +
                    UiFont::width("Java 路径", UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            std::string shown = path.empty()
                    ? std::string("未选择")
                    : UiFont::fitStart(Platform::display(blb::path_from_utf8(path)), UiFont::MEDIUM,
                                       UiWidgets::TEXT_CONTROL, std::max(0.0f, right - labelRight));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void valueRow(const char* label, const std::string& value, float rowY) {
            Page::label(label, rowY);
            float w = UiFont::width(value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(value, Page::controlRight() - w, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        int basicRows(const Options& o) {
            return o.java == "custom" ? 5 : 4;
        }

        constexpr int SERVER_ROWS = 5;

        void basicTab(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& key = entry.key;
            const Options& o = ServerBuilds::options(key);
            Page::card(y, basicRows(o));
            if (ServerBuilds::supported(entry)) {
                valueRow("加载器", ServerBuilds::loaderText(entry), y);
            } else {
                Page::label("加载器", y);
                std::string tag = "不支持";
                float w = UiFont::width(tag, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
                Page::tag(tag, Page::controlRight() - w, y, UiTheme::dangerSoft(), UiTheme::danger());
            }
            y += Page::ROW_H;
            selectRow("server#java#" + key, "Java", javaChoice(key), y, blocked);
            y += Page::ROW_H;
            if (o.java == "custom") {
                javaPathRow(key, y, blocked);
                y += Page::ROW_H;
            }
            numberRow(key, "memory", "最大内存 (MB)", ServerBuilds::defaultMemory(entry), &Options::memoryMb, 512,
                      262144, y, blocked);
            y += Page::ROW_H;
            textRow(
                    key, "jvmArgs", "JVM 参数", "无", WIDE, [](const Options& v) { return v.jvmArgs; },
                    [](Options& v, const std::string& text) { v.jvmArgs = text; }, y, blocked);
        }

        void serverTab(const Instances::Entry& entry, float y, bool blocked) {
            const std::string& key = entry.key;
            Page::card(y, SERVER_ROWS);
            numberRow(key, "port", "端口", 25565, &Options::port, 1, 65535, y, blocked);
            toggleRow(key, "online", "正版验证", &Options::onlineMode, y + Page::ROW_H, blocked);
            numberRow(key, "players", "最大人数", 20, &Options::maxPlayers, 1, 100000, y + 2 * Page::ROW_H, blocked);
            textRow(
                    key, "motd", "服务器描述", "A Minecraft Server", WIDE, [](const Options& v) { return v.motd; },
                    [](Options& v, const std::string& text) { v.motd = text; }, y + 3 * Page::ROW_H, blocked);
            selectRow("server#world#" + key, "世界", worldChoice(entry), y + 4 * Page::ROW_H, blocked);
        }

        void filesTab(const std::string& key, float y, bool blocked) {
            auto tagger = [&key](const FileTree::Node& node) {
                if (ServerBuilds::clientOnly(key, node.path)) {
                    return FileTreeView::Tag{"仅客户端", true};
                }
                return FileTreeView::purpose(node);
            };
            std::string toggled;
            if (FileTreeView::draw("server#tree#" + key, ServerBuilds::tree(key), ServerBuilds::options(key).picks,
                                   Page::columnLeft(), y, Page::columnWidth(), blocked, tagger, &toggled)) {
                ServerBuilds::picked(key, toggled);
            }
        }

        float contentHeight(int tab, const std::string& key) {
            switch (tab) {
                case TAB_SERVER: return SERVER_ROWS * Page::ROW_H + 6.0f;
                case TAB_FILES: return FileTreeView::height(ServerBuilds::tree(key)) + 6.0f;
                default: return (float) basicRows(ServerBuilds::options(key)) * Page::ROW_H + 6.0f;
            }
        }

        void buildButton(const Instances::Entry& entry, bool blocked) {
            float y = MainWindow::height() - 28.0f - Page::FIELD_H;
            bool running = ServerBuilds::running();
            bool mine = running && ServerBuilds::runningKey() == entry.key;
            bool detecting = ServerBuilds::detecting(entry.key);
            std::string label = mine ? "取消" : "构建";
            float w = std::max(88.0f, Page::buttonWidth(label, mine ? 0 : 0xf233));
            float x = Page::controlRight() - w;
            std::string note = mine ? ServerBuilds::status()
                    : running       ? std::string("另一个实例正在生成服务端")
                    : detecting     ? std::string("正在识别仅客户端模组")
                                    : std::string();
            if (!note.empty()) {
                float noteW = UiFont::width(note, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(note, x - 14.0f - noteW, y, Page::FIELD_H,
                                mine ? UiTheme::textSecondary() : UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (mine || detecting) {
                    Page::spinner(x - 14.0f - noteW - 14.0f, y + Page::FIELD_H * 0.5f, 6.0f, UiTheme::accent());
                }
            }
            bool disabled = blocked || (running && !mine) || (!mine && (detecting || !ServerBuilds::supported(entry)));
            if (Page::button("server#go", label, x, y, w, !mine, disabled, mine ? 0 : 0xf233)) {
                if (mine) {
                    ServerBuilds::cancel();
                } else {
                    ServerBuilds::start(entry.key);
                    if (ServerBuilds::running()) {
                        MainWindow::showTransfers();
                    }
                }
            }
        }
    }

    void enter() {
        if (const Instances::Entry* entry = usable(Instances::selected())) {
            chosen = entry->key;
        }
        if (usable(chosen) != nullptr) {
            ServerBuilds::open(chosen);
        }
    }

    bool draw(bool covered) {
        bool back =
                Page::iconButton("server#back", 0xf053, "返回", Page::columnLeft() - 4.0f, Page::top() - 2.0f, covered);
        Page::title("生成服务端", Page::columnLeft() + 30.0f, Page::top());
        const Instances::Entry* found = current();
        if (found == nullptr) {
            UiRender::textY("还没有可用的实例", Page::columnLeft(), Page::top() + 40.0f, Page::ROW_H,
                            UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            return back;
        }
        const Instances::Entry& entry = *found;
        const std::string key = entry.key;
        bool mine = ServerBuilds::running() && ServerBuilds::runningKey() == key;

        SelectWidget picker("server#instance", instanceChoice());
        picker.bounds(Page::controlRight() - PICKER_W, Page::top(), PICKER_W, Page::FIELD_H).block(covered || mine);
        picker.paint();

        float tabsY = Page::top() + 40.0f;
        std::vector<DownloadsParts::Segment> items = {{TAB_BASIC, "基本"}, {TAB_SERVER, "服务器"}, {TAB_FILES, "文件"}};
        int next = DownloadsParts::segments("server", tabs.page, items, Page::columnLeft(), tabsY, covered,
                                            Page::columnWidth());
        if (next != tabs.page) {
            if (NewUi::textEditing.rfind("server#", 0) == 0) {
                NewUi::commitTextEdit();
            }
            tabs.go(next);
        }

        tabs.begin();
        float bottom = MainWindow::height() - 42.0f - Page::FIELD_H;
        Page::Scroll body("server#body#" + key + "#" + std::to_string(tabs.page),
                          tabsY + DownloadsParts::SEGMENT_H + 14.0f, bottom, contentHeight(tabs.page, key));
        bool outside = body.begin();
        bool blocked = outside || covered || mine;
        float y = body.top - body.offset;
        switch (tabs.page) {
            case TAB_SERVER: serverTab(entry, y, blocked); break;
            case TAB_FILES: filesTab(key, y, blocked); break;
            default: basicTab(entry, y, blocked); break;
        }
        body.end();
        tabs.end();

        buildButton(entry, covered);
        return back;
    }
}

#include "ui/imgui/screens/downloads/PackTree.h"
#include "app/downloads/ModNames.h"
#include "app/downloads/PackContents.h"
#include "ui/imgui/screens/downloads/RelationGraph.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/InputWidget.h"

#include <core/base/strings.hpp>

#include <map>
#include <set>

namespace PackTree {

    using Addons::Kind;
    using Addons::Status;
    using DownloadsParts::Route;
    using PackContents::Folder;
    using PackContents::Item;

    namespace {
        constexpr float ROW = 30.0f;
        constexpr float INDENT = 18.0f;
        const char* const SEARCH = "downloads#pack#search";

        struct Row {
            const Folder* folder = nullptr;
            const Item* item = nullptr;
            int depth = 0;
            std::string path;
            Kind kind = Kind::Mod;
        };

        std::map<std::string, std::string> chosen;
        std::map<std::string, std::set<std::string>> opened;
        bool graphView = true;

        Kind kindOf(const std::string& top) {
            if (top == "resourcepacks") {
                return Kind::ResourcePack;
            }
            if (top == "shaderpacks") {
                return Kind::Shader;
            }
            if (top == "datapacks") {
                return Kind::Datapack;
            }
            return Kind::Mod;
        }

        bool matches(const Item& item, const std::string& query) {
            if (b_launcher_base::icontains(item.title, query) || b_launcher_base::icontains(item.name, query)) {
                return true;
            }
            std::string zh = ModNames::zh(item.title);
            return !zh.empty() && b_launcher_base::icontains(zh, query);
        }

        bool any(const Folder& folder, const std::string& query) {
            for (const Item& item : folder.items) {
                if (matches(item, query)) {
                    return true;
                }
            }
            for (const Folder& child : folder.folders) {
                if (any(child, query)) {
                    return true;
                }
            }
            return false;
        }

        void flatten(const Folder& folder, const std::string& path, int depth, Kind kind,
                     const std::set<std::string>& open, const std::string& query, std::vector<Row>& out) {
            for (const Folder& child : folder.folders) {
                if (!query.empty() && !any(child, query)) {
                    continue;
                }
                std::string childPath = path.empty() ? child.name : path + "/" + child.name;
                Kind childKind = depth == 0 ? kindOf(child.name) : kind;
                out.push_back(Row{&child, nullptr, depth, childPath, childKind});
                if (!query.empty() || open.count(childPath) != 0) {
                    flatten(child, childPath, depth + 1, childKind, open, query, out);
                }
            }
            for (const Item& item : folder.items) {
                if (query.empty() || matches(item, query)) {
                    out.push_back(Row{nullptr, &item, depth, path, kind});
                }
            }
        }

        std::string sizeText(long long size) {
            char text[32];
            if (size >= 1024LL * 1024) {
                std::snprintf(text, sizeof(text), "%.1f MB", size / (1024.0 * 1024.0));
            } else if (size >= 1024) {
                std::snprintf(text, sizeof(text), "%.0f KB", size / 1024.0);
            } else {
                std::snprintf(text, sizeof(text), "%lld B", size < 0 ? 0 : size);
            }
            return text;
        }

        float badge(const std::string& text, float right, float centre, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            DownloadsParts::tag(text, right - w, centre, right + 1.0f, fill, ink);
            return w + 4.0f;
        }

        void folderRow(const Row& row, std::set<std::string>& open, float x, float y, float w, bool blocked) {
            bool isOpen = open.count(row.path) != 0;
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW);
            float lit = UiState::motion("downloads#tree#" + row.path + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y + 1.0f, w, ROW - 2.0f, 7.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            float ix = x + 8.0f + row.depth * INDENT;
            UiWidgets::icon(Ui::glyph(isOpen ? 0xf078 : 0xf054), ix + 4.0f, y + ROW * 0.5f, UiTheme::textMuted(), 8.0f);
            UiWidgets::icon(Ui::glyph(isOpen ? 0xf07c : 0xf07b), ix + 22.0f, y + ROW * 0.5f, UiTheme::accent(), 11.0f);
            std::string name = row.folder->name;
            UiRender::textY(name, ix + 34.0f, y, ROW, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            std::string count = std::to_string(row.folder->total);
            UiRender::textY(count, ix + 34.0f + UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f, y,
                            ROW, UiTheme::textFaint(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (hovered && NewUi::click(x, y, w, ROW)) {
                UiSound::request(UiSound::TICK);
                if (isOpen) {
                    open.erase(row.path);
                } else {
                    open.insert(row.path);
                }
            }
        }

        void itemRow(const Row& row, const Route& route, float x, float y, float w, bool blocked) {
            const Item& item = *row.item;
            bool usable = item.resolved && !item.projectId.empty();
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW);
            std::string id = "downloads#tree#" + row.path + "/" + item.name;
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y + 1.0f, w, ROW - 2.0f, 7.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            float ix = x + 8.0f + row.depth * INDENT + 16.0f;
            float right = x + w - 10.0f;
            if (item.file) {
                UiWidgets::icon(Ui::glyph(0xf15b), ix + 6.0f, y + ROW * 0.5f, UiTheme::textFaint(), 10.0f);
                std::string size = sizeText(item.size);
                float sizeW = UiFont::width(size, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(size, right - sizeW, y, ROW, UiTheme::textFaint(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                UiRender::textY(UiFont::fitEnd(item.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                               std::max(0.0f, right - sizeW - 12.0f - (ix + 18.0f))),
                                ix + 18.0f, y, ROW, UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                return;
            }
            DownloadsParts::projectIcon(item.iconUrl, ix, y + 5.0f, 20.0f, row.kind);
            float badges = 0.0f;
            float centre = y + ROW * 0.5f;
            if (item.bundled) {
                badges += badge("内置", right - badges, centre, UiTheme::surfaceRaised(), UiTheme::textSecondary());
            }
            if (item.manual) {
                badges += badge("需手动下载", right - badges, centre, UiTheme::dangerSoft(), UiTheme::danger());
            }
            if (item.optional) {
                badges += badge("可选", right - badges, centre, UiTheme::surfaceRaised(), UiTheme::textSecondary());
            }
            bool clientOnly = item.server == "unsupported" && item.client != "unsupported" && !item.client.empty();
            bool serverOnly = item.client == "unsupported" && item.server != "unsupported" && !item.server.empty();
            if (clientOnly || serverOnly) {
                badges += badge(clientOnly ? "仅客户端" : "仅服务端", right - badges, centre, UiTheme::accentSoft(36),
                                UiTheme::accentText());
            }
            float textX = ix + 28.0f;
            float room = right - badges - 8.0f - textX;
            std::string title =
                    row.kind == Kind::Mod ? DownloadsParts::displayTitle(item.title, Kind::Mod) : item.title;
            std::string shown = UiFont::fitEnd(title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, std::max(0.0f, room));
            UiRender::textY(shown, textX, y, ROW, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float after = textX + UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
            if (!item.version.empty() && after < right - badges - 20.0f) {
                UiRender::textY(UiFont::fitEnd(item.version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL,
                                               right - badges - 8.0f - after),
                                after, y, ROW, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (hovered && usable) {
                UiCursor::hand();
                if (NewUi::click(x, y, w, ROW)) {
                    UiSound::request(UiSound::PAGE);
                    Addons::Hit hit;
                    hit.id = item.projectId;
                    hit.slug = item.slug;
                    hit.title = item.title;
                    hit.iconUrl = item.iconUrl;
                    DownloadsParts::openProject(item.source, row.kind, hit);
                }
            }
        }
    }

    void draw(Route& route, const std::vector<const Addons::Version*>& versions, float top, float bottom,
              bool covered) {
        std::string key = std::to_string((int) route.source) + ":" + route.hit.id;
        const Addons::Version* version = nullptr;
        auto pick = chosen.find(key);
        for (const Addons::Version* each : versions) {
            if (pick != chosen.end() && each->id == pick->second) {
                version = each;
            }
        }
        if (version == nullptr) {
            for (const Addons::Version* each : versions) {
                if (each->type == "release") {
                    version = each;
                    break;
                }
            }
        }
        if (version == nullptr && !versions.empty()) {
            version = versions.front();
        }
        if (version == nullptr) {
            DownloadsParts::message("没有可预览的版本", UiTheme::textMuted(), top, bottom, false);
            return;
        }
        float x = DownloadsParts::mainX();
        float w = DownloadsParts::mainW();
        std::vector<std::pair<std::string, std::string>> labels;
        std::set<std::string> taken;
        for (const Addons::Version* each : versions) {
            std::string label = each->number.empty() ? each->name : each->number;
            while (!taken.insert(label).second) {
                label += " ";
            }
            labels.emplace_back(label, each->id);
            if (labels.size() >= 200) {
                break;
            }
        }
        std::string current = version->id;
        auto choice = std::make_shared<ModeBinding>(
                "downloads#pack#version",
                [labels, current] {
                    for (const auto& [label, id] : labels) {
                        if (id == current) {
                            return label;
                        }
                    }
                    return std::string();
                },
                [labels, key](const std::string& option) {
                    for (const auto& [label, id] : labels) {
                        if (label == option) {
                            chosen[key] = id;
                        }
                    }
                },
                [labels] {
                    std::vector<std::string> out;
                    for (const auto& [label, id] : labels) {
                        out.push_back(label);
                    }
                    return out;
                });
        float selectW = std::min(220.0f, w * 0.42f);
        DownloadsParts::select("downloads#pack#version", choice, x, top, selectW, covered);
        float switchX = Page::right() - 60.0f;
        graphView = RelationGraph::viewSwitch("downloads#pack#view", graphView, switchX, top, covered);
        float searchW = std::min(220.0f, w - selectW - 16.0f - 68.0f);
        float sx = switchX - 8.0f - searchW;
        bool focused = UiInput::isFocused(SEARCH);
        UiRender::roundRect(sx, top, searchW, Page::FIELD_H, 7.0f, UiTheme::overlay(10));
        UiRender::outline(sx, top, searchW, Page::FIELD_H, 7.0f,
                          focused ? UiTheme::accent() : UiTheme::border(UiTheme::outlineSoft()));
        UiWidgets::icon(Ui::glyph(0xf002), sx + 12.0f, top + Page::FIELD_H * 0.5f, UiTheme::textFaint(), 9.0f);
        InputWidget input(SEARCH);
        input.area(sx + 24.0f, searchW - 30.0f, UiWidgets::TEXT_CONTROL, 13.0f)
                .limit(64)
                .hint("搜索整合包内容")
                .bounds(sx, top, searchW, Page::FIELD_H)
                .block(covered);
        input.paint();
        std::string query = Ui::trim(UiInput::get(SEARCH));

        float infoY = top + Page::FIELD_H + 10.0f;
        const PackContents::Contents& contents = PackContents::contents(*version);
        if (contents.status == Status::Loading) {
            DownloadsParts::message("正在读取整合包目录", UiTheme::textMuted(), infoY, bottom, true);
            return;
        }
        if (contents.status == Status::Failed) {
            DownloadsParts::message("读取失败：" + contents.error, UiTheme::danger(), infoY, bottom, false);
            std::string retry = "重试";
            float bw = Page::buttonWidth(retry);
            if (Page::button("downloads#pack#retry", retry, x + (w - bw) * 0.5f, (infoY + bottom) * 0.5f + 20.0f, bw,
                             false, covered)) {
                PackContents::refresh(*version);
            }
            return;
        }
        float tx = x;
        float centre = infoY + 11.0f;
        float limit = Page::right();
        if (!contents.minecraft.empty()) {
            tx += DownloadsParts::tag("Minecraft " + contents.minecraft, tx, centre, limit, UiTheme::surfaceRaised(),
                                      UiTheme::textSecondary()) +
                    6.0f;
        }
        for (const std::string& loader : contents.loaders) {
            tx += DownloadsParts::tag(loader, tx, centre, limit, UiTheme::surfaceRaised(), UiTheme::textSecondary()) +
                    6.0f;
        }
        tx += DownloadsParts::tag(std::to_string(contents.mods) + " 个模组", tx, centre, limit, UiTheme::accentSoft(40),
                                  UiTheme::accentText()) +
                6.0f;
        if (contents.bundled > 0) {
            DownloadsParts::tag("内置 " + std::to_string(contents.bundled), tx, centre, limit, UiTheme::surfaceRaised(),
                                UiTheme::textSecondary());
        }
        if (graphView) {
            float graphTop = infoY + 30.0f;
            RelationGraph::pack(route, contents, version->id, query, x, graphTop, w, bottom - graphTop, covered);
            return;
        }
        auto found = opened.find(key + "#" + version->id);
        if (found == opened.end()) {
            found = opened.emplace(key + "#" + version->id, std::set<std::string>{"mods"}).first;
        }
        std::vector<Row> rows;
        flatten(contents.root, std::string(), 0, Kind::Mod, found->second, query, rows);
        float listTop = infoY + 30.0f;
        if (rows.empty()) {
            DownloadsParts::message(query.empty() ? "整合包是空的" : "没有匹配的内容", UiTheme::textMuted(), listTop,
                                    bottom, false);
            return;
        }
        Page::Scroll scroll("downloads#pack#" + key + "#" + version->id, listTop, bottom, rows.size() * ROW + 6.0f, x,
                            w);
        bool blocked = scroll.begin() || covered;
        float y = scroll.top - scroll.offset;
        for (const Row& row : rows) {
            if (y + ROW >= scroll.top && y <= scroll.bottom) {
                if (row.folder != nullptr) {
                    folderRow(row, found->second, x, y, w, blocked);
                } else {
                    itemRow(row, route, x, y, w, blocked);
                }
            }
            y += ROW;
        }
        scroll.end();
    }
}

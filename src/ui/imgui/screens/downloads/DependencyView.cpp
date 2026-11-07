#include "ui/imgui/screens/downloads/DependencyView.h"
#include "app/downloads/Dependencies.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/Projects.h"
#include "app/instances/Instances.h"
#include "app/instances/ModManager.h"
#include "ui/imgui/screens/downloads/Prerequisites.h"
#include "ui/imgui/screens/downloads/RelationGraph.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace DependencyView {

    using Addons::Kind;
    using Addons::Source;
    using Addons::Status;
    using DownloadsParts::Route;

    namespace {
        constexpr float ROW = 32.0f;
        constexpr float INDENT = 22.0f;
        constexpr float ICON = 20.0f;
        constexpr int DEPTH_LIMIT = 8;
        constexpr int OPEN_DEPTH = 2;

        enum class Role { Root, Required, Optional, Embedded, Incompatible };

        struct Row {
            Role role = Role::Root;
            int depth = 0;
            std::string path;
            std::string project;
            const Dependencies::Node* node = nullptr;
            std::vector<bool> rails;
            bool last = false;
            bool expandable = false;
            bool open = false;
            bool cycle = false;
        };

        struct Context {
            Source source = Source::Modrinth;
            std::string game;
            std::string loader;
            std::string owner;
            std::string key;
        };

        std::map<std::string, std::string> chosen;
        std::map<std::string, bool> toggled;
        bool graphView = true;

        Role roleOf(const Addons::Dependency& dependency) {
            if (dependency.incompatible) {
                return Role::Incompatible;
            }
            if (dependency.embedded) {
                return Role::Embedded;
            }
            return dependency.required ? Role::Required : Role::Optional;
        }

        float railX(float x, int depth) {
            return x + 14.0f + (float) depth * INDENT;
        }

        void collect(const Context& ctx, const Addons::Version& version, int depth, const std::string& path,
                     std::vector<bool>& rails, std::set<std::string>& ancestors, std::vector<Row>& out) {
            std::vector<const Addons::Dependency*> list;
            for (const Addons::Dependency& dependency : version.dependencies) {
                if (!dependency.projectId.empty() || !dependency.versionId.empty()) {
                    list.push_back(&dependency);
                }
            }
            std::stable_sort(list.begin(), list.end(), [](const Addons::Dependency* a, const Addons::Dependency* b) {
                return (int) roleOf(*a) < (int) roleOf(*b);
            });
            for (size_t i = 0; i < list.size(); i++) {
                const Addons::Dependency& dependency = *list[i];
                Row row;
                row.role = roleOf(dependency);
                row.depth = depth;
                row.rails = rails;
                row.last = i + 1 == list.size();
                if (row.role == Role::Required || row.role == Role::Optional) {
                    row.node = &Dependencies::resolve(version.source, dependency, ctx.game, ctx.loader);
                }
                row.project = dependency.projectId;
                if (row.project.empty() && row.node != nullptr && row.node->status == Status::Ready) {
                    row.project = row.node->version.projectId;
                }
                row.path = path + "/" + (row.project.empty() ? dependency.versionId : row.project);
                row.cycle = !row.project.empty() && ancestors.count(row.project) != 0;
                bool children = row.node != nullptr && row.node->status == Status::Ready &&
                        !row.node->version.dependencies.empty();
                row.expandable = children && !row.cycle && depth < DEPTH_LIMIT;
                auto found = toggled.find(ctx.key + row.path);
                bool wanted =
                        found != toggled.end() ? found->second : row.role == Role::Required && depth <= OPEN_DEPTH;
                row.open = row.expandable && wanted;
                out.push_back(row);
                if (row.open) {
                    rails.push_back(!row.last);
                    ancestors.insert(row.project);
                    collect(ctx, row.node->version, depth + 1, row.path, rails, ancestors, out);
                    ancestors.erase(row.project);
                    rails.pop_back();
                }
            }
        }

        float badge(const std::string& text, float right, float centre, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            DownloadsParts::tag(text, right - w, centre, right + 1.0f, fill, ink);
            return w + 4.0f;
        }

        void rails(const Row& row, float x, float y) {
            uint32_t line = UiTheme::divider();
            float centre = y + ROW * 0.5f;
            for (int k = 0; k + 1 < row.depth; k++) {
                if (row.rails[(size_t) k]) {
                    UiRender::vLine(railX(x, k), y, y + ROW, line);
                }
            }
            if (row.depth > 0) {
                float elbow = railX(x, row.depth - 1);
                UiRender::vLine(elbow, y, row.last ? centre : y + ROW, line);
                float end = row.expandable ? railX(x, row.depth) - 7.0f : railX(x, row.depth) + 6.0f;
                UiRender::hLine(elbow, end, centre, line);
            }
            if (row.role == Role::Root) {
                UiRender::vLine(railX(x, 0), centre + ICON * 0.5f + 3.0f, y + ROW, line);
            } else if (row.open) {
                UiRender::vLine(railX(x, row.depth), centre + 7.0f, y + ROW, line);
            }
        }

        void drawRow(const Context& ctx, const Row& row, const Route& route, const Addons::Version& root, float x,
                     float y, float w, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW);
            bool interactive = row.role != Role::Root && (row.expandable || !row.project.empty());
            float lit = UiState::motion("downloads#deps#" + ctx.key + row.path + "#hover",
                                        hovered && interactive ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y + 1.0f, w, ROW - 2.0f, 7.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            rails(row, x, y);
            float centre = y + ROW * 0.5f;
            float right = x + w - 10.0f;
            const Projects::Brief* brief =
                    row.role == Role::Root || row.project.empty() ? nullptr : Projects::brief(ctx.source, row.project);
            bool resolved = row.node != nullptr && row.node->status == Status::Ready;
            const Addons::Version* version = row.role == Role::Root ? &root : resolved ? &row.node->version : nullptr;
            std::string iconUrl = row.role == Role::Root ? route.hit.iconUrl
                    : brief != nullptr                   ? brief->iconUrl
                                                         : std::string();
            std::string title = row.role == Role::Root             ? route.hit.title
                    : brief != nullptr && !brief->title.empty()    ? brief->title
                    : version != nullptr && !version->name.empty() ? version->name
                                                                   : row.project;
            float iconX = row.role == Role::Root ? railX(x, 0) - ICON * 0.5f : railX(x, row.depth) + 8.0f;
            if (row.expandable) {
                UiWidgets::icon(Ui::glyph(row.open ? 0xf078 : 0xf054), railX(x, row.depth), centre,
                                UiTheme::textMuted(), 8.0f);
            }
            DownloadsParts::projectIcon(iconUrl, iconX, centre - ICON * 0.5f, ICON, Kind::Mod);

            float badges = 0.0f;
            switch (row.role) {
                case Role::Required:
                    badges += badge("必需", right, centre, UiTheme::accentSoft(36), UiTheme::accentText());
                    break;
                case Role::Optional:
                    badges += badge("可选", right, centre, UiTheme::surfaceRaised(), UiTheme::textSecondary());
                    break;
                case Role::Embedded:
                    badges += badge("内置", right, centre, UiTheme::surfaceRaised(), UiTheme::textSecondary());
                    break;
                case Role::Incompatible:
                    badges += badge("不兼容", right, centre, UiTheme::dangerSoft(), UiTheme::danger());
                    break;
                default: break;
            }
            if (row.cycle) {
                badges += badge("循环", right - badges, centre, UiTheme::surfaceRaised(), UiTheme::textMuted());
            }
            if (row.node != nullptr && row.node->status == Status::Failed) {
                float before = badges;
                badges += badge("无兼容版本", right - badges, centre, UiTheme::dangerSoft(), UiTheme::danger());
                if (hovered && !row.node->error.empty()) {
                    Page::tooltip(row.node->error, right - badges, y, badges - before, ROW);
                }
            }
            if (row.role != Role::Incompatible &&
                Prerequisites::installed(ctx.owner, ctx.source, row.role == Role::Root ? route.hit.id : row.project,
                                         version)) {
                badges += badge("已安装", right - badges, centre, UiTheme::surfaceRaised(), UiTheme::textSecondary());
            }
            if (row.node != nullptr && row.node->status == Status::Loading) {
                Page::spinner(right - badges - 8.0f, centre, 6.0f, UiTheme::accent());
                badges += 20.0f;
            }

            float textX = iconX + ICON + 8.0f;
            float room = right - badges - 8.0f - textX;
            std::string shown = UiFont::fitEnd(DownloadsParts::displayTitle(title, Kind::Mod), UiFont::MEDIUM,
                                               UiWidgets::TEXT_CONTROL, std::max(0.0f, room));
            uint32_t ink = row.role == Role::Incompatible ? UiTheme::textMuted() : UiTheme::textPrimary();
            UiRender::textY(shown, textX, y, ROW, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float after = textX + UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
            std::string number =
                    version != nullptr ? (!version->number.empty() ? version->number : version->name) : std::string();
            if (!number.empty() && after < right - badges - 20.0f) {
                UiRender::textY(
                        UiFont::fitEnd(number, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, right - badges - 8.0f - after),
                        after, y, ROW, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }

            if (!hovered || !interactive) {
                return;
            }
            bool onChevron = row.expandable && NewUi::hit(railX(x, row.depth) - 10.0f, y, 20.0f, ROW);
            if (onChevron || !row.project.empty()) {
                UiCursor::hand();
            }
            if (!NewUi::click(x, y, w, ROW)) {
                return;
            }
            if (onChevron || (row.expandable && row.project.empty())) {
                UiSound::request(UiSound::TICK);
                toggled[ctx.key + row.path] = !row.open;
                return;
            }
            UiSound::request(UiSound::PAGE);
            Addons::Hit hit;
            hit.id = row.project;
            if (brief != nullptr) {
                hit.slug = brief->slug;
                hit.title = brief->title;
                hit.iconUrl = brief->iconUrl;
            }
            DownloadsParts::openProject(ctx.source, brief != nullptr ? brief->kind : Kind::Mod, hit);
        }

        float chip(const std::string& text, float right, float centre) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            DownloadsParts::tag(text, right - w, centre, right + 1.0f, UiTheme::surfaceRaised(),
                                UiTheme::textSecondary());
            return w + 6.0f;
        }
    }

    void draw(Route& route, const std::vector<const Addons::Version*>& versions, const Mcmod::Ref* mcmod, float top,
              float bottom, bool covered) {
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
            DownloadsParts::message("没有可查看的版本", UiTheme::textMuted(), top, bottom, false);
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
                "downloads#deps#version",
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
        DownloadsParts::select("downloads#deps#version", choice, x, top, selectW, covered);

        Context ctx;
        ctx.source = route.source;
        ctx.owner = DownloadsParts::target();
        ctx.key = key + "#" + version->id + "#";
        Prerequisites::context(*version, ctx.owner, ctx.game, ctx.loader);
        if (Instances::find(ctx.owner) != nullptr) {
            ModManager::prepare(ctx.owner);
        }
        float switchX = Page::right() - 60.0f;
        graphView = RelationGraph::viewSwitch("downloads#deps#view", graphView, switchX, top, covered);
        float chipRight = switchX - 8.0f;
        float centre = top + Page::FIELD_H * 0.5f;
        if (!ctx.loader.empty()) {
            chipRight -= chip(Modrinth::label(ctx.loader), chipRight, centre);
        }
        if (!ctx.game.empty() && chipRight - x - selectW > 120.0f) {
            chip("Minecraft " + ctx.game, chipRight, centre);
        }

        float listTop = top + Page::FIELD_H + 12.0f;
        if (graphView) {
            RelationGraph::mod(route, *version, mcmod, ctx.game, ctx.loader, x, listTop, w, bottom - listTop, covered);
            return;
        }
        bool any = std::any_of(version->dependencies.begin(), version->dependencies.end(),
                               [](const Addons::Dependency& dependency) {
                                   return !dependency.projectId.empty() || !dependency.versionId.empty();
                               });
        if (!any) {
            DownloadsParts::message("这个版本没有依赖", UiTheme::textMuted(), listTop, bottom, false);
            return;
        }
        std::vector<Row> rows;
        Row head;
        head.role = Role::Root;
        head.path = "/";
        head.project = route.hit.id;
        head.open = true;
        rows.push_back(head);
        std::vector<bool> stack;
        std::set<std::string> ancestors = {route.hit.id};
        collect(ctx, *version, 1, std::string(), stack, ancestors, rows);

        Page::Scroll scroll("downloads#deps#" + ctx.key, listTop, bottom, (float) rows.size() * ROW + 6.0f, x, w);
        bool blocked = scroll.begin() || covered;
        float y = scroll.top - scroll.offset;
        for (const Row& row : rows) {
            if (y + ROW >= scroll.top && y <= scroll.bottom) {
                drawRow(ctx, row, route, *version, x, y, w, blocked);
            }
            y += ROW;
        }
        scroll.end();
    }
}

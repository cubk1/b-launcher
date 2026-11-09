#include "ui/imgui/screens/downloads/AuthorPage.h"
#include "ui/imgui/screens/downloads/DownloadButton.h"
#include "app/downloads/Projects.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <algorithm>
#include <map>

namespace AuthorPage {

    using Addons::Kind;
    using Addons::Source;
    using Addons::Status;
    using DownloadsParts::Route;

    namespace {
        constexpr float AVATAR = 56.0f;
        constexpr float TABS_Y = 66.0f;
        constexpr int ALL = -1;

        std::map<std::string, bool> byUpdate;

        int kindGlyph(Kind kind) {
            switch (kind) {
                case Kind::Modpack: return 0xf187;
                case Kind::ResourcePack: return 0xf5fd;
                case Kind::Shader: return 0xf185;
                case Kind::Datapack: return 0xf1c0;
                case Kind::World: return 0xf279;
                default: return 0xf12e;
            }
        }

        const char* kindLabel(Kind kind) {
            switch (kind) {
                case Kind::Modpack: return "整合包";
                case Kind::ResourcePack: return "资源包";
                case Kind::Shader: return "光影";
                case Kind::Datapack: return "数据包";
                case Kind::World: return "地图";
                default: return "模组";
            }
        }

        std::string fitEnd(const std::string& text, float available, float size) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, available));
        }

        float stat(int glyph, const std::string& text, float x, float y, float limit) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (x + 14.0f + w > limit) {
                return 0.0f;
            }
            UiWidgets::icon(Ui::glyph(glyph), x + 5.0f, y + 8.0f, UiTheme::textFaint(), 9.0f);
            UiRender::textY(text, x + 14.0f, y, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return 14.0f + w + 14.0f;
        }

        Authors::Kind normalized(const Route& route) {
            return route.author;
        }
    }

    void draw(Route& route, bool covered) {
        const Authors::Profile& profile =
                Authors::profile(normalized(route), route.authorId, route.authorName, route.authorAvatar);
        float x0 = DownloadsParts::mainX();
        float right = Page::right();
        if (Page::iconButton("downloads#back", 0xf053, "返回", x0 - 4.0f, Page::top() - 2.0f + (AVATAR - 28.0f) * 0.5f,
                             covered)) {
            DownloadsParts::back();
            return;
        }
        std::string avatar = !profile.avatar.empty() ? profile.avatar : route.authorAvatar;
        float avatarX = x0 + 30.0f;
        float avatarY = Page::top() - 2.0f;
        if (!avatar.empty()) {
            DownloadsParts::webImage(avatar, avatarX, avatarY, AVATAR, AVATAR, 14.0f, true);
        } else {
            UiRender::roundRect(avatarX, avatarY, AVATAR, AVATAR, 14.0f, UiTheme::surfaceRaised());
            UiWidgets::icon(Ui::glyph(route.author == Authors::Kind::ModrinthOrganization ? 0xf1ad : 0xf007),
                            avatarX + AVATAR * 0.5f, avatarY + AVATAR * 0.5f, UiTheme::textFaint(), 18.0f);
        }
        float textX = avatarX + AVATAR + 12.0f;
        bool sortable = route.author != Authors::Kind::Mcmod;
        std::vector<DownloadsParts::Segment> sorts = {{0, "下载量", 0xf019, 0}, {1, "最近更新", 0xf017, 0}};
        float sortW = sortable ? DownloadsParts::segmentsWidth(sorts) : 0.0f;
        float textRight = sortable ? right - sortW - 12.0f : right;
        float linkX = right - 28.0f;
        std::string site = route.author == Authors::Kind::Mcmod
                ? std::string("在 MC 百科上查看")
                : std::string("在 ") + Addons::sourceName(route.source) + " 上查看";
        if (!profile.url.empty() &&
            Page::iconButton("downloads#author#open", 0xf35d, site, linkX, Page::top() - 6.0f, covered)) {
            Platform::openUrl(profile.url);
        }
        std::string name = !profile.name.empty() ? profile.name : route.authorName;
        std::string heading = fitEnd(name, linkX - 8.0f - textX, Page::TITLE_SIZE);
        UiRender::textY(heading, textX, Page::top() - 4.0f, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                        Page::TITLE_SIZE);
        float aliasX = textX + UiFont::width(heading, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        std::string aliases;
        for (const std::string& alias : profile.aliases) {
            aliases += (aliases.empty() ? "" : "、") + alias;
        }
        if (!aliases.empty() && aliasX < linkX - 8.0f) {
            UiRender::textY(fitEnd(aliases, linkX - 8.0f - aliasX, UiWidgets::TEXT_CONTROL), aliasX, Page::top() - 4.0f,
                            24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }
        std::string role = route.author == Authors::Kind::ModrinthOrganization ? std::string("组织")
                : route.author == Authors::Kind::Mcmod                         ? std::string("MC百科作者")
                                                                               : std::string("创作者");
        std::string bio = !profile.bio.empty() ? profile.bio : role;
        std::string flat = bio;
        std::replace(flat.begin(), flat.end(), '\n', ' ');
        UiRender::textY(fitEnd(flat, textRight - textX, UiWidgets::TEXT_SMALL), textX, Page::top() + 20.0f, 16.0f,
                        UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        float sx = textX;
        float statY = Page::top() + 38.0f;
        if (profile.status == Status::Ready) {
            sx += stat(0xf1b3, std::to_string(profile.projects.size()) + " 个项目", sx, statY, textRight);
            if (profile.downloads > 0) {
                sx += stat(0xf019, Addons::count(profile.downloads) + " 次下载", sx, statY, textRight);
            }
            if (!profile.joined.empty()) {
                sx += stat(0xf073, DownloadsParts::shortDate(profile.joined) + " 加入", sx, statY, textRight);
            }
        }

        float tabsY = Page::top() + TABS_Y;
        float top = tabsY + DownloadsParts::SEGMENT_H + 12.0f;
        float bottom = MainWindow::height() - 14.0f - DownloadButton::inset();
        if (profile.status == Status::Loading) {
            DownloadsParts::message("正在获取作者信息", UiTheme::textMuted(), top, bottom, true);
            return;
        }
        if (profile.status == Status::Failed) {
            DownloadsParts::message("获取作者信息失败：" + profile.error, UiTheme::danger(), top, bottom, false);
            std::string label = "重试";
            float w = Page::buttonWidth(label);
            if (Page::button("downloads#author#retry", label, x0 + (DownloadsParts::mainW() - w) * 0.5f,
                             (top + bottom) * 0.5f + 20.0f, w, false, covered)) {
                Authors::refresh(normalized(route), route.authorId);
            }
            return;
        }
        std::map<Kind, size_t> perKind;
        for (const Authors::Project& project : profile.projects) {
            perKind[project.kind]++;
        }
        std::vector<DownloadsParts::Segment> items = {{ALL, "全部", 0xf00a, profile.projects.size()}};
        for (Kind kind : {Kind::Mod, Kind::Modpack, Kind::ResourcePack, Kind::Shader, Kind::Datapack, Kind::World}) {
            auto found = perKind.find(kind);
            if (found != perKind.end() && perKind.size() > 1) {
                items.push_back({(int) kind, kindLabel(kind), kindGlyph(kind), found->second});
            }
        }
        if (std::none_of(items.begin(), items.end(), [&](const auto& item) { return item.id == route.tab; })) {
            route.tab = ALL;
        }
        if (items.size() > 1) {
            route.tab =
                    DownloadsParts::segments("author", route.tab, items, x0, tabsY, covered, DownloadsParts::mainW());
        } else {
            top = tabsY;
        }
        std::string key = std::to_string((int) route.author) + ":" + route.authorId;
        bool& updated = byUpdate[key];
        if (sortable) {
            float sortY = avatarY + AVATAR - DownloadsParts::SEGMENT_H;
            updated =
                    DownloadsParts::segments("author#sort", updated ? 1 : 0, sorts, right - sortW, sortY, covered) == 1;
        }
        std::vector<const Authors::Project*> list;
        for (const Authors::Project& project : profile.projects) {
            if (route.tab == ALL || (int) project.kind == route.tab) {
                list.push_back(&project);
            }
        }
        if (sortable && updated) {
            std::stable_sort(list.begin(), list.end(), [](const Authors::Project* a, const Authors::Project* b) {
                return a->updated > b->updated;
            });
        }
        if (list.empty()) {
            DownloadsParts::message("没有项目", UiTheme::textMuted(), top, bottom, false);
            return;
        }
        float w = DownloadsParts::mainW();
        float rowH = DownloadsParts::RESULT_H;
        Page::Scroll scroll("downloads#author#" + key + "#" + std::to_string(route.tab), top, bottom,
                            list.size() * rowH, x0, w);
        bool blocked = scroll.begin() || covered;
        float y = scroll.top - scroll.offset;
        Page::listCard(x0, y, w, rowH, (int) list.size());
        for (size_t i = 0; i < list.size(); i++) {
            const Authors::Project& project = *list[i];
            float rowY = y + i * rowH;
            if (rowY + rowH < scroll.top || rowY > scroll.bottom) {
                continue;
            }
            Addons::Hit hit = project.hit;
            Source source = route.source;
            Mcmod::Ref ref;
            ref.id = project.mcmodId;
            ref.type = project.mcmodType;
            ref.url = project.hit.pageUrl;
            ref.keys = project.keys;
            if (project.mcmodId > 0) {
                if (!project.roles.empty()) {
                    hit.categories = {project.roles};
                }
                std::string slug = Mcmod::slugOn(ref, "modrinth");
                if (const Projects::Brief* brief = slug.empty() ? nullptr : Projects::brief(Source::Modrinth, slug)) {
                    hit.iconUrl = brief->iconUrl;
                }
                source = Source::CurseForge;
            }
            std::string id = key + "#" + (hit.id.empty() ? std::to_string(project.mcmodId) : hit.id);
            if (DownloadsParts::projectRow(id, hit, project.kind, source, x0, rowY, w, (int) i, (int) list.size(),
                                           blocked)) {
                if (project.mcmodId > 0) {
                    DownloadsParts::openMcmod(ref);
                } else {
                    DownloadsParts::openProject(route.source, project.kind, project.hit);
                }
            }
        }
        scroll.end();
    }
}

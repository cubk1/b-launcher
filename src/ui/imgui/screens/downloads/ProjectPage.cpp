#include "ui/imgui/screens/downloads/ProjectPage.h"
#include "ui/imgui/screens/downloads/DependencyView.h"
#include "ui/imgui/screens/downloads/DownloadButton.h"
#include "ui/imgui/screens/downloads/PackTree.h"
#include "ui/imgui/screens/downloads/SourceMarks.h"
#include "app/downloads/Mcmod.h"
#include "app/downloads/ModNames.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/ProjectBrowser.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/downloads/Projects.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/instances/ScreenshotViewer.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/MarkdownView.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>

namespace ProjectPage {

    using Addons::Kind;
    using Addons::Source;
    using Addons::Status;
    using DownloadsParts::mainW;
    using DownloadsParts::mainX;
    using DownloadsParts::Route;

    namespace {
        constexpr float ICON = 56.0f;
        constexpr float TABS_Y = 66.0f;
        constexpr float SIDEBAR_W = 220.0f;
        constexpr float WIDE = 680.0f;
        constexpr float TABLE_HEAD = 26.0f;
        constexpr float LINK_H = 24.0f;
        constexpr float PERSON_H = 32.0f;
        constexpr float CHIP_H = 20.0f;
        constexpr int CHANGELOG_STEP = 20;

        enum Tab { DESCRIPTION, VERSIONS, CHANGELOG, GALLERY, CONTENTS, DEPENDENCIES };

        struct State {
            std::string expanded;
            int changelogShown = CHANGELOG_STEP;
            std::string typeFilter;
            int bodySource = -1;
        };

        struct Doc {
            std::unique_ptr<MarkdownView> view;
            long long used = 0;
        };

        std::map<std::string, State> states;
        std::map<std::string, Doc> docs;
        long long frame = 0;

        float clipTop = 0.0f;
        float clipBottom = 0.0f;

        const char* const ANY_TYPE = "全部类型";

        std::string keyOf(const Route& route) {
            return std::to_string((int) route.source) + ":" + route.hit.id;
        }

        MarkdownView& doc(const std::string& id, const std::string& text, bool html, float size = 13.5f) {
            Doc& entry = docs[id];
            if (!entry.view) {
                entry.view = std::make_unique<MarkdownView>();
                entry.view->onLink = [](const std::string& url) { DownloadsParts::follow(url); };
            }
            entry.view->textSize = size;
            entry.view->load(text, html ? Markdown::Format::Html : Markdown::Format::Markdown, id);
            entry.used = frame;
            return *entry.view;
        }

        void prune() {
            for (auto it = docs.begin(); it != docs.end();) {
                if (frame - it->second.used > 600) {
                    it = docs.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::string fitEnd(const std::string& text, float available, float size) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, available));
        }

        std::string bytes(long long size) {
            char text[32];
            if (size >= 1024LL * 1024) {
                std::snprintf(text, sizeof(text), "%.1f MB", size / (1024.0 * 1024.0));
            } else if (size >= 1024) {
                std::snprintf(text, sizeof(text), "%.0f KB", size / 1024.0);
            } else {
                std::snprintf(text, sizeof(text), "%lld B", std::max(0LL, size));
            }
            return text;
        }

        bool textLink(const std::string& id, const std::string& text, float x, float y, float h, float size,
                      uint32_t color, bool blocked) {
            float w = UiFont::width(text, UiFont::MEDIUM, size);
            bool hovered = !blocked && NewUi::hit(x - 2.0f, y, w + 4.0f, h);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            uint32_t ink = UiRender::mix(color, UiTheme::accentText(), lit);
            UiRender::textY(text, x, y, h, ink, UiFont::MEDIUM, size);
            if (hovered) {
                UiRender::hLine(x, x + w, y + h * 0.5f + size * 0.55f, ink);
                UiCursor::hand();
                if (NewUi::click(x - 2.0f, y, w + 4.0f, h)) {
                    UiSound::request(UiSound::TICK);
                    return true;
                }
            }
            return false;
        }

        float stat(int glyph, const std::string& text, float x, float y, float h, float limit) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (x + 14.0f + w > limit) {
                return 0.0f;
            }
            UiWidgets::icon(Ui::glyph(glyph), x + 5.0f, y + h * 0.5f, UiTheme::textFaint(), 9.0f);
            UiRender::textY(text, x + 14.0f, y, h, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return 14.0f + w + 14.0f;
        }

        const Projects::Member* lead(const Projects::Detail& d, const Route& route) {
            for (const Projects::Member& member : d.members) {
                if (member.organization) {
                    return &member;
                }
            }
            for (const Projects::Member& member : d.members) {
                if (member.name == route.hit.author) {
                    return &member;
                }
            }
            return d.members.empty() ? nullptr : &d.members.front();
        }

        void openMember(const Projects::Member& member, Source source) {
            Authors::Kind kind = source == Source::CurseForge ? Authors::Kind::CurseForge
                    : member.organization                     ? Authors::Kind::ModrinthOrganization
                                                              : Authors::Kind::ModrinthUser;
            std::string id = source == Source::Modrinth && !member.organization ? member.name : member.id;
            DownloadsParts::openAuthor(kind, id, member.name, member.avatar, source);
        }

        std::vector<const Addons::Version*> filtered(const Addons::Versions& versions, const State& st) {
            std::vector<const Addons::Version*> out;
            for (const Addons::Version* version : ProjectBrowser::matching(
                         versions.list, DownloadsParts::versionFilter(), DownloadsParts::loaderFilter())) {
                if (st.typeFilter.empty() || version->type == st.typeFilter) {
                    out.push_back(version);
                }
            }
            return out;
        }

        const Addons::Version* best(const std::vector<const Addons::Version*>& matching) {
            for (const Addons::Version* version : matching) {
                if (version->type == "release") {
                    return version;
                }
            }
            return matching.empty() ? nullptr : matching.front();
        }

        const Mcmod::Ref* mcmodRef(const Route& route, const Projects::Detail& d) {
            if (route.kind != Kind::Mod && route.kind != Kind::Modpack) {
                return nullptr;
            }
            std::string slug = !d.slug.empty() ? d.slug : route.hit.slug;
            if (slug.empty()) {
                return nullptr;
            }
            const Mcmod::Lookup& lookup = Mcmod::lookup(route.source, slug);
            return lookup.status == Status::Ready ? &lookup.ref : nullptr;
        }

        bool wikiButton(float x, float y, bool covered) {
            const float size = 28.0f;
            bool hovered = !covered && NewUi::hit(x, y, size, size);
            float lit = UiState::motion("downloads#wiki#icon", hovered ? 1.0f : 0.0f, 18.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, size, size, 6.0f, UiTheme::overlay(Ui::round(16 * lit)));
            }
            const float mark = 14.0f;
            float mx = x + (size - mark) * 0.5f;
            float my = y + (size - mark) * 0.5f;
            SourceMarks::mcmod(mx, my, mark, UiTheme::textSecondary());
            SourceMarks::mcmodColored(mx, my, mark, lit);
            if (hovered) {
                Page::tooltip("在 MC 百科上查看", x, y, size, size);
                UiCursor::hand();
            }
            if (!covered && NewUi::click(x, y, size, size)) {
                UiSound::request(UiSound::PAGE);
                return true;
            }
            return false;
        }

        bool mcmodPending(const Route& route, const Projects::Detail& d) {
            if (route.kind != Kind::Mod && route.kind != Kind::Modpack) {
                return false;
            }
            std::string slug = !d.slug.empty() ? d.slug : route.hit.slug;
            if (slug.empty()) {
                return false;
            }
            const Mcmod::Lookup& lookup = Mcmod::lookup(route.source, slug);
            if (lookup.status == Status::Loading) {
                return true;
            }
            return lookup.status == Status::Ready && Mcmod::intro(lookup.ref).status == Status::Loading;
        }

        void header(Route& route, const Projects::Detail& d, const Mcmod::Ref* mcmod, const Addons::Version* install,
                    bool covered) {
            float x0 = mainX();
            float right = Page::right();
            if (Page::iconButton("downloads#back", 0xf053, "返回", x0 - 4.0f,
                                 Page::top() - 2.0f + (ICON - 28.0f) * 0.5f, covered)) {
                DownloadsParts::back();
                return;
            }
            float iconX = x0 + 30.0f;
            DownloadsParts::projectIcon(!d.iconUrl.empty() ? d.iconUrl : route.hit.iconUrl, iconX, Page::top() - 2.0f,
                                        ICON, route.kind);
            float textX = iconX + ICON + 12.0f;
            float linkX = right - 28.0f;
            std::string site = std::string("在 ") + Addons::sourceName(route.source) + " 上查看";
            std::string pageUrl = !d.pageUrl.empty() ? d.pageUrl : route.hit.pageUrl;
            if (Page::iconButton("downloads#open", 0xf35d, site, linkX, Page::top() - 6.0f, covered) &&
                !pageUrl.empty()) {
                Platform::openUrl(pageUrl);
            }
            float titleRight = linkX - 6.0f;
            if (mcmod != nullptr && !mcmod->url.empty()) {
                float wikiX = linkX - 30.0f;
                if (wikiButton(wikiX, Page::top() - 6.0f, covered)) {
                    Platform::openUrl(mcmod->url);
                }
                titleRight = wikiX - 6.0f;
            }
            const std::string& title = route.hit.title;
            std::string heading =
                    fitEnd(DownloadsParts::displayTitle(title, route.kind), titleRight - textX, Page::TITLE_SIZE);
            UiRender::textY(heading, textX, Page::top() - 4.0f, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            float otherX = textX + UiFont::width(heading, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            std::string other = DownloadsParts::otherTitle(title, route.kind);
            if (!other.empty() && otherX < titleRight) {
                UiRender::textY(fitEnd(other, titleRight - otherX, UiWidgets::TEXT_CONTROL), otherX, Page::top() - 4.0f,
                                24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            float actionRight = right;
            float actionW = 0.0f;
            if (install != nullptr) {
                actionW = 96.0f;
                DownloadsParts::versionAction(*install, route, actionRight, Page::top() + 26.0f, 26.0f, true, covered);
            }
            float lineRight = right - actionW - 12.0f;
            std::string summary = !d.summary.empty() ? d.summary : route.hit.description;
            UiRender::textY(fitEnd(summary, lineRight - textX, UiWidgets::TEXT_SMALL), textX, Page::top() + 20.0f,
                            16.0f, UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float statY = Page::top() + 38.0f;
            float sx = textX;
            const Projects::Member* owner = lead(d, route);
            std::string authorName = owner != nullptr ? owner->name : route.hit.author;
            if (!authorName.empty()) {
                if (owner != nullptr && !owner->avatar.empty()) {
                    DownloadsParts::webImage(owner->avatar, sx, statY + 1.0f, 14.0f, 14.0f, 4.0f, true);
                    sx += 19.0f;
                } else {
                    UiWidgets::icon(Ui::glyph(0xf007), sx + 5.0f, statY + 8.0f, UiTheme::textFaint(), 9.0f);
                    sx += 14.0f;
                }
                std::string name = fitEnd(authorName, std::min(150.0f, lineRight - sx), UiWidgets::TEXT_SMALL);
                bool usable = owner != nullptr || !route.hit.authorId.empty();
                if (usable &&
                    textLink("downloads#head#author", name, sx, statY, 16.0f, UiWidgets::TEXT_SMALL,
                             UiTheme::textSecondary(), covered)) {
                    if (owner != nullptr) {
                        openMember(*owner, route.source);
                    } else {
                        DownloadsParts::openAuthor(route.source == Source::Modrinth ? Authors::Kind::ModrinthUser
                                                                                    : Authors::Kind::CurseForge,
                                                   route.hit.authorId, route.hit.author, std::string(), route.source);
                    }
                }
                if (!usable) {
                    UiRender::textY(name, sx, statY, 16.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                sx += UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 14.0f;
            }
            long long downloads = d.downloads > 0 ? d.downloads : route.hit.downloads;
            sx += stat(0xf019, Addons::count(downloads), sx, statY, 16.0f, lineRight);
            long long followers = d.followers > 0 ? d.followers : route.hit.follows;
            if (followers > 0) {
                sx += stat(route.source == Source::Modrinth ? 0xf004 : 0xf164, Addons::count(followers), sx, statY,
                           16.0f, lineRight);
            }
            if (!d.updated.empty()) {
                sx += stat(0xf017, DownloadsParts::relativeTime(d.updated) + "更新", sx, statY, 16.0f, lineRight);
            }
        }

        struct Chip {
            std::string text;
            uint32_t image = 0;
            uint32_t fill = 0;
            uint32_t ink = 0;
            int glyph = 0;
        };

        float chips(const std::vector<Chip>& list, float x, float y, float w, bool paint) {
            float cx = 0.0f;
            float cy = 0.0f;
            for (const Chip& chip : list) {
                float textW = UiFont::width(chip.text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float chipW = std::min(w, textW + 16.0f + (chip.image != 0 || chip.glyph != 0 ? 17.0f : 0.0f));
                if (cx > 0.0f && cx + chipW > w) {
                    cx = 0.0f;
                    cy += CHIP_H + 6.0f;
                }
                if (paint) {
                    UiRender::roundRect(x + cx, y + cy, chipW, CHIP_H, 6.0f,
                                        chip.fill != 0 ? chip.fill : UiTheme::surfaceRaised());
                    float tx = x + cx + 7.0f;
                    if (chip.image != 0) {
                        UiRender::texture(chip.image, tx, y + cy + (CHIP_H - 14.0f) * 0.5f, 14.0f, 14.0f);
                        tx += 17.0f;
                    } else if (chip.glyph != 0) {
                        UiWidgets::icon(Ui::glyph(chip.glyph), tx + 6.0f, y + cy + CHIP_H * 0.5f, UiTheme::textMuted(),
                                        9.0f);
                        tx += 17.0f;
                    }
                    UiRender::textY(fitEnd(chip.text, x + cx + chipW - 6.0f - tx, UiWidgets::TEXT_SMALL), tx, y + cy,
                                    CHIP_H, chip.ink != 0 ? chip.ink : UiTheme::textSecondary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                cx += chipW + 6.0f;
            }
            return list.empty() ? 0.0f : cy + CHIP_H;
        }

        float person(const std::string& id, const std::string& avatar, const std::string& name, const std::string& role,
                     float x, float y, float w, bool paint, bool blocked) {
            if (!paint) {
                return PERSON_H;
            }
            bool hovered = !blocked && NewUi::hit(x - 6.0f, y, w + 6.0f, PERSON_H);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x - 6.0f, y, w + 6.0f, PERSON_H, 8.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            if (!avatar.empty()) {
                DownloadsParts::webImage(avatar, x, y + 4.0f, 24.0f, 24.0f, 7.0f, true);
            } else {
                UiRender::roundRect(x, y + 4.0f, 24.0f, 24.0f, 7.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(0xf007), x + 12.0f, y + 16.0f, UiTheme::textFaint(), 10.0f);
            }
            float tx = x + 32.0f;
            float roleW = role.empty() ? 0.0f : UiFont::width(role, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float nameRoom = w - 32.0f - (roleW > 0.0f ? std::min(roleW, w * 0.4f) + 8.0f : 0.0f);
            std::string shown = fitEnd(name, nameRoom, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, tx, y, PERSON_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (roleW > 0.0f) {
                float rx = tx + UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                UiRender::textY(fitEnd(role, x + w - rx, UiWidgets::TEXT_SMALL), rx, y, PERSON_H, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (hovered) {
                UiCursor::hand();
                if (NewUi::click(x - 6.0f, y, w + 6.0f, PERSON_H)) {
                    UiSound::request(UiSound::PAGE);
                    return -PERSON_H;
                }
            }
            return PERSON_H;
        }

        float linkRow(const std::string& id, int glyph, const std::string& label, float x, float y, float w, bool paint,
                      bool blocked, bool& clicked) {
            clicked = false;
            if (!paint) {
                return LINK_H;
            }
            if (glyph == 0) {
                SourceMarks::mcmod(x + 1.0f, y + (LINK_H - 12.0f) * 0.5f, 12.0f, UiTheme::textMuted());
            } else {
                UiWidgets::icon(Ui::glyph(glyph), x + 7.0f, y + LINK_H * 0.5f, UiTheme::textMuted(), 10.0f);
            }
            clicked = textLink(id, fitEnd(label, w - 22.0f, UiWidgets::TEXT_CONTROL), x + 20.0f, y, LINK_H,
                               UiWidgets::TEXT_CONTROL, UiTheme::textSecondary(), blocked);
            return LINK_H;
        }

        float detailRow(const std::string& label, const std::string& value, float x, float y, float w, bool paint) {
            if (paint) {
                UiRender::textY(label, x, y, LINK_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float valueW = UiFont::width(value, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float room = w - 70.0f;
                std::string shown = fitEnd(value, room, UiWidgets::TEXT_SMALL);
                UiRender::textY(shown, x + w - std::min(valueW, room), y, LINK_H, UiTheme::textSecondary(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            return LINK_H;
        }

        float sidebar(Route& route, const Projects::Detail& d, const Mcmod::Ref* mcmod, float x, float y, float w,
                      bool blocked, bool paint) {
            float cy = 0.0f;
            std::string base = "downloads#side#" + keyOf(route) + "#";
            auto title = [&](const std::string& text) {
                if (cy > 0.0f) {
                    cy += 14.0f;
                }
                if (paint) {
                    DownloadsParts::sectionTitle(text, x, y + cy, w);
                }
                cy += 28.0f;
            };
            std::vector<Chip> loaders;
            for (const std::string& loader : d.loaders) {
                Chip chip;
                chip.text = Modrinth::label(loader);
                chip.image = DownloadsParts::loaderImage(loader);
                loaders.push_back(chip);
            }
            auto sub = [&](const std::string& text) {
                if (paint) {
                    UiRender::textY(text, x, y + cy, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                cy += 22.0f;
            };
            std::vector<Chip> environment;
            auto side = [&](const std::string& value, const char* name, int glyph) {
                if (value == "required" || value == "optional") {
                    Chip chip;
                    chip.text = std::string(name) + (value == "optional" ? "（可选）" : "");
                    chip.glyph = glyph;
                    environment.push_back(chip);
                }
            };
            side(d.client, "客户端", 0xf108);
            side(d.server, "服务端", 0xf233);
            if (!d.gameVersions.empty() || !loaders.empty() || !environment.empty()) {
                title("兼容性");
                if (!d.gameVersions.empty()) {
                    sub("Minecraft");
                    std::vector<std::string> ranges = DownloadsParts::gameRanges(d.gameVersions);
                    std::vector<Chip> versionChips;
                    for (size_t i = 0; i < ranges.size() && i < 8; i++) {
                        versionChips.push_back(Chip{ranges[i]});
                    }
                    if (ranges.size() > 8) {
                        versionChips.push_back(Chip{"其余 " + std::to_string(ranges.size() - 8) + " 段"});
                    }
                    cy += chips(versionChips, x, y + cy, w, paint) + 10.0f;
                }
                if (!loaders.empty()) {
                    sub("加载器");
                    cy += chips(loaders, x, y + cy, w, paint) + 10.0f;
                }
                if (!environment.empty()) {
                    sub("运行环境");
                    cy += chips(environment, x, y + cy, w, paint) + 10.0f;
                }
                cy -= 10.0f;
            }
            if (!d.links.empty() || mcmod != nullptr) {
                title("链接");
                for (size_t i = 0; i < d.links.size(); i++) {
                    bool clicked = false;
                    cy += linkRow(base + "link#" + std::to_string(i), d.links[i].glyph, d.links[i].label, x, y + cy, w,
                                  paint, blocked, clicked);
                    if (clicked) {
                        Platform::openUrl(d.links[i].url);
                    }
                }
                if (mcmod != nullptr && !mcmod->url.empty()) {
                    bool clicked = false;
                    cy += linkRow(base + "link#mcmod", 0, "MC百科", x, y + cy, w, paint, blocked, clicked);
                    if (clicked) {
                        Platform::openUrl(mcmod->url);
                    }
                }
            }
            if (!d.members.empty() || !d.membersLoaded) {
                title("创作者");
                if (!d.membersLoaded && d.members.empty()) {
                    if (paint) {
                        Page::spinner(x + 10.0f, y + cy + 12.0f, 6.0f, UiTheme::accent());
                    }
                    cy += 24.0f;
                }
                for (size_t i = 0; i < d.members.size(); i++) {
                    const Projects::Member& member = d.members[i];
                    std::string role = member.role;
                    if (role == "Owner") {
                        role = "所有者";
                    } else if (role == "Member") {
                        role = "成员";
                    }
                    float h = person(base + "member#" + std::to_string(i), member.avatar, member.name, role, x, y + cy,
                                     w, paint, blocked);
                    if (h < 0.0f) {
                        openMember(member, route.source);
                        h = -h;
                    }
                    cy += h;
                }
            }
            if (mcmod != nullptr) {
                const Mcmod::Mod& wiki = Mcmod::mod(*mcmod);
                if (wiki.status == Status::Ready) {
                    title("MC百科");
                    std::vector<Chip> tags;
                    if (!wiki.state.empty()) {
                        Chip state;
                        state.text = wiki.state;
                        state.fill = UiTheme::accentSoft(40);
                        state.ink = UiTheme::accentText();
                        tags.push_back(state);
                    }
                    for (const std::string& text : wiki.tags) {
                        tags.push_back(Chip{text});
                    }
                    if (!tags.empty()) {
                        cy += chips(tags, x, y + cy, w, paint) + 8.0f;
                    }
                    const Mcmod::Relations& relations = Mcmod::relations(*mcmod);
                    for (const char* type : {"requires", "integrates"}) {
                        std::vector<const Mcmod::Relation*> list;
                        for (const Mcmod::Relation& relation : relations.items) {
                            if (relation.type == type) {
                                list.push_back(&relation);
                            }
                        }
                        if (list.empty()) {
                            continue;
                        }
                        if (paint) {
                            UiRender::textY(std::string(type == std::string("requires") ? "前置" : "联动") + "  " +
                                                    std::to_string(list.size()),
                                            x, y + cy, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                            UiWidgets::TEXT_SMALL);
                        }
                        cy += 20.0f;
                        size_t shown = std::min<size_t>(list.size(), 6);
                        for (size_t i = 0; i < shown; i++) {
                            bool clicked = false;
                            cy += linkRow(base + type + std::to_string(i), 0xf12e, Mcmod::displayName(list[i]->mod), x,
                                          y + cy, w, paint, blocked, clicked);
                            if (clicked) {
                                DownloadsParts::openMcmod(list[i]->mod);
                            }
                        }
                        cy += 4.0f;
                    }
                    for (size_t i = 0; i < wiki.authors.size() && i < 8; i++) {
                        const Mcmod::Author& author = wiki.authors[i];
                        float h = person(base + "wiki#" + std::to_string(i), author.avatar, author.name, author.roles,
                                         x, y + cy, w, paint, blocked);
                        if (h < 0.0f) {
                            DownloadsParts::openAuthor(Authors::Kind::Mcmod, std::to_string(author.id), author.name,
                                                       author.avatar, route.source);
                            h = -h;
                        }
                        cy += h;
                    }
                }
            }
            title("详情");
            if (!d.license.empty()) {
                if (paint && !d.licenseUrl.empty()) {
                    UiRender::textY("许可证", x, y + cy, LINK_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    std::string shown = fitEnd(d.license, w - 70.0f, UiWidgets::TEXT_SMALL);
                    float lw = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    if (textLink(base + "license", shown, x + w - lw, y + cy, LINK_H, UiWidgets::TEXT_SMALL,
                                 UiTheme::textSecondary(), blocked)) {
                        Platform::openUrl(d.licenseUrl);
                    }
                    cy += LINK_H;
                } else {
                    cy += detailRow("许可证", d.license, x, y + cy, w, paint);
                }
            }
            if (!d.published.empty()) {
                cy += detailRow("发布于", DownloadsParts::shortDate(d.published), x, y + cy, w, paint);
            }
            if (!d.updated.empty()) {
                cy += detailRow("更新于", DownloadsParts::shortDate(d.updated), x, y + cy, w, paint);
            }
            if (!d.id.empty()) {
                if (paint) {
                    UiRender::textY("项目 ID", x, y + cy, LINK_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    float iw = UiFont::width(d.id, UiFont::CODE, UiWidgets::TEXT_SMALL);
                    bool hovered = !blocked && NewUi::hit(x + w - iw - 4.0f, y + cy, iw + 4.0f, LINK_H);
                    UiRender::textY(d.id, x + w - iw, y + cy, LINK_H,
                                    hovered ? UiTheme::accentText() : UiTheme::textSecondary(), UiFont::CODE,
                                    UiWidgets::TEXT_SMALL);
                    if (hovered) {
                        UiCursor::hand();
                        Page::tooltip("复制", x + w - iw, y + cy, iw, LINK_H);
                        if (NewUi::click(x + w - iw - 4.0f, y + cy, iw + 4.0f, LINK_H)) {
                            UiInput::setClipboard(d.id);
                            Notifications::add(Notifications::INFO, "已复制项目 ID", 1500);
                        }
                    }
                }
                cy += LINK_H;
            }
            return cy;
        }

        void retry(const std::string& id, const std::string& text, float top, float bottom, bool covered,
                   const std::function<void()>& again) {
            DownloadsParts::message(text, UiTheme::danger(), top, bottom, false);
            std::string label = "重试";
            float w = Page::buttonWidth(label);
            if (Page::button(id, label, mainX() + (mainW() - w) * 0.5f, (top + bottom) * 0.5f + 20.0f, w, false,
                             covered)) {
                again();
            }
        }

        void description(Route& route, const Projects::Detail& d, State& st, const Mcmod::Ref* mcmod, float top,
                         float bottom, bool covered) {
            if (d.status == Status::Failed && !d.bodyLoaded) {
                retry("downloads#detail#retry", "获取项目详情失败：" + d.error, top, bottom, covered,
                      [&] { Projects::refresh(route.source, route.hit.id); });
                return;
            }
            if (!d.bodyLoaded || mcmodPending(route, d)) {
                DownloadsParts::message("正在获取项目详情", UiTheme::textMuted(), top, bottom, true);
                return;
            }
            float x0 = mainX();
            float w = mainW();
            bool wide = w >= WIDE;
            float contentW = wide ? w - SIDEBAR_W - 28.0f : w;
            bool useWiki = st.bodySource == 1 && mcmod != nullptr;
            std::string text = d.body;
            bool html = d.html;
            bool waiting = false;
            if (useWiki) {
                const Mcmod::Text& intro = Mcmod::intro(*mcmod);
                waiting = intro.status == Status::Loading;
                text = intro.content;
                html = intro.html;
            }
            float bodyH = 0.0f;
            MarkdownView* view = nullptr;
            if (!text.empty()) {
                view = &doc(keyOf(route) + (useWiki ? "#wiki" : "#body"), text, html);
                if (!view->onAnchor) {
                    std::string scrollId = "downloads#about#" + keyOf(route);
                    view->onAnchor = [scrollId](float y) { Page::scrollTo(scrollId, std::max(0.0f, y - 8.0f)); };
                }
                bodyH = view->measure(contentW);
            } else {
                bodyH = 40.0f;
            }
            float sideW = wide ? SIDEBAR_W : contentW;
            float sideH = sidebar(route, d, mcmod, 0.0f, 0.0f, sideW, true, false);
            float contentH = wide ? std::max(bodyH, sideH) : bodyH + 28.0f + sideH;
            Page::Scroll scroll("downloads#about#" + keyOf(route), top, bottom, contentH + 20.0f, x0, w);
            bool blocked = scroll.begin() || covered;
            float y = scroll.top - scroll.offset;
            if (view != nullptr) {
                view->draw(x0, y, scroll.top, scroll.bottom, blocked);
            } else {
                std::string empty = waiting ? "正在获取 MC百科 介绍" : "暂无介绍";
                if (waiting) {
                    Page::spinner(x0 + 8.0f, y + 20.0f, 7.0f, UiTheme::accent());
                }
                UiRender::textY(empty, x0 + (waiting ? 22.0f : 0.0f), y + 8.0f, 24.0f, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            if (wide) {
                float sx = x0 + contentW + 28.0f;
                UiRender::vLine(sx - 14.0f, y, y + std::max(bodyH, sideH), UiTheme::divider());
                sidebar(route, d, mcmod, sx, y, SIDEBAR_W, blocked, true);
            } else {
                float sy = y + bodyH + 14.0f;
                UiRender::hLine(x0, x0 + contentW, sy, UiTheme::divider());
                sidebar(route, d, mcmod, x0, sy + 14.0f, contentW, blocked, true);
            }
            scroll.end();
        }

        std::shared_ptr<Choice> typeChoice(State& st) {
            State* state = &st;
            return std::make_shared<ModeBinding>(
                    "downloads#typeFilter",
                    [state] {
                        return state->typeFilter.empty()
                                ? std::string(ANY_TYPE)
                                : std::string(DownloadsParts::versionTypeLabel(state->typeFilter));
                    },
                    [state](const std::string& option) {
                        state->typeFilter = option == "正式版" ? "release"
                                : option == "测试版"           ? "beta"
                                : option == "早期版"           ? "alpha"
                                                               : "";
                    },
                    [] { return std::vector<std::string>{ANY_TYPE, "正式版", "测试版", "早期版"}; });
        }

        float filters(Route& route, State& st, float top, bool covered) {
            float x = mainX();
            float right = Page::right();
            bool instance = ProjectBrowser::usesInstance(route.kind);
            bool loaders = ProjectBrowser::hasLoaders(route.source, route.kind);
            if (instance) {
                float instanceW = 150.0f;
                DownloadsParts::instancePicker(x, top, instanceW, covered);
                x += instanceW + DownloadsParts::GAP;
            }
            int cells = loaders ? 3 : 2;
            float cell = (right - x - DownloadsParts::GAP * (cells - 1)) / cells;
            DownloadsParts::select("downloads#versionFilter", DownloadsParts::versionChoice(), x, top, cell, covered);
            x += cell + DownloadsParts::GAP;
            if (loaders) {
                DownloadsParts::select("downloads#loaderFilter", DownloadsParts::loaderChoice(route.source, route.kind),
                                       x, top, cell, covered);
                x += cell + DownloadsParts::GAP;
            }
            DownloadsParts::select("downloads#typeFilter", typeChoice(st), x, top, cell, covered);
            return top + Page::FIELD_H + 12.0f;
        }

        bool versionsReady(Route& route, const Addons::Versions& versions, float top, float bottom, bool covered) {
            if (versions.status == Status::Loading) {
                DownloadsParts::message("正在获取版本列表", UiTheme::textMuted(), top, bottom, true);
                return false;
            }
            if (versions.status == Status::Failed) {
                retry("downloads#versions#retry", "获取版本失败：" + versions.error, top, bottom, covered,
                      [&] { Addons::refreshVersions(route.source, route.hit.id); });
                return false;
            }
            return true;
        }

        struct Columns {
            float nameX = 0.0f;
            float gameX = 0.0f;
            float gameW = 0.0f;
            float downloadsX = 0.0f;
            float downloadsW = 0.0f;
            float dateX = 0.0f;
            float dateW = 0.0f;
            float actionX = 0.0f;
            float right = 0.0f;
        };

        Columns columns(float x, float w) {
            Columns c;
            c.right = x + w - 12.0f;
            c.actionX = c.right - 92.0f;
            c.dateW = 72.0f;
            c.dateX = c.actionX - 16.0f - c.dateW;
            c.downloadsW = w >= 600.0f ? 60.0f : 0.0f;
            c.downloadsX = c.dateX - (c.downloadsW > 0.0f ? c.downloadsW + 24.0f : 0.0f);
            c.gameW = w >= 500.0f ? std::min(150.0f, w * 0.22f) : 0.0f;
            c.gameX = c.downloadsX - (c.gameW > 0.0f ? c.gameW + 20.0f : 0.0f);
            c.nameX = x + 14.0f;
            return c;
        }

        float dependencyRow(const Route& route, const Addons::Dependency& dependency, size_t index, float x, float y,
                            float w, bool paint, bool blocked) {
            const float h = 30.0f;
            if (!paint) {
                return h;
            }
            const Projects::Brief* brief = Projects::brief(route.source, dependency.projectId);
            std::string id = "downloads#dep#" + dependency.projectId + "#" + std::to_string(index);
            bool usable = brief != nullptr;
            bool hovered = usable && !blocked && NewUi::hit(x - 6.0f, y, w + 6.0f, h);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x - 6.0f, y, w + 6.0f, h, 8.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            Kind kind = brief != nullptr ? brief->kind : Kind::Mod;
            DownloadsParts::projectIcon(brief != nullptr ? brief->iconUrl : std::string(), x, y + 5.0f, 20.0f, kind);
            std::string label = dependency.embedded ? "内置"
                    : dependency.incompatible       ? "不兼容"
                    : dependency.required           ? "必需"
                                                    : "可选";
            uint32_t ink = dependency.incompatible ? UiTheme::danger()
                    : dependency.required          ? UiTheme::accentText()
                                                   : UiTheme::textSecondary();
            uint32_t fill = dependency.incompatible ? UiTheme::dangerSoft()
                    : dependency.required           ? UiTheme::accentSoft(40)
                                                    : UiTheme::surfaceRaised();
            float labelW = UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            std::string name =
                    brief != nullptr ? DownloadsParts::displayTitle(brief->title, kind) : dependency.projectId;
            std::string shown = fitEnd(name, w - 30.0f - labelW - 8.0f, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, x + 28.0f, y, h, brief != nullptr ? UiTheme::textPrimary() : UiTheme::textMuted(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            DownloadsParts::tag(label, x + 28.0f + UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f,
                                y + h * 0.5f, x + w, fill, ink);
            if (hovered) {
                UiCursor::hand();
                if (NewUi::click(x - 6.0f, y, w + 6.0f, h)) {
                    UiSound::request(UiSound::PAGE);
                    Addons::Hit hit;
                    hit.id = brief->id;
                    hit.slug = brief->slug;
                    hit.title = brief->title;
                    hit.iconUrl = brief->iconUrl;
                    DownloadsParts::openProject(route.source, brief->kind, hit);
                }
            }
            return h;
        }

        float label(const std::string& text, float x, float y, bool paint) {
            if (paint) {
                UiRender::textY(text, x, y, 22.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            return 22.0f;
        }

        float panel(Route& route, const Addons::Version& version, float x, float y, float w, bool paint, bool blocked) {
            float cy = 8.0f;
            if (paint) {
                float tx = x;
                tx += DownloadsParts::tag(version.number, tx, y + cy + 11.0f, x + w, UiTheme::surfaceRaised(),
                                          UiTheme::textSecondary()) +
                        6.0f;
                std::string date = DownloadsParts::shortDate(version.published);
                tx += DownloadsParts::tag(date, tx, y + cy + 11.0f, x + w, UiTheme::surfaceRaised(),
                                          UiTheme::textSecondary()) +
                        6.0f;
                std::string loaders;
                for (const std::string& loader : version.loaders) {
                    loaders += (loaders.empty() ? "" : "、") + Modrinth::label(loader);
                }
                if (!loaders.empty()) {
                    tx += DownloadsParts::tag(loaders, tx, y + cy + 11.0f, x + w, UiTheme::surfaceRaised(),
                                              UiTheme::textSecondary()) +
                            6.0f;
                }
                std::string games = DownloadsParts::gameRange(version.gameVersions, 3);
                if (!games.empty()) {
                    DownloadsParts::tag(games, tx, y + cy + 11.0f, x + w, UiTheme::surfaceRaised(),
                                        UiTheme::textSecondary());
                }
            }
            cy += 30.0f;
            std::vector<const Addons::Dependency*> dependencies;
            for (const Addons::Dependency& dependency : version.dependencies) {
                if (!dependency.projectId.empty()) {
                    dependencies.push_back(&dependency);
                }
            }
            if (!dependencies.empty()) {
                cy += label("依赖", x, y + cy, paint);
                for (size_t i = 0; i < dependencies.size(); i++) {
                    cy += dependencyRow(route, *dependencies[i], i, x, y + cy, w, paint, blocked);
                }
                cy += 6.0f;
            }
            if (!version.files.empty()) {
                cy += label("文件", x, y + cy, paint);
                for (size_t i = 0; i < version.files.size(); i++) {
                    const Addons::File& file = version.files[i];
                    const float h = 28.0f;
                    if (paint) {
                        std::string id = "downloads#file#" + version.id + "#" + std::to_string(i);
                        bool hovered = !blocked && NewUi::hit(x - 6.0f, y + cy, w + 6.0f, h);
                        float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
                        if (lit > 0.001f) {
                            UiRender::roundRect(x - 6.0f, y + cy, w + 6.0f, h, 8.0f,
                                                UiTheme::overlay(Ui::round(12 * lit)));
                        }
                        UiWidgets::icon(Ui::glyph(0xf15b), x + 7.0f, y + cy + h * 0.5f, UiTheme::textMuted(), 10.0f);
                        std::string size = file.size > 0 ? bytes(file.size) : std::string();
                        float sizeW = UiFont::width(size, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                        bool primary = file.primary && version.files.size() > 1;
                        float primaryW =
                                primary ? UiFont::width("主文件", UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 20.0f : 0.0f;
                        std::string name =
                                fitEnd(file.filename, w - 26.0f - sizeW - primaryW - 30.0f, UiWidgets::TEXT_CONTROL);
                        UiRender::textY(name, x + 20.0f, y + cy, h, UiTheme::textSecondary(), UiFont::MEDIUM,
                                        UiWidgets::TEXT_CONTROL);
                        float after = x + 20.0f + UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                        if (primary) {
                            DownloadsParts::tag("主文件", after, y + cy + h * 0.5f, x + w, UiTheme::accentSoft(40),
                                                UiTheme::accentText());
                        }
                        UiRender::textY(size, x + w - 26.0f - sizeW, y + cy, h, UiTheme::textMuted(), UiFont::MEDIUM,
                                        UiWidgets::TEXT_SMALL);
                        UiWidgets::icon(Ui::glyph(0xf019), x + w - 10.0f, y + cy + h * 0.5f,
                                        hovered ? UiTheme::accentText() : UiTheme::textFaint(), 9.0f);
                        if (hovered) {
                            UiCursor::hand();
                            Page::tooltip("另存为", x + w - 20.0f, y + cy, 20.0f, h);
                            if (NewUi::click(x - 6.0f, y + cy, w + 6.0f, h)) {
                                UiSound::request(UiSound::TICK);
                                if (file.primary || version.files.size() == 1) {
                                    ProjectInstaller::saveAs(version, route.hit.title, route.hit.iconUrl);
                                } else {
                                    Platform::openUrl(file.url);
                                }
                            }
                        }
                    }
                    cy += h;
                }
                cy += 6.0f;
            }
            cy += label("更新日志", x, y + cy, paint);
            const Projects::Changelog& log = Projects::changelog(version);
            if (log.status == Status::Loading) {
                if (paint) {
                    Page::spinner(x + 8.0f, y + cy + 12.0f, 6.0f, UiTheme::accent());
                }
                cy += 26.0f;
            } else if (log.text.empty()) {
                if (paint) {
                    UiRender::textY("没有更新日志", x, y + cy, 22.0f, UiTheme::textFaint(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                cy += 24.0f;
            } else {
                MarkdownView& view = doc("cl#" + version.id, log.text, log.html, 13.0f);
                float h = view.measure(w);
                if (paint) {
                    view.draw(x, y + cy, clipTop, clipBottom, blocked);
                }
                cy += h;
            }
            return cy + 12.0f;
        }

        void versionRow(Route& route, State& st, const Addons::Version& version, const std::string& prefix,
                        const Columns& c, float x, float y, float w, bool blocked) {
            const float h = Page::ROW_H;
            bool open = st.expanded == version.id;
            bool overAction = !blocked && NewUi::hit(c.actionX - 8.0f, y, c.right - c.actionX + 20.0f, h);
            bool hovered = !blocked && !overAction && NewUi::hit(x, y, w, h);
            float lit =
                    UiState::motion("downloads#version#" + version.id + "#hover", hovered || open ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x + 4.0f, y + 3.0f, w - 8.0f, h - 6.0f, 8.0f,
                                    UiTheme::overlay(Ui::round((open ? 8 : 12) * lit)));
            }
            float centre = y + h * 0.5f;
            float nx = c.nameX;
            std::vector<uint32_t> icons;
            for (const std::string& loader : version.loaders) {
                if (uint32_t image = DownloadsParts::loaderImage(loader)) {
                    icons.push_back(image);
                }
            }
            if (!icons.empty()) {
                UiRender::texture(icons.front(), nx, centre - 8.0f, 16.0f, 16.0f);
                if (icons.size() > 1) {
                    float bx = nx + 9.0f;
                    float by = centre + 1.0f;
                    UiRender::roundRect(bx - 1.0f, by - 1.0f, 12.0f, 12.0f, 3.0f, UiTheme::surface());
                    UiRender::texture(icons[1], bx, by, 10.0f, 10.0f);
                }
            }
            nx += 24.0f;
            std::string name = ProjectBrowser::shortName(version, prefix, route.hit.title);
            float nameRight = (c.gameW > 0.0f ? c.gameX : c.downloadsW > 0.0f ? c.downloadsX : c.dateX) - 10.0f;
            std::string typeLabel = DownloadsParts::versionTypeLabel(version.type);
            float typeW = UiFont::width(typeLabel, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 20.0f;
            std::string shown = fitEnd(name, std::max(40.0f, nameRight - nx - typeW), UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, nx, y, h, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (shown != name && hovered) {
                Page::tooltip(name, nx, y + 8.0f, UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL),
                              h - 16.0f);
            }
            DownloadsParts::typeTag(version.type,
                                    nx + UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f, centre,
                                    nameRight);
            if (c.gameW > 0.0f) {
                UiRender::textY(
                        fitEnd(DownloadsParts::gameRange(version.gameVersions, 1), c.gameW, UiWidgets::TEXT_SMALL),
                        c.gameX, y, h, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (c.downloadsW > 0.0f) {
                std::string count = Addons::count(version.downloads);
                float cw = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(count, c.downloadsX + c.downloadsW - cw, y, h, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            std::string date = DownloadsParts::relativeTime(version.published);
            UiRender::textY(fitEnd(date, c.dateW, UiWidgets::TEXT_SMALL), c.dateX, y, h, UiTheme::textMuted(),
                            UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (hovered && NewUi::hit(c.dateX, y, c.dateW, h)) {
                Page::tooltip(DownloadsParts::shortDate(version.published), c.dateX, y + 8.0f, c.dateW, h - 16.0f);
            }
            DownloadsParts::versionAction(version, route, c.right, y, h, false, blocked);
            if (hovered) {
                UiCursor::hand();
                if (NewUi::click(x, y, c.actionX - 8.0f - x, h)) {
                    UiSound::request(UiSound::TICK);
                    st.expanded = open ? std::string() : version.id;
                }
            }
        }

        void versionsTab(Route& route, State& st, const Addons::Versions& versions, float top, float bottom,
                         bool covered) {
            float listTop = filters(route, st, top, covered);
            if (!versionsReady(route, versions, listTop, bottom, covered)) {
                return;
            }
            std::vector<const Addons::Version*> list = filtered(versions, st);
            if (list.empty()) {
                DownloadsParts::message("没有符合条件的版本", UiTheme::textMuted(), listTop, bottom, false);
                return;
            }
            float x = mainX();
            float w = mainW();
            Columns c = columns(x, w);
            UiRender::textY("名称", c.nameX, listTop, TABLE_HEAD, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            if (c.gameW > 0.0f) {
                UiRender::textY("游戏版本", c.gameX, listTop, TABLE_HEAD, UiTheme::textFaint(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            if (c.downloadsW > 0.0f) {
                float lw = UiFont::width("下载", UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY("下载", c.downloadsX + c.downloadsW - lw, listTop, TABLE_HEAD, UiTheme::textFaint(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            UiRender::textY("发布", c.dateX, listTop, TABLE_HEAD, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float scrollTop = listTop + TABLE_HEAD;
            std::string prefix = ProjectBrowser::sharedPrefix(versions.list);
            float panelW = w - 28.0f - 24.0f;
            float panelH = 0.0f;
            for (const Addons::Version* version : list) {
                if (version->id == st.expanded) {
                    clipTop = scrollTop;
                    clipBottom = bottom;
                    panelH = panel(route, *version, 0.0f, 0.0f, panelW, false, true);
                }
            }
            float contentH = list.size() * Page::ROW_H + panelH;
            Page::Scroll scroll("downloads#versions#" + keyOf(route), scrollTop, bottom, contentH + 4.0f, x, w);
            bool blocked = scroll.begin() || covered;
            clipTop = scroll.top;
            clipBottom = scroll.bottom;
            float y = scroll.top - scroll.offset;
            Page::card(x, y, w, contentH);
            for (size_t i = 0; i < list.size(); i++) {
                const Addons::Version& version = *list[i];
                bool open = version.id == st.expanded;
                float h = Page::ROW_H + (open ? panelH : 0.0f);
                if (i > 0) {
                    UiRender::hLine(x + 14.0f, x + w - 14.0f, y, UiTheme::rowDivider(UiTheme::outlineSoft()));
                }
                if (y + h >= scroll.top && y <= scroll.bottom) {
                    versionRow(route, st, version, prefix, c, x, y, w, blocked);
                    if (open) {
                        panel(route, version, x + 38.0f, y + Page::ROW_H, panelW, true, blocked);
                    }
                }
                y += h;
            }
            scroll.end();
        }

        void changelogTab(Route& route, State& st, const Addons::Versions& versions, float top, float bottom,
                          bool covered) {
            float listTop = filters(route, st, top, covered);
            if (!versionsReady(route, versions, listTop, bottom, covered)) {
                return;
            }
            std::vector<const Addons::Version*> list = filtered(versions, st);
            if (list.empty()) {
                DownloadsParts::message("没有符合条件的版本", UiTheme::textMuted(), listTop, bottom, false);
                return;
            }
            float x = mainX();
            float w = mainW();
            float bodyX = x + 26.0f;
            float bodyW = w - 26.0f;
            size_t shown = std::min(list.size(), (size_t) st.changelogShown);
            float offset = Page::scrollOffset("downloads#changelog#" + keyOf(route));
            std::vector<float> heights(shown, 0.0f);
            float contentH = 0.0f;
            for (size_t i = 0; i < shown; i++) {
                const Addons::Version& version = *list[i];
                float h = 30.0f;
                bool near = contentH - offset < (bottom - listTop) + 800.0f && contentH + 2000.0f > offset;
                if (version.source == Source::Modrinth || near) {
                    const Projects::Changelog& log = Projects::changelog(version);
                    if (log.status == Status::Ready && !log.text.empty()) {
                        h += doc("cl#" + version.id, log.text, log.html, 13.0f).measure(bodyW);
                    } else {
                        h += 24.0f;
                    }
                } else {
                    h += 24.0f;
                }
                heights[i] = h + 18.0f;
                contentH += heights[i];
            }
            bool more = list.size() > shown;
            Page::Scroll scroll("downloads#changelog#" + keyOf(route), listTop, bottom,
                                contentH + (more ? 44.0f : 8.0f), x, mainW());
            bool blocked = scroll.begin() || covered;
            float y = scroll.top - scroll.offset;
            float railTop = y + 15.0f;
            float railBottom = y + contentH - (shown > 0 ? heights.back() : 0.0f) + 15.0f;
            UiRender::vLine(x + 7.0f, railTop, railBottom, UiTheme::divider());
            for (size_t i = 0; i < shown; i++) {
                const Addons::Version& version = *list[i];
                float h = heights[i];
                if (y + h >= scroll.top && y <= scroll.bottom) {
                    UiRender::circle(x + 7.5f, y + 15.0f, 4.5f, i == 0 ? UiTheme::accent() : UiTheme::outline());
                    std::string date = DownloadsParts::relativeTime(version.published);
                    float dateW = UiFont::width(date, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    UiRender::textY(date, x + w - dateW, y, 30.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    std::string typeLabel = DownloadsParts::versionTypeLabel(version.type);
                    float typeW = UiFont::width(typeLabel, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 20.0f;
                    std::string name = fitEnd(version.name.empty() ? version.number : version.name,
                                              w - 26.0f - dateW - typeW - 12.0f, UiWidgets::TEXT_BODY);
                    UiRender::textY(name, bodyX, y, 30.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_BODY);
                    DownloadsParts::typeTag(version.type,
                                            bodyX + UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 8.0f,
                                            y + 15.0f, x + w - dateW - 8.0f);
                    const Projects::Changelog& log = Projects::changelog(version);
                    if (log.status == Status::Loading) {
                        Page::spinner(bodyX + 7.0f, y + 42.0f, 6.0f, UiTheme::accent());
                    } else if (log.text.empty()) {
                        UiRender::textY("没有更新日志", bodyX, y + 30.0f, 22.0f, UiTheme::textFaint(), UiFont::MEDIUM,
                                        UiWidgets::TEXT_SMALL);
                    } else {
                        doc("cl#" + version.id, log.text, log.html, 13.0f)
                                .draw(bodyX, y + 30.0f, scroll.top, scroll.bottom, blocked);
                    }
                }
                y += h;
            }
            if (more) {
                std::string text = "显示更多";
                float bw = Page::buttonWidth(text);
                if (Page::button("downloads#changelog#more", text, x + (w - bw) * 0.5f, y + 8.0f, bw, false, blocked)) {
                    st.changelogShown += CHANGELOG_STEP;
                }
            }
            scroll.end();
        }

        void galleryTab(Route& route, const Projects::Detail& d, float top, float bottom, bool covered) {
            if (!d.bodyLoaded) {
                DownloadsParts::message("正在获取图库", UiTheme::textMuted(), top, bottom, true);
                return;
            }
            float x = mainX();
            float w = mainW();
            int cols = w >= 600.0f ? 3 : 2;
            float gap = 12.0f;
            float cellW = (w - gap * (cols - 1)) / cols;
            float imageH = std::round(cellW * 9.0f / 16.0f);
            bool titled = std::any_of(d.gallery.begin(), d.gallery.end(),
                                      [](const Projects::Image& image) { return !image.title.empty(); });
            float cellH = imageH + (titled ? 28.0f : 0.0f) + gap;
            int rows = ((int) d.gallery.size() + cols - 1) / cols;
            Page::Scroll scroll("downloads#gallery#" + keyOf(route), top, bottom, rows * cellH, x, mainW());
            bool blocked = scroll.begin() || covered;
            float y0 = scroll.top - scroll.offset;
            for (size_t i = 0; i < d.gallery.size(); i++) {
                const Projects::Image& image = d.gallery[i];
                float cx = x + (i % cols) * (cellW + gap);
                float cy = y0 + (i / cols) * cellH;
                if (cy > scroll.bottom || cy + cellH < scroll.top) {
                    continue;
                }
                std::string id = "downloads#shot#" + std::to_string(i);
                bool hovered = !blocked && NewUi::hit(cx, cy, cellW, imageH);
                DownloadsParts::webImage(image.thumb, cx, cy, cellW, imageH, 10.0f, true);
                float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
                if (lit > 0.001f) {
                    UiRender::roundRect(cx, cy, cellW, imageH, 10.0f, UiTheme::overlay(Ui::round(18 * lit)));
                    UiRender::outline(cx, cy, cellW, imageH, 10.0f,
                                      UiRender::mix(UiTheme::clear(), UiTheme::accent(), lit));
                }
                if (titled && !image.title.empty()) {
                    UiRender::textY(fitEnd(image.title, cellW, UiWidgets::TEXT_CONTROL), cx, cy + imageH + 4.0f, 20.0f,
                                    UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                }
                if (hovered) {
                    UiCursor::hand();
                    if (NewUi::click(cx, cy, cellW, imageH)) {
                        UiSound::request(UiSound::PAGE);
                        std::vector<ScreenshotViewer::Picture> pictures;
                        for (const Projects::Image& each : d.gallery) {
                            pictures.push_back({each.thumb, each.full, each.title, each.description});
                        }
                        ScreenshotViewer::openWeb(std::move(pictures), (int) i);
                    }
                }
            }
            scroll.end();
        }

        float bodySwitch(Source source, State& st, const Mcmod::Ref* mcmod, float y, bool covered) {
            if (mcmod == nullptr) {
                return 0.0f;
            }
            const Mcmod::Text& intro = Mcmod::intro(*mcmod);
            if (intro.status != Status::Ready || intro.content.empty()) {
                return 0.0f;
            }
            if (st.bodySource < 0) {
                st.bodySource = ModNames::original() ? 0 : 1;
            }
            int mark = source == Source::CurseForge ? DownloadsParts::MARK_CURSEFORGE : DownloadsParts::MARK_MODRINTH;
            std::vector<DownloadsParts::Segment> items = {{0, Addons::sourceName(source), 0, 0, mark},
                                                          {1, "MC百科", 0, 0, DownloadsParts::MARK_MCMOD}};
            float w = DownloadsParts::segmentsWidth(items);
            bool narrow = mainW() < 560.0f;
            if (narrow) {
                w = 40.0f + DownloadsParts::segmentsWidth({items[(size_t) std::clamp(st.bodySource, 0, 1)]});
            }
            st.bodySource = DownloadsParts::segments("body", st.bodySource, items, Page::right() - w, y, covered,
                                                     narrow ? 1.0f : 0.0f);
            return w;
        }
    }

    void draw(Route& route, bool covered) {
        frame++;
        if (frame % 120 == 0) {
            prune();
        }
        const Projects::Detail& d = Projects::detail(route.source, route.hit.id);
        if (d.status == Status::Ready) {
            if (!d.title.empty()) {
                route.hit.title = d.title;
            }
            if (route.hit.slug.empty() || route.hit.slug == route.hit.id) {
                route.hit.slug = d.slug;
            }
            if (!d.iconUrl.empty()) {
                route.hit.iconUrl = d.iconUrl;
            }
            if (!d.pageUrl.empty()) {
                route.hit.pageUrl = d.pageUrl;
            }
            if (route.kind != Kind::Datapack && route.kind != Kind::World) {
                route.kind = d.kind;
            }
        }
        State& st = states[keyOf(route)];
        const Mcmod::Ref* mcmod = mcmodRef(route, d);
        const Addons::Versions& versions = Addons::versions(route.source, route.hit.id);
        std::vector<const Addons::Version*> matching;
        if (versions.status == Status::Ready) {
            matching = ProjectBrowser::matching(versions.list, DownloadsParts::versionFilter(),
                                                DownloadsParts::loaderFilter());
        }
        header(route, d, mcmod, versions.status == Status::Ready ? best(matching) : nullptr, covered);
        std::vector<DownloadsParts::Segment> items = {
                {DESCRIPTION, "介绍", 0xf05a, 0},
                {VERSIONS, "版本", 0xf019, versions.status == Status::Ready ? versions.list.size() : 0},
                {CHANGELOG, "更新日志", 0xf1da, 0},
        };
        if (!d.gallery.empty()) {
            items.push_back({GALLERY, "图库", 0xf03e, d.gallery.size()});
        }
        if (route.kind == Kind::Modpack) {
            items.push_back({CONTENTS, "模组", 0xf0e8, 0});
        }
        bool related = mcmod != nullptr && !Mcmod::relations(*mcmod).items.empty();
        if (route.kind == Kind::Mod &&
            (versions.status != Status::Ready || related ||
             std::any_of(versions.list.begin(), versions.list.end(),
                         [](const Addons::Version& version) { return !version.dependencies.empty(); }))) {
            items.push_back({DEPENDENCIES, "依赖", 0xf0e8, 0});
        }
        bool known = std::any_of(items.begin(), items.end(), [&](const auto& item) { return item.id == route.tab; });
        if (!known) {
            route.tab = DESCRIPTION;
        }
        float tabsY = Page::top() + TABS_Y;
        float switchW = route.tab == DESCRIPTION ? bodySwitch(route.source, st, mcmod, tabsY, covered) : 0.0f;
        route.tab = DownloadsParts::segments("project", route.tab, items, mainX(), tabsY, covered,
                                             mainW() - (switchW > 0.0f ? switchW + 12.0f : 0.0f));
        float top = tabsY + DownloadsParts::SEGMENT_H + 12.0f;
        float bottom = MainWindow::height() - 14.0f - DownloadButton::inset();
        switch (route.tab) {
            case VERSIONS: versionsTab(route, st, versions, top, bottom, covered); break;
            case CHANGELOG: changelogTab(route, st, versions, top, bottom, covered); break;
            case GALLERY: galleryTab(route, d, top, bottom, covered); break;
            case CONTENTS:
            case DEPENDENCIES: {
                if (versionsReady(route, versions, top, bottom, covered)) {
                    std::vector<const Addons::Version*> list = filtered(versions, st);
                    if (list.empty()) {
                        for (const Addons::Version& version : versions.list) {
                            list.push_back(&version);
                        }
                    }
                    if (route.tab == CONTENTS) {
                        PackTree::draw(route, list, top, bottom, covered);
                    } else {
                        DependencyView::draw(route, list, mcmod, top, bottom, covered);
                    }
                }
                break;
            }
            default: description(route, d, st, mcmod, top, bottom, covered); break;
        }
    }
}

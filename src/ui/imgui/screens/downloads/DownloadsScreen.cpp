#include "ui/imgui/screens/downloads/DownloadsScreen.h"
#include "ui/imgui/screens/downloads/AuthorPage.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/downloads/JavaPage.h"
#include "ui/imgui/screens/downloads/ThemeCatalogPage.h"
#include "ui/imgui/screens/downloads/Prerequisites.h"
#include "ui/imgui/screens/downloads/ProjectPage.h"
#include "ui/imgui/screens/downloads/SourceMarks.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/Projects.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Catalog.h"
#include "app/downloads/Aggregate.h"
#include "app/system/Config.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "app/downloads/ModNames.h"
#include "app/downloads/Modrinth.h"
#include "ui/imgui/screens/instances/NewInstanceScreen.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "app/system/Platform.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/downloads/ProjectBrowser.h"

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/theme/ThemePaint.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/NavItemWidget.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace DownloadsScreen {

    using Addons::Kind;
    using Addons::Source;

    namespace {
        constexpr float SIDE_W = 140.0f;
        float mainX() {
            return Page::left() + SIDE_W + 14.0f;
        }
        constexpr float NAV_ITEM_H = 30.0f;
        constexpr float NAV_STEP = 32.0f;
        constexpr float FILTER_ROW = 26.0f;
        constexpr float GROUP_HEAD = 30.0f;
        constexpr float GAP = 8.0f;
        constexpr float FILTER_PAD = 10.0f;
        constexpr float FILTER_H = FILTER_PAD * 2 + Page::FIELD_H * 2 + GAP;
        constexpr float RESULT_H = 56.0f;
        constexpr float RESULT_ICON = 36.0f;
        constexpr float LOADER_ICON = 16.0f;
        constexpr float LOADER_BADGE = 10.0f;
        constexpr size_t LOADER_SLOTS = 4;
        constexpr float CARD_RADIUS = 12.0f;
        constexpr size_t LOADERS_SHOWN = 3;
        constexpr double TYPING_PAUSE = 0.4;
        constexpr float SEARCH_H = NAV_ITEM_H;
        constexpr float SEARCH_RADIUS = 8.0f;
        constexpr float SEARCH_INSET = 3.0f;
        constexpr float FOUND_H = 76.0f;
        constexpr float FOUND_ICON = 40.0f;
        constexpr float STEP_H = 30.0f;

        float mainW() {
            return Page::right() - mainX();
        }

        const char* const NAME_OWNER = "downloads#name";
        const char* const ASK_OWNER = "downloads#ask";
        const char* const FOUND_SCROLL = "downloads#aggregate#results";
        const char* const ANY_INSTANCE = "不指定实例";
        const char* const ANY_VERSION = "全部版本";
        const char* const ANY_LOADER = "全部加载器";

        struct KindInfo {
            Kind kind;
            const char* label;
            int glyph;
        };

        const KindInfo KINDS[] = {
                {Kind::Mod, "模组", 0xf12e},
                {Kind::Modpack, "整合包", 0xf187},
                {Kind::ResourcePack, "资源包", 0xf5fd},
                {Kind::Shader, "光影", 0xf185},
                {Kind::Datapack, "数据包", 0xf1c0},
                {Kind::World, "地图", 0xf279},
        };

        using DownloadsParts::Route;
        using DownloadsParts::View;

        constexpr int VIEW_RESULTS = 0;
        PageTransition views(VIEW_RESULTS);

        Addons::Query query;
        std::string typed;
        double typedAt = 0.0;
        std::string target;
        std::string skippedTarget;
        bool pickedTarget = false;
        bool started = false;
        bool followed = false;
        Addons::Results shown;
        std::vector<Route> routes;
        std::string versionFilter;
        std::string loaderFilter;
        std::set<std::string> folded;
        bool allLoaders = false;
        Source source = Source::Modrinth;
        constexpr int AGGREGATE_PAGE = 2;
        constexpr int JAVA_PAGE = 3;
        constexpr int THEMES_PAGE = 4;
        PageTransition sourceViews(AGGREGATE_PAGE);
        bool aggregate = true;
        bool java = false;
        bool themes = false;
        Source browsing = Source::Modrinth;
        Source aggregateSource = Source::Modrinth;
        std::string asking;
        std::string asked;

        struct Tab {
            bool ready = false;
            std::string target;
            Addons::Query query;
            std::string typed;
            Addons::Results shown;
            std::vector<Route> routes;
            std::string versionFilter;
            std::string loaderFilter;
            std::set<std::string> folded;
            bool allLoaders = false;
        };
        Tab tabs[2];
        Tab aggregateTab;
        std::map<std::pair<int, int>, Tab> kindTabs;

        std::string fitEnd(const std::string& text, float available, float size) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, available);
        }

        float tag(const std::string& text, float x, float centreY, float limit, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            if (x + w > limit) {
                return 0.0f;
            }
            UiRender::roundRect(x, centreY - 8.0f, w, 16.0f, 5.0f, fill);
            UiRender::textY(text, x + 6.0f, centreY - 8.0f, 16.0f, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        void select(const std::string& id, const std::shared_ptr<Choice>& choice, float x, float y, float w,
                    bool blocked) {
            SelectWidget widget(id, choice);
            widget.bounds(x, y, w, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        bool contains(const std::vector<std::string>& list, const std::string& value) {
            return std::find(list.begin(), list.end(), value) != list.end();
        }

        void flip(std::vector<std::string>& list, const std::string& value) {
            auto at = std::find(list.begin(), list.end(), value);
            if (at != list.end()) {
                list.erase(at);
            } else {
                list.push_back(value);
            }
        }

        void message(const std::string& text, uint32_t color, float top, float bottom, bool loading) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float lead = loading ? 22.0f : 0.0f;
            float x = mainX() + (mainW() - w - lead) * 0.5f;
            float y = (top + bottom) * 0.5f - 12.0f;
            if (loading) {
                Page::spinner(x + 7.0f, y + 12.0f, 7.0f, UiTheme::accent());
            }
            UiRender::textY(text, x + lead, y, 24.0f, color, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }

        void projectIcon(const std::string& url, float x, float y, float size, Kind kind) {
            float radius = size * 0.22f;
            if (uint32_t texture = Textures::of(Addons::icon(url))) {
                UiRender::texture(texture, x, y, size, size);
                return;
            }
            UiRender::roundRect(x, y, size, size, radius, UiTheme::surfaceRaised());
            for (const KindInfo& info : KINDS) {
                if (info.kind == kind) {
                    UiWidgets::icon(Ui::glyph(info.glyph), x + size * 0.5f, y + size * 0.5f, UiTheme::textFaint(),
                                    size * 0.36f);
                }
            }
        }

        void projectIcon(const std::string& url, float x, float y, float size) {
            projectIcon(url, x, y, size, query.kind);
        }

        void progressRing(const std::string& id, float cx, float cy, float fraction) {
            if (fraction < 0.0f) {
                Page::spinner(cx, cy, 7.0f, UiTheme::accent());
                return;
            }
            float shown = UiState::motion("downloads#ring#" + id, fraction, 10.0f);
            Page::progressRing(cx, cy, shown);
        }

        bool hasLoaders(Kind kind) {
            return ProjectBrowser::hasLoaders(source, kind);
        }

        std::string categoryLabel(const std::string& name) {
            return source == Source::Modrinth ? Modrinth::label(name) : name;
        }

        const std::vector<std::string>& loaderTags(Kind kind) {
            return ProjectBrowser::loaderTags(source, kind);
        }

        void saveTarget() {
            Config::data()["downloadInstance"] = target;
            Config::save();
        }

        void followInstance() {
            if (Instances::find(target) == nullptr) {
                if (ProjectBrowser::usesInstance(query.kind)) {
                    query.gameVersion.clear();
                }
                if (query.kind == Kind::Mod) {
                    query.loaders.clear();
                }
                query.page = 0;
            }
            ProjectBrowser::followInstance(query, target);
            versionFilter = query.gameVersion;
            loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
        }

        void chooseTarget(const std::string& key) {
            target = key;
            saveTarget();
            followInstance();
        }

        bool suits(const std::string& key) {
            const Instances::Entry* entry = Instances::find(key);
            return entry != nullptr && (entry->info.modded() || query.kind == Kind::ResourcePack);
        }

        void enterWith(const std::string& key) {
            pickedTarget = false;
            skippedTarget = suits(key) ? std::string() : key;
            std::string chosen = skippedTarget.empty() ? key : std::string();
            if (chosen != target || !followed) {
                chooseTarget(chosen);
            }
        }

        void retarget() {
            if (pickedTarget) {
                return;
            }
            if (!target.empty() && Instances::find(target) != nullptr && !suits(target)) {
                skippedTarget = target;
                chooseTarget(std::string());
            } else if (target.empty() && suits(skippedTarget)) {
                std::string key = skippedTarget;
                skippedTarget.clear();
                chooseTarget(key);
            }
        }

        const char* const PICKER = "downloads#instancePicker";
        std::string pickingSource;

        std::shared_ptr<ModeBinding> sourceInstances() {
            return std::make_shared<ModeBinding>(
                    "downloads#instances",
                    [] {
                        const Instances::Entry* entry = Instances::find(target);
                        return entry != nullptr && entry->source == pickingSource ? entry->info.id : std::string();
                    },
                    [](const std::string& id) {
                        pickedTarget = true;
                        chooseTarget(Instances::keyOf(pickingSource, id));
                    },
                    [] {
                        std::vector<std::string> ids;
                        for (const Instances::Entry& entry : Instances::list()) {
                            if (entry.source == pickingSource) {
                                ids.push_back(entry.info.id);
                            }
                        }
                        return ids;
                    });
        }

        void instanceMenu(float x, float y, float menuY, float w);

        void instancePicker(float x, float y, float w, bool blocked) {
            const Instances::Entry* entry = Instances::find(target);
            std::string label = ANY_INSTANCE;
            if (entry != nullptr) {
                const Instances::Source* source = Instances::source(entry->source);
                label = entry->info.id + (source != nullptr ? "（" + source->label + "）" : std::string());
            }
            bool open = NewUi::menuOwner == PICKER || (NewUi::popupOpen && NewUi::popupSource == PICKER);
            bool hovered = !blocked && NewUi::hit(x, y, w, Page::FIELD_H);
            UiWidgets::select(PICKER, x, y, w, label, hovered, open);
            if (blocked || !NewUi::click(x, y, w, Page::FIELD_H)) {
                return;
            }
            UiSound::request(UiSound::TICK);
            if (open) {
                NewUi::closeMenu();
                return;
            }
            instanceMenu(x, y, y + Page::FIELD_H + 4.0f, w);
        }

        void instanceMenu(float x, float y, float menuY, float w) {
            NewUi::openMenu(
                    PICKER,
                    [x, y, w] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make(ANY_INSTANCE, 0xf05e, [] {
                                            pickedTarget = true;
                                            chooseTarget(std::string());
                                        })->checkedIf([] { return target.empty(); }));
                        for (const Instances::Source& source : Instances::sources()) {
                            std::string key = source.key;
                            size_t count =
                                    (size_t) std::count_if(Instances::list().begin(), Instances::list().end(),
                                                           [&](const Instances::Entry& e) { return e.source == key; });
                            std::string text = source.label + "（" + std::to_string(count) + " 个实例）";
                            items.push_back(ActionMenuItem::make(text, 0xf07b,
                                                                 [key, x, y, w] {
                                                                     pickingSource = key;
                                                                     NewUi::openChoice(sourceInstances(), x, y, w,
                                                                                       PICKER);
                                                                 })
                                                    ->trailing(0xf054)
                                                    ->enabled(count > 0));
                        }
                        return items;
                    },
                    x, menuY, w);
        }

        std::vector<std::string> gameVersions(const std::string& keep) {
            std::vector<std::string> out = {ANY_VERSION};
            for (const auto& version : Catalog::minecraft().list) {
                if (version.type == "release") {
                    out.push_back(version.id);
                }
            }
            if (!keep.empty() && std::find(out.begin(), out.end(), keep) == out.end()) {
                out.insert(out.begin() + 1, keep);
            }
            return out;
        }

        std::shared_ptr<ModeBinding> versionChoice(const std::string& id, std::string* value, bool resetsPage) {
            return std::make_shared<ModeBinding>(
                    id, [value] { return value->empty() ? std::string(ANY_VERSION) : *value; },
                    [value, resetsPage](const std::string& option) {
                        *value = option == ANY_VERSION ? std::string() : option;
                        if (resetsPage) {
                            query.page = 0;
                        }
                    },
                    [value] { return gameVersions(*value); });
        }

        uint32_t loaderImage(const std::string& tag);

        std::shared_ptr<ModeBinding> loaderChoice() {
            auto choice = std::make_shared<ModeBinding>(
                    "downloads#loaderFilter",
                    [] { return loaderFilter.empty() ? std::string(ANY_LOADER) : Modrinth::label(loaderFilter); },
                    [](const std::string& option) {
                        loaderFilter.clear();
                        for (const std::string& tag : loaderTags(query.kind)) {
                            if (Modrinth::label(tag) == option) {
                                loaderFilter = tag;
                            }
                        }
                    },
                    [] {
                        std::vector<std::string> labels = {ANY_LOADER};
                        for (const std::string& tag : loaderTags(query.kind)) {
                            labels.push_back(Modrinth::label(tag));
                        }
                        return labels;
                    });
            choice->icon(ANY_LOADER, 0xf12e);
            for (const std::string& tag : loaderTags(query.kind)) {
                if (uint32_t image = loaderImage(tag)) {
                    choice->image(Modrinth::label(tag), image);
                } else {
                    choice->icon(Modrinth::label(tag), 0xf12e);
                }
            }
            return choice;
        }

        int sortGlyph(Addons::Sort sort) {
            switch (sort) {
                case Addons::Sort::Relevance: return 0xf002;
                case Addons::Sort::Downloads: return 0xf019;
                case Addons::Sort::Follows: return 0xf004;
                case Addons::Sort::Newest: return 0xf271;
                case Addons::Sort::Updated: return 0xf2f1;
                case Addons::Sort::Popularity: return 0xf06d;
                case Addons::Sort::Name: return 0xf15d;
            }
            return 0;
        }

        std::shared_ptr<ModeBinding> sortChoice() {
            using ProjectBrowser::SortOption;
            const auto* sorts = &ProjectBrowser::sorts(source);
            auto choice = std::make_shared<ModeBinding>(
                    "downloads#sort",
                    [sorts] {
                        for (const SortOption& option : *sorts) {
                            if (option.sort == query.sort) {
                                return std::string(option.label);
                            }
                        }
                        return std::string(sorts->front().label);
                    },
                    [sorts](const std::string& picked) {
                        for (const SortOption& option : *sorts) {
                            if (picked == option.label) {
                                query.sort = option.sort;
                            }
                        }
                        query.page = 0;
                    },
                    [sorts] {
                        std::vector<std::string> labels;
                        for (const SortOption& option : *sorts) {
                            labels.push_back(option.label);
                        }
                        return labels;
                    });
            for (const SortOption& option : *sorts) {
                choice->icon(option.label, sortGlyph(option.sort));
            }
            return choice;
        }

        std::shared_ptr<TextBinding> nameValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        NAME_OWNER, [] { return typed; },
                        [](const std::string& text) {
                            typed = text;
                            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                                query.text = Ui::trim(text);
                                query.page = 0;
                            }
                        },
                        "名称");
                binding->limit = 100;
                return binding;
            }();
            return value;
        }

        void filters(float y, int total, bool counted, bool blocked) {
            Page::card(mainX(), y, mainW(), FILTER_H);
            float x = mainX() + FILTER_PAD;
            float w = mainW() - FILTER_PAD * 2;
            float rowY = y + FILTER_PAD;
            float nameX = x;
            if (ProjectBrowser::usesInstance(query.kind)) {
                float instanceW = 160.0f;
                instancePicker(x, rowY, instanceW, blocked);
                nameX += instanceW + GAP;
            }
            TextFieldWidget field(nameValue());
            field.box(nameX, rowY, x + w - nameX, Page::FIELD_H)
                    .bounds(nameX, rowY, x + w - nameX, Page::FIELD_H)
                    .block(blocked);
            field.paint();

            rowY += Page::FIELD_H + GAP;
            float versionW = 120.0f;
            select("downloads#version", versionChoice("downloads#version", &query.gameVersion, true), x, rowY, versionW,
                   blocked);
            select("downloads#sort", sortChoice(), x + versionW + GAP, rowY, 110.0f, blocked);
            if (counted) {
                std::string found = "共 " + std::to_string(total) + " 个";
                float foundW = UiFont::width(found, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(found, x + w - foundW, rowY, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
        }

        void followTyping() {
            bool editing = NewUi::textEditing == NAME_OWNER;
            std::string current = Ui::trim(editing ? UiInput::get(NewUi::INPUT_TEXT) : typed);
            static std::string last;
            double now = ImGui::GetTime();
            if (current != last) {
                last = current;
                typedAt = now;
            }
            if (current != query.text && now - typedAt >= TYPING_PAUSE) {
                query.text = current;
                query.page = 0;
            }
        }

        int categoryGlyph(const std::string& name) {
            static const std::map<std::string, int> glyphs = {
                    {"adventure", 0xf14e},      {"cursed", 0xf188},
                    {"decoration", 0xf015},     {"economy", 0xf155},
                    {"equipment", 0xf3ed},      {"food", 0xf787},
                    {"game-mechanics", 0xf1de}, {"library", 0xf02d},
                    {"magic", 0xf0d0},          {"management", 0xf233},
                    {"minigame", 0xf559},       {"mobs", 0xf6d5},
                    {"optimization", 0xf0e7},   {"social", 0xf075},
                    {"storage", 0xf187},        {"technology", 0xf2db},
                    {"transportation", 0xf0d1}, {"utility", 0xf0b1},
                    {"worldgen", 0xf57d},       {"combat", 0xf6e3},
                    {"challenging", 0xf6cf},    {"kitchen-sink", 0xf0c9},
                    {"lightweight", 0xf0e7},    {"multiplayer", 0xf233},
                    {"quests", 0xf559},         {"audio", 0xf001},
                    {"blocks", 0xf1b2},         {"entities", 0xf1b0},
                    {"environment", 0xf185},    {"items", 0xf3ed},
                    {"models", 0xf1b2},         {"gui", 0xf03e},
                    {"fonts", 0xf031},          {"realistic", 0xf03e},
                    {"cartoon", 0xf1fc},        {"fantasy", 0xf0d0},
                    {"foliage", 0xf06c},        {"atmosphere", 0xf0c2},
                    {"shadows", 0xf186},        {"reflections", 0xf5fd},
            };
            auto found = glyphs.find(name);
            return found != glyphs.end() ? found->second : 0xf02b;
        }

        uint32_t loaderImage(const std::string& tag) {
            using b_launcher_base::install::loader_type;
            if (tag == "fabric") {
                return Icons::loader(loader_type::fabric);
            }
            if (tag == "legacy-fabric") {
                return Icons::loader(loader_type::legacy_fabric);
            }
            if (tag == "liteloader") {
                return Icons::loader(loader_type::liteloader);
            }
            if (tag == "iris") {
                return Icons::iris();
            }
            if (tag == "canvas") {
                return Icons::canvas();
            }
            if (tag == "forge") {
                return Icons::forge();
            }
            if (tag == "neoforge") {
                return Icons::loader(loader_type::neoforge);
            }
            if (tag == "quilt") {
                return Icons::loader(loader_type::quilt);
            }
            if (tag == "optifine") {
                return Icons::loader(loader_type::optifine);
            }
            if (tag == "vanilla") {
                return Icons::vanilla();
            }
            return 0;
        }

        bool filterRow(const std::string& id, const std::string& text, int glyph, uint32_t image, bool on, float y,
                       bool blocked) {
            float x = Page::left();
            float w = SIDE_W;
            bool hovered = !blocked && NewUi::hit(x, y, w, FILTER_ROW);
            float lit = UiState::motion(id + "#on", on ? 1.0f : 0.0f, 16.0f);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, y + 1.0f, w, FILTER_ROW - 2.0f, 7.0f, UiTheme::accentSoft(Ui::round(36 * lit)));
            }
            if (hover > 0.001f) {
                UiRender::roundRect(x, y + 1.0f, w, FILTER_ROW - 2.0f, 7.0f, UiTheme::overlay(Ui::round(10 * hover)));
            }
            uint32_t ink = UiRender::mix(UiTheme::textSecondary(), UiTheme::accentText(), lit);
            if (image != 0) {
                UiRender::texture(image, x + 10.0f, y + (FILTER_ROW - 14.0f) * 0.5f, 14.0f, 14.0f);
            } else {
                UiWidgets::iconBeside(Ui::glyph(glyph), x + 17.0f, text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, y,
                                      FILTER_ROW, UiRender::mix(UiTheme::textMuted(), UiTheme::accentText(), lit),
                                      10.0f);
            }
            UiRender::textY(fitEnd(text, w - 34.0f - 22.0f, UiWidgets::TEXT_CONTROL), x + 32.0f, y, FILTER_ROW, ink,
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (lit > 0.01f) {
                float saved = UiRender::alphaMul;
                UiRender::alphaMul = saved * lit;
                UiWidgets::check(x + w - 14.0f, y + FILTER_ROW * 0.5f, UiTheme::accentText(), 8.0f);
                UiRender::alphaMul = saved;
            }
            if (!blocked && NewUi::click(x, y, w, FILTER_ROW)) {
                UiSound::request(UiSound::TICK);
                return true;
            }
            return false;
        }

        bool groupHead(const std::string& key, const std::string& title, float y, bool blocked) {
            bool open = folded.count(key) == 0;
            bool hovered = !blocked && NewUi::hit(Page::left(), y, SIDE_W, GROUP_HEAD);
            uint32_t ink = hovered ? UiTheme::textPrimary() : UiTheme::textSecondary();
            UiRender::textY(title, Page::left() + 4.0f, y, GROUP_HEAD, ink, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float turn = UiState::motion("downloads#group#" + key, open ? 1.0f : 0.0f, 16.0f);
            float cx = Page::left() + SIDE_W - 12.0f;
            float cy = y + GROUP_HEAD * 0.5f;
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * turn;
            UiWidgets::icon(Ui::glyph(0xf077), cx, cy, UiTheme::textMuted(), 9.0f);
            UiRender::alphaMul = saved * (1.0f - turn);
            UiWidgets::icon(Ui::glyph(0xf078), cx, cy, UiTheme::textMuted(), 9.0f);
            UiRender::alphaMul = saved;
            if (!blocked && NewUi::click(Page::left(), y, SIDE_W, GROUP_HEAD)) {
                UiSound::request(UiSound::TICK);
                if (open) {
                    folded.insert(key);
                } else {
                    folded.erase(key);
                }
            }
            return open;
        }

        struct Filter {
            std::string id;
            std::string text;
            int glyph;
            uint32_t image;
            bool on;
            std::function<void()> click;
        };

        float shownTop = 0.0f;
        float shownBottom = 0.0f;

        bool inSight(float y, float h) {
            return y + h >= shownTop && y <= shownBottom;
        }

        float groupHeight(const std::string& key, size_t rows, float afterH = 0.0f) {
            return GROUP_HEAD + (folded.count(key) == 0 ? rows * FILTER_ROW + afterH : 0.0f) + 6.0f;
        }

        float group(const std::string& key, const std::string& title, const std::vector<Filter>& rows, float y,
                    bool blocked, const std::function<void(float)>& after = nullptr, float afterH = 0.0f) {
            float start = y;
            bool open = folded.count(key) == 0;
            if (inSight(y, GROUP_HEAD)) {
                open = groupHead(key, title, y, blocked);
            }
            y += GROUP_HEAD;
            if (!open) {
                return y - start + 6.0f;
            }
            for (const Filter& row : rows) {
                if (inSight(y, FILTER_ROW) && filterRow(row.id, row.text, row.glyph, row.image, row.on, y, blocked) &&
                    row.click) {
                    row.click();
                }
                y += FILTER_ROW;
            }
            if (after) {
                if (inSight(y, afterH)) {
                    after(y);
                }
                y += afterH;
            }
            return y - start + 6.0f;
        }

        const char* headerTitle(const std::string& header) {
            if (header == "features") {
                return "特性";
            }
            if (header == "resolutions") {
                return "分辨率";
            }
            if (header == "performance impact") {
                return "性能要求";
            }
            return "类别";
        }

        float loaderGroup(float y, bool blocked) {
            if (!hasLoaders(query.kind)) {
                return 0.0f;
            }
            {
                const auto& tags = loaderTags(query.kind);
                bool more = tags.size() > LOADERS_SHOWN + 1;
                std::vector<Filter> rows;
                for (size_t i = 0; i < tags.size(); i++) {
                    if (more && !allLoaders && i >= LOADERS_SHOWN && !contains(query.loaders, tags[i])) {
                        continue;
                    }
                    std::string tag = tags[i];
                    rows.push_back(Filter{"downloads#loader#" + tag, Modrinth::label(tag), 0xf12e, loaderImage(tag),
                                          contains(query.loaders, tag), [tag] {
                                              bool picked = contains(query.loaders, tag);
                                              query.loaders.clear();
                                              if (!picked) {
                                                  query.loaders.push_back(tag);
                                              }
                                              query.page = 0;
                                          }});
                }
                std::function<void(float)> toggleMore;
                if (more) {
                    toggleMore = [blocked](float rowY) {
                        bool hovered = !blocked && NewUi::hit(Page::left(), rowY, SIDE_W, FILTER_ROW);
                        uint32_t ink = hovered ? UiTheme::textPrimary() : UiTheme::textMuted();
                        std::string text = allLoaders ? "收起" : "显示更多";
                        UiWidgets::iconBeside(Ui::glyph(allLoaders ? 0xf077 : 0xf078), Page::left() + 17.0f, text,
                                              UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, rowY, FILTER_ROW, ink, 9.0f);
                        UiRender::textY(text, Page::left() + 32.0f, rowY, FILTER_ROW, ink, UiFont::MEDIUM,
                                        UiWidgets::TEXT_CONTROL);
                        if (!blocked && NewUi::click(Page::left(), rowY, SIDE_W, FILTER_ROW)) {
                            UiSound::request(UiSound::TICK);
                            allLoaders = !allLoaders;
                        }
                    };
                }
                return group("loader", "加载器", rows, y, blocked, toggleMore, more ? FILTER_ROW : 0.0f);
            }
        }

        float curseGroups(float y, bool blocked) {
            float start = y;
            y += loaderGroup(y, blocked);
            std::vector<Filter> rows;
            for (const Addons::Category& category : Addons::categories(source, query.kind)) {
                std::string id = category.id;
                rows.push_back(Filter{"downloads#cf#category#" + id, category.name, 0xf02b,
                                      Textures::of(Addons::icon(category.iconUrl)), contains(query.categories, id),
                                      [id] {
                                          flip(query.categories, id);
                                          query.page = 0;
                                      }});
            }
            if (rows.empty()) {
                Page::spinner(Page::left() + 17.0f, y + 13.0f, 6.0f, UiTheme::accent());
                y += FILTER_ROW;
            } else {
                y += group("cf#categories", "类别", rows, y, blocked);
            }
            return y - start;
        }

        float modrinthGroups(float y, bool blocked) {
            float start = y;
            y += loaderGroup(y, blocked);

            const std::vector<Addons::Category>& categories = Addons::categories(source, query.kind);
            for (const char* header : {"categories", "features", "resolutions", "performance impact"}) {
                std::vector<Filter> rows;
                for (const Addons::Category& category : categories) {
                    if (category.header != header) {
                        continue;
                    }
                    std::string name = category.name;
                    rows.push_back(Filter{"downloads#category#" + name, categoryLabel(name), categoryGlyph(name), 0,
                                          contains(query.categories, name), [name] {
                                              flip(query.categories, name);
                                              query.page = 0;
                                          }});
                }
                if (!rows.empty()) {
                    y += group(header, headerTitle(header), rows, y, blocked);
                }
            }
            if (categories.empty()) {
                Page::spinner(Page::left() + 17.0f, y + 13.0f, 6.0f, UiTheme::accent());
                y += FILTER_ROW;
            }

            if (query.kind == Kind::Mod || query.kind == Kind::Modpack) {
                std::vector<Filter> rows = {
                        {"downloads#env#client", "客户端", 0xf108, 0, query.client,
                         [] {
                             query.client = !query.client;
                             query.page = 0;
                         }},
                        {"downloads#env#server", "服务端", 0xf233, 0, query.server,
                         [] {
                             query.server = !query.server;
                             query.page = 0;
                         }},
                };
                y += group("environment", "环境", rows, y, blocked);
            }
            std::vector<Filter> other = {
                    {"downloads#opensource", "仅开源", 0xf121, 0, query.openSource,
                     [] {
                         query.openSource = !query.openSource;
                         query.page = 0;
                     }},
            };
            y += group("other", "其他", other, y, blocked);
            return y - start;
        }

        float filterGroups(float y, bool blocked) {
            return source == Source::CurseForge ? curseGroups(y, blocked) : modrinthGroups(y, blocked);
        }

        float loaderHeight() {
            if (!hasLoaders(query.kind)) {
                return 0.0f;
            }
            {
                const auto& tags = loaderTags(query.kind);
                bool more = tags.size() > LOADERS_SHOWN + 1;
                size_t rows = 0;
                for (size_t i = 0; i < tags.size(); i++) {
                    if (!more || allLoaders || i < LOADERS_SHOWN || contains(query.loaders, tags[i])) {
                        rows++;
                    }
                }
                return groupHeight("loader", rows, more ? FILTER_ROW : 0.0f);
            }
        }

        float filterHeight() {
            if (source == Source::CurseForge) {
                size_t rows = Addons::categories(source, query.kind).size();
                return loaderHeight() + (rows > 0 ? groupHeight("cf#categories", rows) : FILTER_ROW);
            }
            float h = loaderHeight();
            const std::vector<Addons::Category>& categories = Addons::categories(source, query.kind);
            for (const char* header : {"categories", "features", "resolutions", "performance impact"}) {
                size_t rows = (size_t) std::count_if(
                        categories.begin(), categories.end(),
                        [header](const Addons::Category& category) { return category.header == header; });
                if (rows > 0) {
                    h += groupHeight(header, rows);
                }
            }
            if (categories.empty()) {
                h += FILTER_ROW;
            }
            if (query.kind == Kind::Mod || query.kind == Kind::Modpack) {
                h += groupHeight("environment", 2);
            }
            h += groupHeight("other", 1);
            return h;
        }

        void keep(Tab& out);
        void resume(const Tab& in);
        void stopEditing();

        void switchKind(Kind kind) {
            if (query.kind != kind) {
                stopEditing();
                keep(kindTabs[{(int) source, (int) query.kind}]);
                auto saved = kindTabs.find({(int) source, (int) kind});
                if (saved != kindTabs.end() && saved->second.ready) {
                    resume(saved->second);
                } else {
                    query.kind = kind;
                    query.text.clear();
                    query.page = 0;
                    query.categories.clear();
                    query.loaders.clear();
                    query.gameVersion.clear();
                    query.client = false;
                    query.server = false;
                    typed.clear();
                    shown = Addons::Results();
                    folded.clear();
                    allLoaders = false;
                    retarget();
                    followInstance();
                }
            }
            routes.clear();
            views.go(VIEW_RESULTS);
        }

        void keep(Tab& out) {
            out.ready = true;
            out.target = target;
            out.query = query;
            out.typed = typed;
            out.shown = shown;
            out.routes = routes;
            out.versionFilter = versionFilter;
            out.loaderFilter = loaderFilter;
            out.folded = folded;
            out.allLoaders = allLoaders;
        }

        void resume(const Tab& in) {
            query = in.query;
            typed = in.typed;
            shown = in.shown;
            routes = in.routes;
            versionFilter = in.versionFilter;
            loaderFilter = in.loaderFilter;
            folded = in.folded;
            allLoaders = in.allLoaders;
            views.page = (int) routes.size();
            if (in.target != target && ProjectBrowser::usesInstance(query.kind)) {
                followInstance();
                shown = Addons::Results();
            }
        }

        void stopEditing() {
            if (NewUi::textEditing == NAME_OWNER || NewUi::textEditing == ASK_OWNER) {
                NewUi::cancelTextEdit();
            }
        }

        void enterAggregate() {
            if (java || themes) {
                java = false;
                themes = false;
                if (aggregate) {
                    sourceViews.go(AGGREGATE_PAGE);
                    return;
                }
            }
            if (aggregate) {
                routes.clear();
                views.go(VIEW_RESULTS);
                return;
            }
            stopEditing();
            keep(tabs[(int) source]);
            browsing = source;
            aggregate = true;
            if (aggregateTab.ready) {
                source = aggregateSource;
                resume(aggregateTab);
            } else {
                views.page = VIEW_RESULTS;
            }
            views.mix = 1.0f;
            sourceViews.go(AGGREGATE_PAGE);
        }

        void switchSource(Source next) {
            if (java || themes) {
                java = false;
                themes = false;
                if (!aggregate && next == source) {
                    sourceViews.go((int) source);
                    return;
                }
            }
            if (!aggregate && next == source) {
                routes.clear();
                views.go(VIEW_RESULTS);
                return;
            }
            stopEditing();
            Kind kind = query.kind == Kind::World && next != Source::CurseForge ? Kind::Mod : query.kind;
            if (aggregate) {
                keep(aggregateTab);
                aggregateSource = source;
                aggregate = false;
                kind = Kind::Mod;
            } else {
                keep(tabs[(int) source]);
            }
            source = next;
            Tab& in = tabs[(int) next];
            if (in.ready) {
                resume(in);
            } else {
                query = Addons::Query();
                query.kind = kind;
                query.sort = next == Source::CurseForge ? Addons::Sort::Popularity : Addons::Sort::Relevance;
                typed.clear();
                shown = Addons::Results();
                routes.clear();
                folded.clear();
                allLoaders = false;
                followInstance();
                views.page = VIEW_RESULTS;
            }
            views.mix = 1.0f;
            sourceViews.go((int) next);
        }

        void enterJava() {
            stopEditing();
            java = true;
            themes = false;
            sourceViews.go(JAVA_PAGE);
        }

        void enterThemes() {
            stopEditing();
            java = false;
            themes = true;
            sourceViews.go(THEMES_PAGE);
        }

        bool sourceRow(Source each, float y, bool blocked) {
            std::string id = std::string("downloads#source#") + Addons::sourceName(each);
            float x = Page::left();
            bool active = !java && !themes && !aggregate && source == each;
            bool hovered = !blocked && NewUi::hit(x, y, SIDE_W, NAV_ITEM_H);
            float r = UiState::motion(id, active ? 1.0f : (hovered ? 0.48f : 0.0f), 16.0f);
            if (r > 0.001f) {
                UiRender::roundRect(x, y, SIDE_W, NAV_ITEM_H, 8.0f,
                                    UiRender::mix(UiTheme::clear(), UiTheme::navHover(), r));
            }
            const float mark = 14.0f;
            SourceMarks::draw(each, x + 16.0f - mark * 0.5f, y + (NAV_ITEM_H - mark) * 0.5f, mark,
                              UiRender::mix(UiTheme::textMuted(), UiTheme::accent(), r));
            UiRender::textY(Addons::sourceName(each), x + 31.0f, y, NAV_ITEM_H,
                            UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), r), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
            if (!blocked && NewUi::click(x, y, SIDE_W, NAV_ITEM_H)) {
                UiSound::request(UiSound::TICK);
                return true;
            }
            return false;
        }

        void side(bool covered) {
            float y = Page::top();
            NavItemWidget search("downloads#aggregate", Ui::glyph(0xf002), "搜索");
            search.selected(aggregate && !java && !themes).bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
            search.paint();
            if (search.pressed && (java || themes || !aggregate || views.page != VIEW_RESULTS)) {
                enterAggregate();
            }
            y += NAV_STEP;
            for (Source each : {Source::Modrinth, Source::CurseForge}) {
                if (sourceRow(each, y, covered)) {
                    switchSource(each);
                }
                y += NAV_STEP;
            }
            NavItemWidget runtime("downloads#java", Ui::glyph(0xf0f4), "Java");
            runtime.selected(java).bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
            runtime.paint();
            if (runtime.pressed && !java) {
                enterJava();
            }
            y += NAV_STEP;
            NavItemWidget launcherThemes("downloads#themes", Ui::glyph(0xf1fc), "启动器主题");
            launcherThemes.selected(themes).bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
            launcherThemes.paint();
            if (launcherThemes.pressed && !themes) {
                enterThemes();
            }
            y += NAV_STEP;
            if (aggregate || java || themes) {
                return;
            }
            y += 4.0f;
            UiRender::hLine(Page::left() + 4.0f, Page::left() + SIDE_W - 4.0f, y, UiTheme::divider());
            y += 9.0f;
            for (const KindInfo& info : KINDS) {
                if (info.kind == Kind::World && source != Source::CurseForge) {
                    continue;
                }
                NavItemWidget item(std::string("downloads#kind#") + info.label, Ui::glyph(info.glyph), info.label);
                item.selected(query.kind == info.kind).bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
                item.paint();
                if (item.pressed && (query.kind != info.kind || views.page != VIEW_RESULTS)) {
                    switchKind(info.kind);
                }
                y += NAV_STEP;
            }
            y += 4.0f;
            UiRender::hLine(Page::left() + 4.0f, Page::left() + SIDE_W - 4.0f, y, UiTheme::divider());
            float top = y + 6.0f;
            float bottom = MainWindow::height() - 14.0f;
            Page::Scroll body("downloads#filters#" + std::to_string((int) source) + "#" +
                                      std::to_string((int) query.kind),
                              top, bottom, filterHeight(), Page::left(), SIDE_W);
            bool outside = body.begin();
            shownTop = top;
            shownBottom = bottom;
            filterGroups(body.top - body.offset, outside || covered);
            body.end();
        }

        void pushRoute(const Route& route) {
            routes.push_back(route);
            if (routes.size() > 24) {
                routes.erase(routes.begin());
            }
            views.go((int) routes.size());
        }

        void openProject(const Addons::Hit& hit) {
            versionFilter = query.gameVersion;
            loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
            Route route;
            route.source = source;
            route.kind = query.kind;
            route.hit = hit;
            pushRoute(route);
        }

        bool authorLink(const Addons::Hit& hit, Source from, float right, float y, float h, bool blocked) {
            if (hit.author.empty()) {
                return false;
            }
            std::string name = fitEnd(hit.author, 140.0f, UiWidgets::TEXT_SMALL);
            float w = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float x = right - w;
            bool usable = !hit.authorId.empty();
            bool hovered = usable && !blocked && NewUi::hit(x - 2.0f, y, w + 4.0f, h);
            uint32_t ink = hovered ? UiTheme::accentText() : UiTheme::textMuted();
            UiRender::textY(name, x, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (hovered) {
                UiRender::hLine(x, x + w, y + h * 0.5f + 7.0f, ink);
                UiCursor::hand();
                if (NewUi::click(x - 2.0f, y, w + 4.0f, h)) {
                    UiSound::request(UiSound::PAGE);
                    DownloadsParts::openAuthor(from == Source::Modrinth ? Authors::Kind::ModrinthUser
                                                                        : Authors::Kind::CurseForge,
                                               hit.authorId, hit.author, std::string(), from);
                }
            }
            return true;
        }

        bool projectRow(const std::string& id, const Addons::Hit& hit, Kind kind, Source from, float x, float y,
                        float w, int index, int count, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, RESULT_H);
            float hover = UiState::motion("downloads#hit#" + id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, RESULT_H, CARD_RADIUS, UiTheme::overlay(Ui::round(14 * hover)),
                                    Page::rowCorners(index, count));
            }
            projectIcon(hit.iconUrl, x + 12.0f, y + (RESULT_H - RESULT_ICON) * 0.5f, RESULT_ICON, kind);
            float textX = x + 12.0f + RESULT_ICON + 12.0f;
            float right = x + w - 14.0f;
            float titleY = hit.description.empty() && hit.author.empty() ? y + (RESULT_H - 20.0f) * 0.5f : y + 9.0f;
            float titleCentre = titleY + 10.0f;

            float limit = right;
            if (hit.downloads > 0) {
                std::string downloads = Addons::count(hit.downloads);
                float downloadsW = UiFont::width(downloads, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float downloadsX = right - downloadsW;
                UiRender::textY(downloads, downloadsX, titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                UiWidgets::iconBeside(Ui::glyph(0xf019), downloadsX - 9.0f, downloads, UiFont::MEDIUM,
                                      UiWidgets::TEXT_SMALL, titleY, 20.0f, UiTheme::textFaint(), 9.0f);
                limit = downloadsX - 22.0f;
            }

            std::string chinese = ModNames::applies(kind) ? ModNames::zh(hit.title) : std::string();
            bool original = chinese.empty() || ModNames::original();
            std::string title = fitEnd(original ? hit.title : chinese, limit - textX, UiWidgets::TEXT_BODY);
            UiRender::textY(title, textX, titleY, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float tagX = textX + UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 8.0f;
            if (!chinese.empty() && tagX < limit) {
                std::string other = fitEnd(original ? chinese : hit.title, limit - tagX, UiWidgets::TEXT_SMALL);
                UiRender::textY(other, tagX, titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                tagX += UiFont::width(other, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 8.0f;
            }
            for (const std::string& category : hit.categories) {
                float tagW = tag(from == Source::Modrinth ? Modrinth::label(category) : category, tagX, titleCentre,
                                 limit, UiTheme::surfaceRaised(), UiTheme::textSecondary());
                if (tagW == 0.0f) {
                    break;
                }
                tagX += tagW + 4.0f;
            }
            for (const std::string& loader : hit.loaders) {
                float tagW = tag(Modrinth::label(loader), tagX, titleCentre, limit, UiTheme::accentSoft(40),
                                 UiTheme::accentText());
                if (tagW == 0.0f) {
                    break;
                }
                tagX += tagW + 4.0f;
            }
            float descriptionRight = right;
            if (authorLink(hit, from, right, y + 30.0f, 16.0f, blocked)) {
                descriptionRight -=
                        std::min(140.0f, UiFont::width(hit.author, UiFont::MEDIUM, UiWidgets::TEXT_SMALL)) + 16.0f;
            }
            UiRender::textY(fitEnd(hit.description, descriptionRight - textX, UiWidgets::TEXT_SMALL), textX, y + 30.0f,
                            16.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!blocked && NewUi::click(x, y, w, RESULT_H)) {
                UiSound::request(UiSound::PAGE);
                return true;
            }
            return false;
        }

        void resultRow(const Addons::Hit& hit, float x, float y, float w, int index, int count, bool blocked) {
            if (projectRow(hit.id, hit, query.kind, source, x, y, w, index, count, blocked)) {
                openProject(hit);
            }
        }

        void pages(float y, int total, bool blocked) {
            int count = std::max(1, (total + Addons::PAGE_SIZE - 1) / Addons::PAGE_SIZE);
            int page = std::min(query.page, count - 1);
            std::vector<int> numbers;
            if (count <= 7) {
                for (int i = 0; i < count; i++) {
                    numbers.push_back(i);
                }
            } else {
                int from = std::max(1, std::min(page - 1, count - 4));
                int to = std::min(count - 2, std::max(page + 1, 3));
                numbers.push_back(0);
                if (from > 1) {
                    numbers.push_back(-1);
                }
                for (int i = from; i <= to; i++) {
                    numbers.push_back(i);
                }
                if (to < count - 2) {
                    numbers.push_back(-1);
                }
                numbers.push_back(count - 1);
            }
            const float cell = 28.0f;
            const float gapW = 18.0f;
            auto cellW = [&](int number) {
                if (number < 0) {
                    return gapW;
                }
                return std::max(cell,
                                UiFont::width(std::to_string(number + 1), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) +
                                        16.0f);
            };
            float totalW = cell * 2.0f;
            for (int number : numbers) {
                totalW += cellW(number);
            }
            float x = mainX() + (mainW() - totalW) * 0.5f;
            float cy = y + cell * 0.5f;

            auto arrow = [&](const char* id, int glyph, int to, bool usable) {
                bool hovered = usable && !blocked && NewUi::hit(x, y, cell, cell);
                float hover = UiState::motion(std::string("downloads#pages#") + id, hovered ? 1.0f : 0.0f, 20.0f);
                if (hover > 0.001f) {
                    UiRender::roundRect(x, y, cell, cell, cell * 0.5f, UiTheme::overlay(Ui::round(14 * hover)));
                }
                UiWidgets::icon(Ui::glyph(glyph), x + cell * 0.5f, cy,
                                usable ? UiTheme::textSecondary() : UiTheme::textFaint(), 10.0f);
                if (usable && !blocked && NewUi::click(x, y, cell, cell)) {
                    query.page = to;
                }
                x += cell;
            };

            arrow("prev", 0xf053, page - 1, page > 0);
            for (int number : numbers) {
                float w = cellW(number);
                if (number < 0) {
                    UiWidgets::icon(Ui::glyph(0xf141), x + w * 0.5f, cy, UiTheme::textFaint(), 9.0f);
                    x += w;
                    continue;
                }
                bool current = number == page;
                bool hovered = !current && !blocked && NewUi::hit(x, y, w, cell);
                float hover =
                        UiState::motion("downloads#pages#" + std::to_string(number), hovered ? 1.0f : 0.0f, 20.0f);
                if (current) {
                    UiRender::roundRect(x, y, w, cell, cell * 0.5f, UiTheme::accentSoft(56));
                } else if (hover > 0.001f) {
                    UiRender::roundRect(x, y, w, cell, cell * 0.5f, UiTheme::overlay(Ui::round(14 * hover)));
                }
                std::string label = std::to_string(number + 1);
                float labelW = UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(label, x + (w - labelW) * 0.5f, y, cell,
                                current ? UiTheme::accentText() : UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (!current && !blocked && NewUi::click(x, y, w, cell)) {
                    query.page = number;
                }
                x += w;
            }
            arrow("next", 0xf054, page + 1, page + 1 < count);
        }

        void resultsView(bool covered) {
            followTyping();
            Addons::Query effective = query;
            bool translating = false;
            if (ModNames::applies(query.kind)) {
                ModNames::Rewrite rewrite = ModNames::rewrite(query.text);
                translating = rewrite.waiting;
                effective.text = rewrite.text;
            }
            float y = Page::top();
            float listTop = y + FILTER_H + 10.0f;
            const float pagesH = 28.0f;
            float pagesY = MainWindow::height() - 12.0f - pagesH;
            float listBottom = pagesY - 8.0f;
            static const Addons::Results waiting;
            const Addons::Results& results = translating ? waiting : Addons::search(source, effective);
            static const Addons::Results* shownFrom = nullptr;
            static bool shownReady = false;
            bool named = results.status != Addons::Status::Ready || !ModNames::applies(query.kind) ||
                    ModNames::original() || ModNames::resolved(results.hits);
            bool ready = results.status == Addons::Status::Ready && named;
            if (ready && (shownFrom != &results || !shownReady)) {
                shown = results;
            }
            shownFrom = &results;
            shownReady = ready;
            bool loading = results.status == Addons::Status::Loading || !named;
            filters(y, shown.total, !shown.hits.empty() || ready, covered);

            if (results.status == Addons::Status::Failed) {
                message("搜索失败：" + results.error, UiTheme::danger(), listTop, listBottom, false);
                std::string retry = "重试";
                float w = Page::buttonWidth(retry);
                if (Page::button("downloads#retry", retry, mainX() + (mainW() - w) * 0.5f,
                                 (listTop + listBottom) * 0.5f + 20.0f, w, false, covered)) {
                    Addons::refresh(source, effective);
                }
                return;
            }
            if (shown.hits.empty()) {
                if (loading) {
                    message(std::string("正在搜索 ") + Addons::sourceName(source), UiTheme::textMuted(), listTop,
                            listBottom, true);
                } else {
                    message("没有找到符合条件的内容", UiTheme::textMuted(), listTop, listBottom, false);
                }
                return;
            }
            bool stale = loading || results.status == Addons::Status::Failed;
            float contentH = shown.hits.size() * RESULT_H;
            Page::Scroll body("downloads#results#" + std::to_string((int) source) + "#" +
                                      std::to_string((int) query.kind),
                              listTop, listBottom, contentH, mainX(), mainW());
            bool outside = body.begin();
            bool blocked = outside || covered || stale;
            float rowsY = body.top - body.offset;
            float saved = UiRender::alphaMul;
            if (stale) {
                UiRender::alphaMul = saved * 0.5f;
            }
            Page::listCard(mainX(), rowsY, mainW(), RESULT_H, (int) shown.hits.size());
            for (size_t i = 0; i < shown.hits.size(); i++) {
                float rowY = rowsY + i * RESULT_H;
                if (rowY + RESULT_H < listTop || rowY > listBottom) {
                    continue;
                }
                resultRow(shown.hits[i], mainX(), rowY, mainW(), (int) i, (int) shown.hits.size(), blocked);
            }
            UiRender::alphaMul = saved;
            body.end();
            if (loading) {
                Page::spinner(mainX() + mainW() * 0.5f, (listTop + listBottom) * 0.5f, 9.0f, UiTheme::accent());
            }
            pages(pagesY, shown.total, covered || loading);
        }

        std::optional<Kind> kindOf(const std::string& type) {
            if (type == "mod") {
                return Kind::Mod;
            }
            if (type == "modpack") {
                return Kind::Modpack;
            }
            if (type == "resourcepack") {
                return Kind::ResourcePack;
            }
            if (type == "shader") {
                return Kind::Shader;
            }
            if (type == "datapack") {
                return Kind::Datapack;
            }
            return std::nullopt;
        }

        const char* typeLabel(const std::string& type) {
            if (type == "mod") {
                return "模组";
            }
            if (type == "modpack") {
                return "整合包";
            }
            if (type == "resourcepack") {
                return "资源包";
            }
            if (type == "shader") {
                return "光影";
            }
            if (type == "datapack") {
                return "数据包";
            }
            if (type == "plugin") {
                return "插件";
            }
            return "";
        }

        Aggregate::Request askedRequest() {
            Aggregate::Request request;
            request.text = asked;
            if (const Instances::Entry* entry = Instances::find(target)) {
                request.gameVersion = entry->info.minecraft_version;
                std::string loader = ProjectInstaller::loaderOf(target);
                if (loader == "forge" || loader == "neoforge" || loader == "fabric" || loader == "quilt") {
                    request.loader = loader;
                }
            }
            return request;
        }

        void ask(const std::string& text) {
            std::string trimmed = Ui::trim(text);
            if (trimmed.empty()) {
                return;
            }
            if (trimmed == asked) {
                Aggregate::Request request = askedRequest();
                if (Aggregate::search(request).status == Addons::Status::Failed) {
                    Aggregate::refresh(request);
                }
            }
            asked = trimmed;
            Page::scrollTo(FOUND_SCROLL, 0.0f);
        }

        std::shared_ptr<TextBinding> askValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        ASK_OWNER, [] { return asking; },
                        [](const std::string& text) {
                            asking = text;
                            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                                ask(text);
                            }
                        },
                        "搜索模组或整合包");
                binding->limit = 200;
                return binding;
            }();
            return value;
        }

        std::string scopeLabel() {
            const Instances::Entry* entry = Instances::find(target);
            if (entry == nullptr) {
                return ANY_VERSION;
            }
            std::string label = entry->info.minecraft_version;
            std::string loader = ProjectInstaller::loaderOf(target);
            if (!loader.empty()) {
                label += " " + Modrinth::label(loader);
            }
            return label;
        }

        void searchBar(float x, float y, float w, bool blocked) {
            const float inner = SEARCH_H - SEARCH_INSET * 2.0f;
            float innerY = y + SEARCH_INSET;
            float buttonX = x + w - SEARCH_INSET - inner;
            std::string scope = scopeLabel();
            float scopeW = 12.0f + UiFont::width(scope, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
            float scopeX = buttonX - 4.0f - scopeW;
            bool open = NewUi::menuOwner == PICKER || (NewUi::popupOpen && NewUi::popupSource == PICKER);
            bool scopePressed = !blocked && NewUi::click(scopeX, innerY, scopeW, inner);
            bool searchPressed = !blocked && NewUi::click(buttonX, innerY, inner, inner);

            TextFieldWidget field(askValue());
            field.box(x, y, w, SEARCH_H).lead(4.0f).radius(SEARCH_RADIUS).room(x + w - scopeX);
            field.bounds(x, y, scopeX - x, SEARCH_H).block(blocked);
            field.paint();

            bool scopeHovered = !blocked && NewUi::hit(scopeX, innerY, scopeW, inner);
            float scopeLit = UiState::motion("downloads#scope#hover", scopeHovered || open ? 1.0f : 0.0f, 18.0f);
            if (scopeLit > 0.001f) {
                UiRender::roundRect(scopeX, innerY, scopeW, inner, SEARCH_RADIUS - SEARCH_INSET,
                                    UiTheme::overlay(Ui::round(14 * scopeLit)));
            }
            UiRender::textY(scope, scopeX + 12.0f, innerY, inner,
                            UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), scopeLit), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            UiWidgets::icon(Ui::glyph(0xf078), scopeX + scopeW - 15.0f, innerY + inner * 0.5f, UiTheme::textMuted(),
                            8.0f);
            if (scopeHovered && !open) {
                if (const Instances::Entry* entry = Instances::find(target)) {
                    Page::tooltip(entry->info.id, scopeX, innerY, scopeW, inner);
                }
            }

            bool searchHovered = !blocked && NewUi::hit(buttonX, innerY, inner, inner);
            float searchLit = UiState::motion("downloads#ask#hover", searchHovered ? 1.0f : 0.0f, 18.0f);
            ThemePaint::primaryFill(buttonX, innerY, inner, inner, SEARCH_RADIUS - SEARCH_INSET, 0.10f * searchLit);
            UiWidgets::icon(Ui::glyph(0xf002), buttonX + inner * 0.5f, innerY + inner * 0.5f,
                            UiTheme::tokenOr(UiTheme::PRIMARY_TEXT, UiTheme::onAccent()), 11.0f);

            if (scopePressed) {
                UiSound::request(UiSound::TICK);
                if (open) {
                    NewUi::closeMenu();
                } else {
                    float menuW = std::max(scopeW, 220.0f);
                    instanceMenu(scopeX + scopeW - menuW, innerY + (inner - Page::FIELD_H) * 0.5f,
                                 innerY + inner + 6.0f, menuW);
                }
            }
            if (searchPressed) {
                UiSound::request(UiSound::TICK);
                std::string current = asking;
                if (NewUi::textEditing == ASK_OWNER) {
                    current = UiInput::get(NewUi::INPUT_TEXT);
                    NewUi::commitTextEdit();
                }
                ask(current);
            }
        }

        void openLink(const Aggregate::Item& item, const Aggregate::Link& link) {
            std::optional<Kind> kind = kindOf(item.type);
            if (!kind) {
                Platform::openUrl(link.url);
                return;
            }
            source = link.source;
            query = Addons::Query();
            query.kind = *kind;
            followInstance();
            folded.clear();
            allLoaders = false;
            Addons::Hit hit;
            hit.id = link.id;
            hit.slug = link.slug;
            hit.title = item.title;
            hit.description = item.summary;
            hit.iconUrl = item.iconUrl;
            hit.pageUrl = link.url;
            hit.loaders = item.loaders;
            hit.downloads = link.downloads;
            openProject(hit);
        }

        void openItem(const Aggregate::Item& item) {
            if (!item.links.empty()) {
                openLink(item, item.links.front());
            } else if (!item.mcmodUrl.empty()) {
                Platform::openUrl(item.mcmodUrl);
            }
        }

        std::string versionRange(const std::vector<std::string>& versions) {
            if (versions.empty()) {
                return std::string();
            }
            return versions.size() == 1 ? versions.front() : versions.back() + "–" + versions.front();
        }

        std::string loaderNames(const std::vector<std::string>& loaders) {
            std::string out;
            for (size_t i = 0; i < loaders.size() && i < LOADERS_SHOWN; i++) {
                out += (out.empty() ? "" : " / ") + Modrinth::label(loaders[i]);
            }
            return out;
        }

        float foundLinks(const Aggregate::Item& item, const std::string& id, float right, float y, bool blocked,
                         bool& over) {
            struct Entry {
                std::string label;
                const Aggregate::Link* link;
            };
            std::vector<Entry> entries;
            for (const Aggregate::Link& link : item.links) {
                entries.push_back(Entry{Addons::sourceName(link.source), &link});
            }
            if (!item.mcmodUrl.empty()) {
                entries.push_back(Entry{"MC百科", nullptr});
            }
            const float mark = 11.0f;
            float x = right;
            for (size_t i = entries.size(); i-- > 0;) {
                const Entry& entry = entries[i];
                float w = mark + 4.0f + UiFont::width(entry.label, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                x -= w;
                bool hovered = !blocked && NewUi::hit(x - 3.0f, y, w + 6.0f, 18.0f);
                float lit = UiState::motion(id + "#link#" + entry.label, hovered ? 1.0f : 0.0f, 20.0f);
                uint32_t ink = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
                if (entry.link != nullptr) {
                    SourceMarks::draw(entry.link->source, x, y + (18.0f - mark) * 0.5f, mark, ink);
                } else {
                    SourceMarks::mcmod(x, y + (18.0f - mark) * 0.5f, mark, ink);
                }
                UiRender::textY(entry.label, x + mark + 4.0f, y, 18.0f, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (hovered) {
                    over = true;
                    UiCursor::hand();
                }
                if (hovered && NewUi::click(x - 3.0f, y, w + 6.0f, 18.0f)) {
                    UiSound::request(UiSound::PAGE);
                    if (entry.link != nullptr) {
                        openLink(item, *entry.link);
                    } else {
                        Platform::openUrl(item.mcmodUrl);
                    }
                }
                x -= 12.0f;
            }
            return x;
        }

        void foundRow(const Aggregate::Item& item, size_t index, size_t count, float x, float y, float w,
                      bool blocked) {
            std::string id = "downloads#found#" + std::to_string(index) + "#" + item.title;
            bool hovered = !blocked && NewUi::hit(x, y, w, FOUND_H);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, FOUND_H, CARD_RADIUS, UiTheme::overlay(Ui::round(14 * hover)),
                                    Page::rowCorners((int) index, (int) count));
            }
            std::optional<Kind> kind = kindOf(item.type);
            projectIcon(item.iconUrl, x + 14.0f, y + (FOUND_H - FOUND_ICON) * 0.5f, FOUND_ICON,
                        kind.value_or(Kind::Mod));
            float textX = x + 14.0f + FOUND_ICON + 14.0f;
            float right = x + w - 16.0f;

            float metaY = y + 50.0f;
            bool overLink = false;
            float metaLimit = foundLinks(item, id, right, metaY, blocked, overLink);
            float metaX = textX;
            std::string type = typeLabel(item.type);
            if (!type.empty()) {
                float typeW =
                        tag(type, metaX, metaY + 9.0f, metaLimit, UiTheme::surfaceRaised(), UiTheme::textSecondary());
                if (typeW > 0.0f) {
                    metaX += typeW + 10.0f;
                }
            }
            for (const std::string& part : {loaderNames(item.loaders), versionRange(item.gameVersions)}) {
                float partW = UiFont::width(part, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (part.empty() || metaX + partW > metaLimit) {
                    continue;
                }
                UiRender::textY(part, metaX, metaY, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                metaX += partW + 12.0f;
            }

            float titleY = y + 10.0f;
            std::string downloads = Addons::count(item.downloads);
            float downloadsW = UiFont::width(downloads, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float downloadsX = right - downloadsW;
            UiRender::textY(downloads, downloadsX, titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            UiWidgets::iconBeside(Ui::glyph(0xf019), downloadsX - 9.0f, downloads, UiFont::MEDIUM,
                                  UiWidgets::TEXT_SMALL, titleY, 20.0f, UiTheme::textFaint(), 9.0f);
            float limit = downloadsX - 22.0f;
            bool original = item.zh.empty() || ModNames::original();
            std::string name = fitEnd(original ? item.title : item.zh, limit - textX, UiWidgets::TEXT_BODY);
            uint32_t link = UiTheme::accentText();
            UiRender::textY(name, textX, titleY, 20.0f, link, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            if (hovered && !overLink) {
                UiRender::hLine(textX, textX + nameW, titleY + 18.0f, link);
            }
            float otherX = textX + nameW + 8.0f;
            if (!item.zh.empty() && otherX < limit) {
                UiRender::textY(fitEnd(original ? item.zh : item.title, limit - otherX, UiWidgets::TEXT_SMALL), otherX,
                                titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            UiRender::textY(fitEnd(item.summary, right - textX, UiWidgets::TEXT_SMALL), textX, y + 31.0f, 16.0f,
                            UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!blocked && !overLink && NewUi::click(x, y, w, FOUND_H)) {
                UiSound::request(UiSound::PAGE);
                openItem(item);
            }
        }

        void foundSummary(const Aggregate::Results& results, float x, float y, float right, bool blocked) {
            std::string count = std::to_string(results.items.size()) + " 个结果";
            float countW = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(count, right - countW, y, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float limit = right - countW - 12.0f;
            if (results.partial) {
                uint32_t ink = UiTheme::warningInk();
                float tagW = tag("结果可能不完整", x, y + Page::FIELD_H * 0.5f, limit,
                                 (ink & 0x00FFFFFFu) | 0x2E000000u, ink);
                if (tagW > 0.0f) {
                    if (!blocked && NewUi::hit(x, y, tagW, Page::FIELD_H)) {
                        Page::tooltip("部分平台没有响应", x, y, tagW, Page::FIELD_H);
                    }
                    x += tagW + 8.0f;
                }
            }
            if (!results.summary.empty() && x < limit) {
                UiRender::textY(fitEnd(results.summary, limit - x, UiWidgets::TEXT_SMALL), x, y, Page::FIELD_H,
                                UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
        }

        std::string targetName(const std::string& target) {
            if (target == "mcmod") {
                return "MC百科";
            }
            if (target == "modrinth") {
                return "Modrinth";
            }
            if (target == "curseforge") {
                return "CurseForge";
            }
            if (target == "modrinth+curseforge") {
                return "Modrinth 和 CurseForge";
            }
            return target;
        }

        void searchProgress(const Aggregate::Results& results, float top) {
            enum class Mark { Running, Found, Empty };
            struct Row {
                Mark mark;
                std::string text;
                std::string detail;
            };
            std::vector<Row> rows;
            if (!results.thought.empty()) {
                rows.push_back(Row{Mark::Found, results.thought, std::string()});
            }
            bool running = false;
            for (const Aggregate::Step& step : results.steps) {
                Row row{Mark::Running, "在 " + targetName(step.target) + " 搜索“" + step.keyword + "”", std::string()};
                if (!step.done) {
                    running = true;
                } else if (step.count > 0) {
                    row.mark = Mark::Found;
                    row.detail = "找到 " + std::to_string(step.count) + " 个";
                    for (size_t i = 0; i < step.titles.size(); i++) {
                        row.detail += (i == 0 ? "：" : "、") + step.titles[i];
                    }
                } else {
                    row.mark = Mark::Empty;
                    row.detail = "没有结果";
                }
                rows.push_back(std::move(row));
            }
            if (!running) {
                rows.push_back(Row{Mark::Running, results.steps.empty() ? "正在搜索" : "正在整理结果", std::string()});
            }
            float x = mainX();
            float w = mainW();
            Page::listCard(x, top, w, STEP_H, (int) rows.size());
            for (size_t i = 0; i < rows.size(); i++) {
                const Row& row = rows[i];
                float rowY = top + i * STEP_H;
                float centreY = rowY + STEP_H * 0.5f;
                switch (row.mark) {
                    case Mark::Running: Page::spinner(x + 20.0f, centreY, 6.0f, UiTheme::accent()); break;
                    case Mark::Found: UiWidgets::check(x + 20.0f, centreY, UiTheme::accent(), 9.0f); break;
                    case Mark::Empty:
                        UiWidgets::icon(Ui::glyph(0xf068), x + 20.0f, centreY, UiTheme::textFaint(), 8.5f);
                        break;
                }
                float textX = x + 36.0f;
                float right = x + w - 14.0f;
                std::string text = fitEnd(row.text, right - textX, UiWidgets::TEXT_CONTROL);
                UiRender::textY(text, textX, rowY, STEP_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                float detailX = textX + UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 10.0f;
                if (!row.detail.empty() && detailX < right) {
                    UiRender::textY(fitEnd(row.detail, right - detailX, UiWidgets::TEXT_SMALL), detailX, rowY, STEP_H,
                                    UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                }
            }
        }

        void aggregateView(bool covered) {
            float barX = mainX();
            float barW = mainW();
            float bottom = MainWindow::height() - 14.0f;
            searchBar(barX, Page::top(), barW, covered);
            if (asked.empty()) {
                return;
            }
            float toolsY = Page::top() + SEARCH_H + 6.0f;
            float listTop = toolsY + Page::FIELD_H + 4.0f;
            Aggregate::Request request = askedRequest();
            const Aggregate::Results& results = Aggregate::search(request);
            if (results.status == Addons::Status::Loading) {
                searchProgress(results, toolsY + 4.0f);
                return;
            }
            if (results.status == Addons::Status::Failed) {
                message("搜索失败：" + results.error, UiTheme::danger(), listTop, bottom, false);
                std::string retry = "重试";
                float w = Page::buttonWidth(retry);
                if (Page::button("downloads#aggregate#retry", retry, mainX() + (mainW() - w) * 0.5f,
                                 (listTop + bottom) * 0.5f + 20.0f, w, false, covered)) {
                    Aggregate::refresh(request);
                }
                return;
            }
            if (results.items.empty()) {
                message("没有找到相关内容", UiTheme::textMuted(), listTop, bottom, false);
                if (!request.gameVersion.empty()) {
                    std::string wider = "搜索全部版本";
                    float w = Page::buttonWidth(wider);
                    if (Page::button("downloads#aggregate#wider", wider, mainX() + (mainW() - w) * 0.5f,
                                     (listTop + bottom) * 0.5f + 20.0f, w, false, covered)) {
                        chooseTarget(std::string());
                    }
                }
                return;
            }
            foundSummary(results, barX + 4.0f, toolsY, barX + barW - 4.0f, covered);
            float contentH = results.items.size() * FOUND_H;
            Page::Scroll body(FOUND_SCROLL, listTop, bottom, contentH, mainX(), mainW());
            bool outside = body.begin();
            bool blocked = outside || covered;
            float rowsY = body.top - body.offset;
            Page::listCard(mainX(), rowsY, mainW(), FOUND_H, (int) results.items.size());
            for (size_t i = 0; i < results.items.size(); i++) {
                float rowY = rowsY + i * FOUND_H;
                if (rowY + FOUND_H < listTop || rowY > bottom) {
                    continue;
                }
                foundRow(results.items[i], i, results.items.size(), mainX(), rowY, mainW(), blocked);
            }
            body.end();
        }

        std::string pendingSlug;
        Kind pendingKind = Kind::Mod;
        Mcmod::Ref pendingMcmod;
        std::string pendingUrl;

        struct SlugLookup {
            Addons::Status status = Addons::Status::Loading;
            std::string id;
        };

        std::map<std::string, SlugLookup> slugs;
        AsyncTasks slugTasks;

        const SlugLookup& curseSlug(const std::string& slug, Kind kind) {
            std::string key = std::to_string((int) kind) + ":" + slug;
            auto found = slugs.find(key);
            if (found != slugs.end()) {
                return found->second;
            }
            SlugLookup& entry = slugs[key];
            int classId = kind == Kind::Modpack  ? 4471
                    : kind == Kind::ResourcePack ? 12
                    : kind == Kind::Shader       ? 6552
                    : kind == Kind::Datapack     ? 6945
                    : kind == Kind::World        ? 17
                                                 : 6;
            slugTasks.run(
                    [key, slug, classId](b_launcher_base::cancellation_token token) -> AsyncTasks::Continuation {
                        auto doc = CurseForge::call("/mods/search?gameId=432&classId=" + std::to_string(classId) +
                                                            "&slug=" + b_launcher_base::url_encode(slug),
                                                    std::string(), token);
                        std::string id;
                        if (doc) {
                            auto data = doc->find("data");
                            if (data != doc->end() && data->is_array() && !data->empty()) {
                                id = CurseForge::hitFrom(data->front()).id;
                            }
                        }
                        return [key, id] {
                            slugs[key].id = id;
                            slugs[key].status = id.empty() ? Addons::Status::Failed : Addons::Status::Ready;
                        };
                    },
                    [key](const std::string&) -> AsyncTasks::Continuation {
                        return [key] { slugs[key].status = Addons::Status::Failed; };
                    });
            return entry;
        }

        void openSlug(Source from, Kind kind, const std::string& slug) {
            Addons::Hit hit;
            hit.id = slug;
            hit.slug = slug;
            hit.title = slug;
            Route route;
            route.source = from;
            route.kind = kind;
            route.hit = hit;
            pushRoute(route);
        }

        void followPending() {
            slugTasks.poll();
            if (!pendingSlug.empty()) {
                const SlugLookup& found = curseSlug(pendingSlug, pendingKind);
                if (found.status == Addons::Status::Ready) {
                    openSlug(Source::CurseForge, pendingKind, found.id);
                    pendingSlug.clear();
                    pendingUrl.clear();
                } else if (found.status == Addons::Status::Failed) {
                    if (!pendingUrl.empty()) {
                        Platform::openUrl(pendingUrl);
                    }
                    pendingSlug.clear();
                    pendingUrl.clear();
                }
                return;
            }
            if (pendingMcmod.id <= 0) {
                return;
            }
            std::string modrinth = Mcmod::slugOn(pendingMcmod, "modrinth");
            std::string curseforge = Mcmod::slugOn(pendingMcmod, "curseforge");
            if (modrinth.empty() && curseforge.empty()) {
                const Mcmod::Mod& found = Mcmod::mod(pendingMcmod);
                if (found.status == Addons::Status::Loading) {
                    return;
                }
                if (found.status == Addons::Status::Ready) {
                    modrinth = found.modrinth;
                    curseforge = found.curseforge;
                }
            }
            Kind kind = pendingMcmod.type == "modpack" ? Kind::Modpack : Kind::Mod;
            if (!modrinth.empty()) {
                openSlug(Source::Modrinth, kind, modrinth);
            } else if (!curseforge.empty()) {
                pendingSlug = curseforge;
                pendingKind = kind;
                pendingUrl = pendingMcmod.url;
            } else if (!pendingMcmod.url.empty()) {
                Platform::openUrl(pendingMcmod.url);
            }
            pendingMcmod = Mcmod::Ref();
        }

        void drawRoute(bool covered) {
            Route& route = routes.back();
            if (route.view == View::Author) {
                AuthorPage::draw(route, covered);
            } else {
                ProjectPage::draw(route, covered);
            }
        }
    }

    void draw() {
        if (!started) {
            started = true;
            const nlohmann::json& doc = Config::data();
            auto saved = doc.find("downloadInstance");
            target = saved != doc.end() && saved->is_string() ? saved->get<std::string>() : Instances::selected();
        }
        if (!followed && Instances::find(target) != nullptr) {
            enterWith(target);
            followed = true;
        }
        if (!target.empty() && Instances::find(target) == nullptr && !Instances::loading()) {
            chooseTarget(std::string());
        }
        bool covered = NewUi::overlayCoversMouse();
        followPending();
        side(covered);
        sourceViews.begin();
        views.begin();
        if (java) {
            JavaPage::draw(mainX(), covered);
        } else if (themes) {
            ThemeCatalogPage::draw(mainX(), covered);
        } else if (!routes.empty()) {
            drawRoute(covered);
        } else if (aggregate) {
            aggregateView(covered);
        } else {
            resultsView(covered);
        }
        views.end();
        sourceViews.end();
    }

    void openFor(const std::string& key) {
        if (Instances::find(key) == nullptr) {
            return;
        }
        enterWith(key);
        started = true;
        followed = true;
    }

    void openJava() {
        if (!java) {
            enterJava();
        }
        MainWindow::showDownloads();
    }

    void open(Addons::Kind kind) {
        openFor(Instances::selected());
        if (aggregate) {
            switchSource(browsing);
        }
        switchKind(kind);
        MainWindow::showDownloads();
    }

    void showProject(const std::string& instanceKey, Source from, Kind kind, const Addons::Hit& hit) {
        if (aggregate || source != from) {
            switchSource(from);
        }
        switchKind(kind);
        openFor(instanceKey);
        versionFilter = query.gameVersion;
        loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
        Route route;
        route.source = from;
        route.kind = kind;
        route.hit = hit;
        pushRoute(route);
        MainWindow::showDownloads();
    }
}

namespace DownloadsParts {

    using Addons::Kind;
    using Addons::Source;

    float mainX() {
        return DownloadsScreen::mainX();
    }

    float mainW() {
        return DownloadsScreen::mainW();
    }

    float tag(const std::string& text, float x, float centreY, float limit, uint32_t fill, uint32_t ink) {
        return DownloadsScreen::tag(text, x, centreY, limit, fill, ink);
    }

    void message(const std::string& text, uint32_t color, float top, float bottom, bool loading) {
        DownloadsScreen::message(text, color, top, bottom, loading);
    }

    void projectIcon(const std::string& url, float x, float y, float size, Kind kind) {
        DownloadsScreen::projectIcon(url, x, y, size, kind);
    }

    uint32_t loaderImage(const std::string& tag) {
        return DownloadsScreen::loaderImage(tag);
    }

    void progressRing(const std::string& id, float cx, float cy, float fraction) {
        DownloadsScreen::progressRing(id, cx, cy, fraction);
    }

    void select(const std::string& id, const std::shared_ptr<Choice>& choice, float x, float y, float w, bool blocked) {
        DownloadsScreen::select(id, choice, x, y, w, blocked);
    }

    void instancePicker(float x, float y, float w, bool blocked) {
        DownloadsScreen::instancePicker(x, y, w, blocked);
    }

    std::shared_ptr<Choice> versionChoice() {
        return DownloadsScreen::versionChoice("downloads#versionFilter", &DownloadsScreen::versionFilter, false);
    }

    std::shared_ptr<Choice> loaderChoice(Source source, Kind kind) {
        auto choice = std::make_shared<ModeBinding>(
                "downloads#loaderFilter",
                [] {
                    return DownloadsScreen::loaderFilter.empty() ? std::string(DownloadsScreen::ANY_LOADER)
                                                                 : Modrinth::label(DownloadsScreen::loaderFilter);
                },
                [source, kind](const std::string& option) {
                    DownloadsScreen::loaderFilter.clear();
                    for (const std::string& tag : ProjectBrowser::loaderTags(source, kind)) {
                        if (Modrinth::label(tag) == option) {
                            DownloadsScreen::loaderFilter = tag;
                        }
                    }
                },
                [source, kind] {
                    std::vector<std::string> labels = {DownloadsScreen::ANY_LOADER};
                    for (const std::string& tag : ProjectBrowser::loaderTags(source, kind)) {
                        labels.push_back(Modrinth::label(tag));
                    }
                    return labels;
                });
        choice->icon(DownloadsScreen::ANY_LOADER, 0xf12e);
        for (const std::string& tag : ProjectBrowser::loaderTags(source, kind)) {
            if (uint32_t image = loaderImage(tag)) {
                choice->image(Modrinth::label(tag), image);
            } else {
                choice->icon(Modrinth::label(tag), 0xf12e);
            }
        }
        return choice;
    }

    std::string& versionFilter() {
        return DownloadsScreen::versionFilter;
    }

    std::string& loaderFilter() {
        return DownloadsScreen::loaderFilter;
    }

    const std::string& target() {
        return DownloadsScreen::target;
    }

    std::string displayTitle(const std::string& title, Kind kind) {
        std::string chinese = ModNames::applies(kind) ? ModNames::zh(title) : std::string();
        return chinese.empty() || ModNames::original() ? title : chinese;
    }

    std::string otherTitle(const std::string& title, Kind kind) {
        std::string chinese = ModNames::applies(kind) ? ModNames::zh(title) : std::string();
        if (chinese.empty()) {
            return std::string();
        }
        return ModNames::original() ? chinese : title;
    }

    void versionAction(const Addons::Version& version, const Route& route, float right, float rowY, float rowH,
                       bool primary, bool blocked) {
        const std::string& owner = DownloadsScreen::target;
        float centreY = rowY + rowH * 0.5f;
        Kind kind = route.kind;
        std::string taskTarget = kind == Kind::World ? owner : std::string();
        ProjectInstaller::State state = ProjectInstaller::state(version.id, taskTarget);
        if (state == ProjectInstaller::State::Running) {
            progressRing(version.id, right - 20.0f, centreY, ProjectInstaller::progress(version.id, taskTarget));
            return;
        }
        bool intoInstance = !ProjectInstaller::folder(kind).empty() || kind == Kind::World;
        bool saveAs = intoInstance && Instances::find(owner) == nullptr;
        if (!saveAs &&
            ((intoInstance && ProjectInstaller::installed(version, kind, owner)) ||
             ((kind == Kind::Modpack || kind == Kind::World) && state == ProjectInstaller::State::Done))) {
            std::string done = "已安装";
            float doneW = UiFont::width(done, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(done, right - doneW, rowY, rowH, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            UiWidgets::check(right - doneW - 12.0f, centreY, UiTheme::accent(), 9.0f);
            return;
        }
        std::string text = saveAs ? "另存为" : kind == Kind::Datapack ? "下载" : "安装";
        int glyph = saveAs ? 0xf0c7 : 0xf019;
        float w = Page::buttonWidth(text, glyph);
        float x = right - w;
        std::string id = std::string("downloads#install#") + (primary ? "head#" : "") + version.id;
        if (!Page::button(id, text, x, rowY + (rowH - Page::FIELD_H) * 0.5f, w, primary, blocked, glyph)) {
            return;
        }
        const std::string& title = route.hit.title;
        const std::string& icon = route.hit.iconUrl;
        if (kind == Kind::Mod && Prerequisites::needed(version)) {
            Prerequisites::open(version, title, saveAs, owner, icon);
            return;
        }
        if (saveAs) {
            ProjectInstaller::saveAs(version, title, icon);
            return;
        }
        switch (kind) {
            case Kind::Modpack: {
                auto setup = ModpackSetup::fromProject(version, title);
                if (setup) {
                    NewInstanceScreen::openModpack(*setup);
                    MainWindow::showNewInstance();
                }
                break;
            }
            case Kind::Datapack: {
                const Instances::Entry* entry = Instances::find(owner);
                std::filesystem::path start =
                        entry != nullptr ? entry->gameDirectory / "saves" : Platform::userDirectory();
                if (std::optional<std::filesystem::path> folder = Platform::pickFolder(start)) {
                    ProjectInstaller::saveTo(version, *folder, title, icon);
                }
                break;
            }
            case Kind::World: ProjectInstaller::installWorld(version, title, owner, icon); break;
            default: ProjectInstaller::install(version, kind, title, owner, std::nullopt, icon); break;
        }
    }

    bool projectRow(const std::string& id, const Addons::Hit& hit, Kind kind, Source source, float x, float y, float w,
                    int index, int count, bool blocked) {
        return DownloadsScreen::projectRow(id, hit, kind, source, x, y, w, index, count, blocked);
    }

    void push(const Route& route) {
        DownloadsScreen::pushRoute(route);
    }

    void back() {
        if (!DownloadsScreen::routes.empty()) {
            DownloadsScreen::routes.pop_back();
        }
        DownloadsScreen::views.go((int) DownloadsScreen::routes.size());
    }

    void openProject(Source source, Kind kind, const Addons::Hit& hit) {
        Route route;
        route.source = source;
        route.kind = kind;
        route.hit = hit;
        push(route);
    }

    void openAuthor(Authors::Kind kind, const std::string& id, const std::string& name, const std::string& avatar,
                    Source source) {
        if (id.empty()) {
            return;
        }
        Route route;
        route.view = View::Author;
        route.source = source;
        route.author = kind;
        route.authorId = id;
        route.authorName = name;
        route.authorAvatar = avatar;
        push(route);
    }

    void openMcmod(const Mcmod::Ref& ref) {
        if (ref.id > 0) {
            DownloadsScreen::pendingMcmod = ref;
        }
    }

    void follow(const std::string& url) {
        std::string rest = url;
        size_t scheme = rest.find("://");
        if (scheme != std::string::npos) {
            rest = rest.substr(scheme + 3);
        }
        rest = rest.substr(0, rest.find_first_of("?#"));
        size_t slash = rest.find('/');
        std::string host = b_launcher_base::to_lower(rest.substr(0, slash));
        std::vector<std::string> parts;
        if (slash != std::string::npos) {
            std::string path = rest.substr(slash + 1);
            size_t start = 0;
            while (start <= path.size()) {
                size_t end = path.find('/', start);
                std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!part.empty()) {
                    parts.push_back(part);
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
        }
        if ((host == "modrinth.com" || host == "www.modrinth.com") && parts.size() >= 2) {
            const std::string& type = parts[0];
            if (type == "user") {
                openAuthor(Authors::Kind::ModrinthUser, parts[1], parts[1], std::string(), Source::Modrinth);
                return;
            }
            if (type == "organization") {
                openAuthor(Authors::Kind::ModrinthOrganization, parts[1], parts[1], std::string(), Source::Modrinth);
                return;
            }
            if (type == "mod" || type == "modpack" || type == "resourcepack" || type == "shader" ||
                type == "datapack" || type == "plugin" || type == "project") {
                DownloadsScreen::openSlug(Source::Modrinth, Modrinth::kindOf(type), parts[1]);
                return;
            }
        }
        if ((host == "curseforge.com" || host == "www.curseforge.com") && parts.size() >= 3 &&
            parts[0] == "minecraft") {
            static const std::map<std::string, Kind> classes = {
                    {"mc-mods", Kind::Mod},    {"modpacks", Kind::Modpack},    {"texture-packs", Kind::ResourcePack},
                    {"shaders", Kind::Shader}, {"data-packs", Kind::Datapack}, {"worlds", Kind::World},
            };
            auto found = classes.find(parts[1]);
            if (found != classes.end()) {
                DownloadsScreen::pendingSlug = parts[2];
                DownloadsScreen::pendingKind = found->second;
                DownloadsScreen::pendingUrl = url;
                return;
            }
        }
        if ((host == "mcmod.cn" || host == "www.mcmod.cn") && parts.size() >= 2 &&
            (parts[0] == "class" || parts[0] == "modpack")) {
            Mcmod::Ref ref;
            ref.id = std::atoll(parts[1].c_str());
            ref.type = parts[0] == "class" ? "mod" : "modpack";
            ref.url = url;
            if (ref.id > 0) {
                openMcmod(ref);
                return;
            }
        }
        Platform::openUrl(url);
    }
}

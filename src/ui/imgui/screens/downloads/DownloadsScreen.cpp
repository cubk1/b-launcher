#include "ui/imgui/screens/downloads/DownloadsScreen.h"
#include "ui/imgui/screens/downloads/SourceMarks.h"
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
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/NavItemWidget.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/installer/loader.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
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

        constexpr int VIEW_RESULTS = 0;
        constexpr int VIEW_PROJECT = 1;
        PageTransition views(VIEW_RESULTS);

        Addons::Query query;
        std::string typed;
        double typedAt = 0.0;
        std::string target;
        bool started = false;
        bool followed = false;
        Addons::Results shown;
        Addons::Hit project;
        std::string versionFilter;
        std::string loaderFilter;
        std::set<std::string> folded;
        bool allLoaders = false;
        Source source = Source::Modrinth;
        constexpr int AGGREGATE_PAGE = 2;
        PageTransition sourceViews(AGGREGATE_PAGE);
        bool aggregate = true;
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
            Addons::Hit project;
            std::string versionFilter;
            std::string loaderFilter;
            std::set<std::string> folded;
            bool allLoaders = false;
            int view = VIEW_RESULTS;
        };
        Tab tabs[2];
        Tab aggregateTab;

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

        const char* const PICKER = "downloads#instancePicker";
        std::string pickingSource;

        std::shared_ptr<ModeBinding> sourceInstances() {
            return std::make_shared<ModeBinding>(
                    "downloads#instances",
                    [] {
                        const Instances::Entry* entry = Instances::find(target);
                        return entry != nullptr && entry->source == pickingSource ? entry->info.id : std::string();
                    },
                    [](const std::string& id) { chooseTarget(Instances::keyOf(pickingSource, id)); },
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

        void switchKind(Kind kind) {
            if (query.kind != kind) {
                query.kind = kind;
                query.page = 0;
                query.categories.clear();
                query.loaders.clear();
                query.gameVersion.clear();
                query.client = false;
                query.server = false;
                shown = Addons::Results();
                allLoaders = false;
                followInstance();
            }
            views.go(VIEW_RESULTS);
        }

        void keep(Tab& out) {
            out.ready = true;
            out.target = target;
            out.query = query;
            out.typed = typed;
            out.shown = shown;
            out.project = project;
            out.versionFilter = versionFilter;
            out.loaderFilter = loaderFilter;
            out.folded = folded;
            out.allLoaders = allLoaders;
            out.view = views.page;
        }

        void resume(const Tab& in) {
            query = in.query;
            typed = in.typed;
            shown = in.shown;
            project = in.project;
            versionFilter = in.versionFilter;
            loaderFilter = in.loaderFilter;
            folded = in.folded;
            allLoaders = in.allLoaders;
            views.page = in.view;
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
            if (aggregate) {
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
            if (!aggregate && next == source) {
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
                project = Addons::Hit();
                folded.clear();
                allLoaders = false;
                followInstance();
                views.page = VIEW_RESULTS;
            }
            views.mix = 1.0f;
            sourceViews.go((int) next);
        }

        bool sourceRow(Source each, float y, bool blocked) {
            std::string id = std::string("downloads#source#") + Addons::sourceName(each);
            float x = Page::left();
            bool active = !aggregate && source == each;
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
            float y = Page::TOP;
            NavItemWidget search("downloads#aggregate", Ui::glyph(0xf002), "搜索");
            search.selected(aggregate).bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
            search.paint();
            if (search.pressed && (!aggregate || views.page != VIEW_RESULTS)) {
                enterAggregate();
            }
            y += NAV_STEP;
            for (Source each : {Source::Modrinth, Source::CurseForge}) {
                if (sourceRow(each, y, covered)) {
                    switchSource(each);
                }
                y += NAV_STEP;
            }
            if (aggregate) {
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

        void openProject(const Addons::Hit& hit) {
            project = hit;
            versionFilter = query.gameVersion;
            loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
            views.go(VIEW_PROJECT);
        }

        void resultRow(const Addons::Hit& hit, float x, float y, float w, int index, int count, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, RESULT_H);
            float hover = UiState::motion("downloads#hit#" + hit.id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, RESULT_H, CARD_RADIUS, UiTheme::overlay(Ui::round(14 * hover)),
                                    Page::rowCorners(index, count));
            }
            projectIcon(hit.iconUrl, x + 12.0f, y + (RESULT_H - RESULT_ICON) * 0.5f, RESULT_ICON);
            float textX = x + 12.0f + RESULT_ICON + 12.0f;
            float right = x + w - 14.0f;
            float titleY = y + 9.0f;
            float titleCentre = titleY + 10.0f;

            std::string downloads = Addons::count(hit.downloads);
            float downloadsW = UiFont::width(downloads, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float downloadsX = right - downloadsW;
            UiRender::textY(downloads, downloadsX, titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            UiWidgets::iconBeside(Ui::glyph(0xf019), downloadsX - 9.0f, downloads, UiFont::MEDIUM,
                                  UiWidgets::TEXT_SMALL, titleY, 20.0f, UiTheme::textFaint(), 9.0f);
            float limit = downloadsX - 22.0f;

            std::string chinese = ModNames::applies(query.kind) ? ModNames::zh(hit.title) : std::string();
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
                float tagW = tag(categoryLabel(category), tagX, titleCentre, limit, UiTheme::surfaceRaised(),
                                 UiTheme::textSecondary());
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
            UiRender::textY(fitEnd(hit.description, right - textX, UiWidgets::TEXT_SMALL), textX, y + 30.0f, 16.0f,
                            UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!blocked && NewUi::click(x, y, w, RESULT_H)) {
                UiSound::request(UiSound::PAGE);
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
            float y = Page::TOP;
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
            UiRender::roundRect(buttonX, innerY, inner, inner, SEARCH_RADIUS - SEARCH_INSET,
                                UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.10f * searchLit));
            UiWidgets::icon(Ui::glyph(0xf002), buttonX + inner * 0.5f, innerY + inner * 0.5f, UiTheme::onAccent(),
                            11.0f);

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
                    UiWidgets::icon(Ui::glyph(0xf02d), x + mark * 0.5f, y + 9.0f, ink, 9.5f);
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
                uint32_t ink = UiTheme::isLight() ? 0xFFB36B00u : 0xFFF0B04Cu;
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
            searchBar(barX, Page::TOP, barW, covered);
            if (asked.empty()) {
                return;
            }
            float toolsY = Page::TOP + SEARCH_H + 6.0f;
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

        const char* versionTypeLabel(const std::string& type) {
            if (type == "beta") {
                return "测试版";
            }
            if (type == "alpha") {
                return "早期版";
            }
            return "正式版";
        }

        void openModpackInstall(const Addons::Version& version) {
            auto setup = ModpackSetup::fromProject(version, project.title);
            if (!setup) {
                return;
            }
            NewInstanceScreen::openModpack(*setup);
            MainWindow::showNewInstance();
        }

        void versionAction(const Addons::Version& version, float rowY, bool blocked) {
            float centreY = rowY + Page::ROW_H * 0.5f;
            float right = Page::right() - 12.0f;
            Kind kind = query.kind;
            std::string taskTarget = kind == Kind::World ? target : std::string();
            ProjectInstaller::State state = ProjectInstaller::state(version.id, taskTarget);
            if (state == ProjectInstaller::State::Running) {
                progressRing(version.id, right - 20.0f, centreY, ProjectInstaller::progress(version.id, taskTarget));
                return;
            }
            bool intoInstance = !ProjectInstaller::folder(kind).empty() || kind == Kind::World;
            bool saveAs = intoInstance && Instances::find(target) == nullptr;
            if (!saveAs &&
                ((intoInstance && ProjectInstaller::installed(version, kind, target)) ||
                 ((kind == Kind::Modpack || kind == Kind::World) && state == ProjectInstaller::State::Done))) {
                std::string done = "已安装";
                float doneW = UiFont::width(done, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(done, right - doneW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                UiWidgets::check(right - doneW - 12.0f, centreY, UiTheme::accent(), 9.0f);
                return;
            }
            std::string text = saveAs ? "另存为" : kind == Kind::Datapack ? "下载" : "安装";
            int glyph = saveAs ? 0xf0c7 : 0xf019;
            float w = Page::buttonWidth(text, glyph);
            float x = right - w;
            if (!Page::button("downloads#install#" + version.id, text, x, rowY + Page::FIELD_Y, w, false, blocked,
                              glyph)) {
                return;
            }
            if (saveAs) {
                ProjectInstaller::saveAs(version, project.title);
                return;
            }
            switch (kind) {
                case Kind::Modpack: openModpackInstall(version); break;
                case Kind::Datapack: {
                    const Instances::Entry* entry = Instances::find(target);
                    std::filesystem::path start =
                            entry != nullptr ? entry->gameDirectory / "saves" : Platform::exeDirectory();
                    if (std::optional<std::filesystem::path> folder = Platform::pickFolder(start)) {
                        ProjectInstaller::saveTo(version, *folder, project.title);
                    }
                    break;
                }
                case Kind::World: ProjectInstaller::installWorld(version, project.title, target); break;
                default: ProjectInstaller::install(version, kind, project.title, target); break;
            }
        }

        std::vector<std::pair<uint32_t, std::string>> loaderIcons(const Addons::Version& version) {
            std::vector<std::pair<uint32_t, std::string>> out;
            for (const std::string& loader : version.loaders) {
                if (out.size() == LOADER_SLOTS) {
                    break;
                }
                if (uint32_t image = loaderImage(loader)) {
                    out.emplace_back(image, loader);
                }
            }
            return out;
        }

        bool hasLoaderIcons(const std::vector<const Addons::Version*>& versions) {
            return std::any_of(versions.begin(), versions.end(), [](const Addons::Version* version) {
                return std::any_of(version->loaders.begin(), version->loaders.end(),
                                   [](const std::string& loader) { return loaderImage(loader) != 0; });
            });
        }

        void versionRow(const Addons::Version& version, const std::string& prefix, float rowY, int index, int count,
                        bool showIcons, bool blocked) {
            bool hovered = !blocked && NewUi::hit(mainX(), rowY, mainW(), Page::ROW_H);
            float hover = UiState::motion("downloads#version#" + version.id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(mainX(), rowY, mainW(), Page::ROW_H, CARD_RADIUS,
                                    UiTheme::overlay(Ui::round(10 * hover)), Page::rowCorners(index, count));
            }
            float centreY = rowY + Page::ROW_H * 0.5f;
            float x = mainX() + 14.0f;
            bool overIcon = false;
            if (showIcons) {
                std::vector<std::pair<uint32_t, std::string>> icons = loaderIcons(version);
                float iconY = centreY - LOADER_ICON * 0.5f;
                if (!icons.empty()) {
                    UiRender::texture(icons.front().first, x, iconY, LOADER_ICON, LOADER_ICON);
                    if (icons.size() > 1) {
                        float badgeX = x + LOADER_ICON - LOADER_BADGE + 3.0f;
                        float badgeY = iconY + LOADER_ICON - LOADER_BADGE + 3.0f;
                        UiRender::roundRect(badgeX - 1.0f, badgeY - 1.0f, LOADER_BADGE + 2.0f, LOADER_BADGE + 2.0f,
                                            3.0f, UiTheme::surface());
                        if (hover > 0.001f) {
                            UiRender::roundRect(badgeX - 1.0f, badgeY - 1.0f, LOADER_BADGE + 2.0f, LOADER_BADGE + 2.0f,
                                                3.0f, UiTheme::overlay(Ui::round(10 * hover)));
                        }
                        UiRender::texture(icons[1].first, badgeX, badgeY, LOADER_BADGE, LOADER_BADGE);
                    }
                    if (hovered && NewUi::hit(x - 2.0f, iconY - 2.0f, LOADER_ICON + 6.0f, LOADER_ICON + 6.0f)) {
                        overIcon = true;
                        std::string names;
                        for (const auto& icon : icons) {
                            names += (names.empty() ? "" : "、") + Modrinth::label(icon.second);
                        }
                        Page::tooltip(names, x, iconY, LOADER_ICON, LOADER_ICON);
                    }
                }
                x += LOADER_ICON + 8.0f;
            }
            std::string detail = version.published.substr(0, std::min<size_t>(10, version.published.size()));
            if (versionFilter.empty() && !version.gameVersions.empty()) {
                detail = version.gameVersions.back() + (version.gameVersions.size() > 1 ? " 等" : "") + "  " + detail;
            }
            float detailW = UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float detailX = Page::right() - 104.0f - detailW;
            float limit = detailX - 10.0f;
            std::string typeLabel = versionTypeLabel(version.type);
            float typeRoom = typeLabel.empty()
                    ? 0.0f
                    : UiFont::width(typeLabel, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f + 8.0f;
            std::string name = ProjectBrowser::shortName(version, prefix, project.title);
            std::string shownName = fitEnd(name, std::max(60.0f, limit - x - typeRoom), UiWidgets::TEXT_CONTROL);
            UiRender::textY(shownName, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (shownName != name && hovered && !overIcon) {
                Page::tooltip(name, x, rowY + 8.0f, UiFont::width(shownName, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL),
                    Page::ROW_H - 16.0f);
            }
            UiRender::textY(detail, detailX, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float tagX = x + UiFont::width(shownName, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
            uint32_t typeInk = UiTheme::textSecondary();
            uint32_t typeFill = UiTheme::surfaceRaised();
            if (version.type == "beta" || version.type == "alpha") {
                typeInk = version.type == "beta" ? (UiTheme::isLight() ? 0xFFB36B00u : 0xFFF0B04Cu) : UiTheme::danger();
                typeFill = (typeInk & 0x00FFFFFFu) | 0x2E000000u;
            }
            if (!typeLabel.empty()) {
                tag(typeLabel, tagX, centreY, limit, typeFill, typeInk);
            }
            versionAction(version, rowY, blocked);
        }

        void projectView(bool covered) {
            if (Page::iconButton("downloads#back", 0xf053, "返回", mainX() - 4.0f, Page::TOP + 2.0f, covered)) {
                views.go(VIEW_RESULTS);
            }
            float iconX = mainX() + 30.0f;
            projectIcon(project.iconUrl, iconX, Page::TOP, 32.0f);
            float textX = iconX + 44.0f;
            float linkX = Page::right() - 28.0f;
            bool named = ModNames::applies(query.kind);
            std::string chinese = named ? ModNames::zh(project.title) : std::string();
            long long wiki = named ? ModNames::wikiId(project.title) : 0;
            float wikiX = linkX - 32.0f;
            float textRight = (wiki > 0 ? wikiX : linkX) - 8.0f;
            bool original = chinese.empty() || ModNames::original();
            std::string heading = fitEnd(original ? project.title : chinese, textRight - textX, Page::TITLE_SIZE);
            UiRender::textY(heading, textX, Page::TOP - 2.0f, 22.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            float englishX = textX + UiFont::width(heading, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            if (!chinese.empty() && englishX < textRight) {
                UiRender::textY(
                        fitEnd(original ? chinese : project.title, textRight - englishX, UiWidgets::TEXT_CONTROL),
                        englishX, Page::TOP - 2.0f, 22.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
            }
            std::string byline = project.author.empty() ? std::string() : project.author + "  ";
            byline += Addons::count(project.downloads) + " 次下载";
            UiRender::textY(fitEnd(byline, textRight - textX, UiWidgets::TEXT_SMALL), textX, Page::TOP + 20.0f, 16.0f,
                            UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            std::string site = std::string("在 ") + Addons::sourceName(source) + " 上查看";
            if (Page::iconButton("downloads#open", 0xf35d, site, linkX, Page::TOP + 2.0f, covered) &&
                !project.pageUrl.empty()) {
                Platform::openUrl(project.pageUrl);
            }
            if (wiki > 0 &&
                Page::iconButton("downloads#wiki", 0xf02d, "在 MC 百科上查看", wikiX, Page::TOP + 2.0f, covered)) {
                Platform::openUrl("https://www.mcmod.cn/class/" + std::to_string(wiki) + ".html");
            }

            float rowY = Page::TOP + 46.0f;
            float filterX = mainX();
            if (ProjectBrowser::usesInstance(query.kind)) {
                float instanceW = 150.0f;
                instancePicker(mainX(), rowY, instanceW, covered);
                filterX += instanceW + GAP;
            }
            float cell = (Page::right() - filterX - GAP) / 2.0f;
            select("downloads#versionFilter", versionChoice("downloads#versionFilter", &versionFilter, false), filterX,
                   rowY, cell, covered);
            if (hasLoaders(query.kind)) {
                select("downloads#loaderFilter", loaderChoice(), filterX + cell + GAP, rowY, cell, covered);
            }

            float listTop = rowY + Page::FIELD_H + 12.0f;
            float listBottom = MainWindow::height() - 14.0f;
            const Addons::Versions& versions = Addons::versions(source, project.id);
            if (versions.status == Addons::Status::Loading) {
                message("正在获取版本列表", UiTheme::textMuted(), listTop, listBottom, true);
                return;
            }
            if (versions.status == Addons::Status::Failed) {
                message("获取版本失败：" + versions.error, UiTheme::danger(), listTop, listBottom, false);
                std::string retry = "重试";
                float w = Page::buttonWidth(retry);
                if (Page::button("downloads#versions#retry", retry, mainX() + (mainW() - w) * 0.5f,
                                 (listTop + listBottom) * 0.5f + 20.0f, w, false, covered)) {
                    Addons::refreshVersions(source, project.id);
                }
                return;
            }
            std::string prefix = ProjectBrowser::sharedPrefix(versions.list);
            auto matching = ProjectBrowser::matching(versions.list, versionFilter, loaderFilter);
            if (matching.empty()) {
                message("没有符合条件的版本", UiTheme::textMuted(), listTop, listBottom, false);
                return;
            }
            bool showIcons = hasLoaderIcons(matching);
            float contentH = matching.size() * Page::ROW_H;
            Page::Scroll body("downloads#versions#" + std::to_string((int) source) + "#" + project.id, listTop,
                              listBottom, contentH, mainX(), mainW());
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            Page::listCard(mainX(), y, mainW(), Page::ROW_H, (int) matching.size());
            for (size_t i = 0; i < matching.size(); i++) {
                float versionY = y + i * Page::ROW_H;
                if (versionY + Page::ROW_H < listTop || versionY > listBottom) {
                    continue;
                }
                versionRow(*matching[i], prefix, versionY, (int) i, (int) matching.size(), showIcons, blocked);
            }
            body.end();
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
            followed = true;
            followInstance();
        }
        if (!target.empty() && Instances::find(target) == nullptr && !Instances::loading()) {
            chooseTarget(std::string());
        }
        bool covered = NewUi::overlayCoversMouse();
        side(covered);
        sourceViews.begin();
        views.begin();
        if (views.page == VIEW_PROJECT) {
            projectView(covered);
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
        if (key != target || !followed) {
            chooseTarget(key);
        }
        started = true;
        followed = true;
    }

    void open(Addons::Kind kind) {
        openFor(Instances::selected());
        if (aggregate) {
            switchSource(browsing);
        }
        switchKind(kind);
        MainWindow::showDownloads();
    }
}

#include "DownloadsScreen.h"
#include "Catalog.h"
#include "Config.h"
#include "Icons.h"
#include "Instances.h"
#include "MainWindow.h"
#include "Modrinth.h"
#include "NewInstanceScreen.h"
#include "Page.h"
#include "Platform.h"
#include "ProjectInstaller.h"

#include "ui/gl/ShaderUtil.h"
#include "ui/menu/MenuItems.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiInput.h"
#include "ui/panel/UiPageTransition.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"
#include "ui/value/Bind.h"
#include "ui/widget/IconButtonWidget.h"
#include "ui/widget/NavItemWidget.h"
#include "ui/widget/SelectWidget.h"
#include "ui/widget/TextFieldWidget.h"

#include <core/installer/loader.hpp>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace DownloadsScreen {

    using Modrinth::Kind;

    namespace {
        constexpr float SIDE_W = 140.0f;
        constexpr float MAIN_X = Page::LEFT + SIDE_W + 14.0f;
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
        constexpr size_t LOADER_SLOTS = 4;
        constexpr float CARD_RADIUS = 12.0f;
        constexpr size_t LOADERS_SHOWN = 3;
        constexpr double TYPING_PAUSE = 0.4;

        float mainW() {
            return Page::right() - MAIN_X;
        }

        const char* const NAME_OWNER = "downloads#name";
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
        };

        constexpr int VIEW_RESULTS = 0;
        constexpr int VIEW_PROJECT = 1;
        PageTransition views(VIEW_RESULTS);

        Modrinth::Query query;
        std::string typed;
        double typedAt = 0.0;
        std::string target;
        bool started = false;
        bool followed = false;
        Modrinth::Results shown;
        Modrinth::Hit project;
        std::string versionFilter;
        std::string loaderFilter;
        std::set<std::string> folded;
        bool allLoaders = false;

        void tip(const std::string& text, float x, float y, float w, float h) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
            NewUi::tooltipText = text;
            NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
            NewUi::tooltipY = y + h + 6.0f;
        }

        bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y, bool blocked) {
            IconButtonWidget button(id, Ui::glyph(glyph), UiTheme::textSecondary(), UiTheme::textPrimary());
            button.bounds(x, y, 28, 28).block(blocked);
            button.paint();
            if (!blocked && button.hovered()) {
                tip(hint, x, y, 28, 28);
            }
            return button.pressed;
        }

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

        int corners(int index, int count) {
            return (index == 0 ? 1 | 2 : 0) | (index == count - 1 ? 4 | 8 : 0);
        }

        void card(float x, float y, float w, float h) {
            UiRender::roundRect(x, y, w, h, CARD_RADIUS, UiTheme::surface());
            UiRender::outline(x, y, w, h, CARD_RADIUS, UiTheme::outlineSoft());
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
            float x = MAIN_X + (mainW() - w - lead) * 0.5f;
            float y = (top + bottom) * 0.5f - 12.0f;
            if (loading) {
                Page::spinner(x + 7.0f, y + 12.0f, 7.0f, UiTheme::accent());
            }
            UiRender::textY(text, x + lead, y, 24.0f, color, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }

        void projectIcon(const std::string& url, float x, float y, float size) {
            float radius = size * 0.22f;
            if (uint32_t texture = Modrinth::icon(url)) {
                UiRender::texture(texture, x, y, size, size);
                return;
            }
            UiRender::roundRect(x, y, size, size, radius, UiTheme::surfaceRaised());
            for (const KindInfo& info : KINDS) {
                if (info.kind == query.kind) {
                    UiWidgets::icon(Ui::glyph(info.glyph), x + size * 0.5f, y + size * 0.5f, UiTheme::textFaint(),
                                    size * 0.36f);
                }
            }
        }

        void progressRing(float cx, float cy, float fraction) {
            if (fraction < 0.0f) {
                Page::spinner(cx, cy, 7.0f, UiTheme::accent());
                return;
            }
            const float tau = 6.28318531f;
            float r = UiRender::transformW(7.0f);
            float stroke = UiRender::transformW(1.7f);
            float x = UiRender::transformX(cx);
            float y = UiRender::transformY(cy);
            ShaderUtil::arc(x, y, r, stroke, 0.0f, tau,
                            UiRender::applyAlpha((UiTheme::accent() & 0x00FFFFFFu) | 0x40000000u));
            float shown = UiState::motion("downloads#ring", fraction, 10.0f);
            if (shown > 0.001f) {
                ShaderUtil::arc(x, y, r, stroke, -tau * 0.25f, -tau * 0.25f + tau * std::min(1.0f, shown),
                                UiRender::applyAlpha(UiTheme::accent()));
            }
        }

        bool usesInstance(Kind kind) {
            return kind != Kind::Modpack;
        }

        bool hasLoaders(Kind kind) {
            return kind == Kind::Mod || kind == Kind::Modpack || kind == Kind::Shader;
        }

        std::vector<std::string> loaderTags(Kind kind) {
            if (kind == Kind::Shader) {
                return {"iris", "optifine", "canvas", "vanilla"};
            }
            if (kind == Kind::Modpack) {
                return {"fabric", "forge", "neoforge", "quilt"};
            }
            return {"fabric", "forge", "neoforge", "quilt", "legacy-fabric", "liteloader"};
        }

        void saveTarget() {
            Config::data()["downloadInstance"] = target;
            Config::save();
        }

        void followInstance() {
            const Instances::Entry* entry = Instances::find(target);
            if (entry == nullptr) {
                return;
            }
            if (query.kind != Kind::Modpack) {
                query.gameVersion = entry->info.minecraft_version;
            }
            if (query.kind == Kind::Mod) {
                std::string loader = ProjectInstaller::loaderOf(target);
                query.loaders.clear();
                if (!loader.empty()) {
                    query.loaders.push_back(loader);
                }
            }
            query.page = 0;
        }

        std::vector<std::pair<std::string, std::string>> instanceOptions() {
            std::vector<std::pair<std::string, std::string>> out;
            const std::vector<Instances::Entry>& list = Instances::list();
            for (const Instances::Entry& entry : list) {
                bool shared = std::count_if(list.begin(), list.end(), [&](const Instances::Entry& other) {
                                  return other.info.id == entry.info.id;
                              }) > 1;
                std::string label = entry.info.id;
                if (shared) {
                    const Instances::Source* source = Instances::source(entry.source);
                    label += "（" + (source != nullptr ? source->label : entry.source) + "）";
                }
                out.emplace_back(label, entry.key);
            }
            return out;
        }

        void chooseTarget(const std::string& key) {
            target = key;
            saveTarget();
            followInstance();
            versionFilter = query.gameVersion;
            loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
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
                    x, y + Page::FIELD_H + 4.0f, w);
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

        std::shared_ptr<ModeBinding> loaderChoice() {
            return std::make_shared<ModeBinding>(
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
        }

        std::shared_ptr<ModeBinding> sortChoice() {
            static const std::vector<std::pair<Modrinth::Sort, const char*>> sorts = {
                    {Modrinth::Sort::Relevance, "相关度"}, {Modrinth::Sort::Downloads, "下载量"},
                    {Modrinth::Sort::Follows, "关注数"},   {Modrinth::Sort::Newest, "最新发布"},
                    {Modrinth::Sort::Updated, "最近更新"},
            };
            return std::make_shared<ModeBinding>(
                    "downloads#sort",
                    [] {
                        for (const auto& sort : sorts) {
                            if (sort.first == query.sort) {
                                return std::string(sort.second);
                            }
                        }
                        return std::string(sorts.front().second);
                    },
                    [](const std::string& option) {
                        for (const auto& sort : sorts) {
                            if (option == sort.second) {
                                query.sort = sort.first;
                            }
                        }
                        query.page = 0;
                    },
                    [] {
                        std::vector<std::string> labels;
                        for (const auto& sort : sorts) {
                            labels.push_back(sort.second);
                        }
                        return labels;
                    });
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
            card(MAIN_X, y, mainW(), FILTER_H);
            float x = MAIN_X + FILTER_PAD;
            float w = mainW() - FILTER_PAD * 2;
            float rowY = y + FILTER_PAD;
            float nameX = x;
            if (usesInstance(query.kind)) {
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
            float x = Page::LEFT;
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
            bool hovered = !blocked && NewUi::hit(Page::LEFT, y, SIDE_W, GROUP_HEAD);
            uint32_t ink = hovered ? UiTheme::textPrimary() : UiTheme::textSecondary();
            UiRender::textY(title, Page::LEFT + 4.0f, y, GROUP_HEAD, ink, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float turn = UiState::motion("downloads#group#" + key, open ? 1.0f : 0.0f, 16.0f);
            float cx = Page::LEFT + SIDE_W - 12.0f;
            float cy = y + GROUP_HEAD * 0.5f;
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * turn;
            UiWidgets::icon(Ui::glyph(0xf077), cx, cy, UiTheme::textMuted(), 9.0f);
            UiRender::alphaMul = saved * (1.0f - turn);
            UiWidgets::icon(Ui::glyph(0xf078), cx, cy, UiTheme::textMuted(), 9.0f);
            UiRender::alphaMul = saved;
            if (!blocked && NewUi::click(Page::LEFT, y, SIDE_W, GROUP_HEAD)) {
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

        float filterGroups(float y, bool blocked) {
            float start = y;
            if (hasLoaders(query.kind)) {
                std::vector<std::string> tags = loaderTags(query.kind);
                bool more = tags.size() > LOADERS_SHOWN + 1;
                std::vector<Filter> rows;
                for (size_t i = 0; i < tags.size(); i++) {
                    if (more && !allLoaders && i >= LOADERS_SHOWN && !contains(query.loaders, tags[i])) {
                        continue;
                    }
                    std::string tag = tags[i];
                    rows.push_back(Filter{"downloads#loader#" + tag, Modrinth::label(tag), 0xf12e, loaderImage(tag),
                                          contains(query.loaders, tag), [tag] {
                                              flip(query.loaders, tag);
                                              query.page = 0;
                                          }});
                }
                std::function<void(float)> toggleMore;
                if (more) {
                    toggleMore = [blocked](float rowY) {
                        bool hovered = !blocked && NewUi::hit(Page::LEFT, rowY, SIDE_W, FILTER_ROW);
                        uint32_t ink = hovered ? UiTheme::textPrimary() : UiTheme::textMuted();
                        std::string text = allLoaders ? "收起" : "显示更多";
                        UiWidgets::iconBeside(Ui::glyph(allLoaders ? 0xf077 : 0xf078), Page::LEFT + 17.0f, text,
                                              UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, rowY, FILTER_ROW, ink, 9.0f);
                        UiRender::textY(text, Page::LEFT + 32.0f, rowY, FILTER_ROW, ink, UiFont::MEDIUM,
                                        UiWidgets::TEXT_CONTROL);
                        if (!blocked && NewUi::click(Page::LEFT, rowY, SIDE_W, FILTER_ROW)) {
                            UiSound::request(UiSound::TICK);
                            allLoaders = !allLoaders;
                        }
                    };
                }
                y += group("loader", "加载器", rows, y, blocked, toggleMore, more ? FILTER_ROW : 0.0f);
            }

            const std::vector<Modrinth::Category>& categories = Modrinth::categories(query.kind);
            for (const char* header : {"categories", "features", "resolutions", "performance impact"}) {
                std::vector<Filter> rows;
                for (const Modrinth::Category& category : categories) {
                    if (category.header != header) {
                        continue;
                    }
                    std::string name = category.name;
                    rows.push_back(Filter{"downloads#category#" + name, Modrinth::label(name), categoryGlyph(name), 0,
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
                Page::spinner(Page::LEFT + 17.0f, y + 13.0f, 6.0f, UiTheme::accent());
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

        float filterHeight() {
            float h = 0.0f;
            if (hasLoaders(query.kind)) {
                std::vector<std::string> tags = loaderTags(query.kind);
                bool more = tags.size() > LOADERS_SHOWN + 1;
                size_t rows = 0;
                for (size_t i = 0; i < tags.size(); i++) {
                    if (!more || allLoaders || i < LOADERS_SHOWN || contains(query.loaders, tags[i])) {
                        rows++;
                    }
                }
                h += groupHeight("loader", rows, more ? FILTER_ROW : 0.0f);
            }
            const std::vector<Modrinth::Category>& categories = Modrinth::categories(query.kind);
            for (const char* header : {"categories", "features", "resolutions", "performance impact"}) {
                size_t rows = (size_t) std::count_if(
                        categories.begin(), categories.end(),
                        [header](const Modrinth::Category& category) { return category.header == header; });
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
                query.categories.clear();
                query.loaders.clear();
                query.gameVersion.clear();
                query.client = false;
                query.server = false;
                shown = Modrinth::Results();
                allLoaders = false;
                followInstance();
            }
            views.go(VIEW_RESULTS);
        }

        void side(bool covered) {
            float y = Page::TOP;
            for (const KindInfo& info : KINDS) {
                NavItemWidget item(std::string("downloads#kind#") + info.label, Ui::glyph(info.glyph), info.label);
                item.selected(query.kind == info.kind).bounds(Page::LEFT, y, SIDE_W, NAV_ITEM_H).block(covered);
                item.paint();
                if (item.pressed && (query.kind != info.kind || views.page != VIEW_RESULTS)) {
                    switchKind(info.kind);
                }
                y += NAV_STEP;
            }
            y += 4.0f;
            UiRender::hLine(Page::LEFT + 4.0f, Page::LEFT + SIDE_W - 4.0f, y, UiTheme::divider());
            float top = y + 6.0f;
            float bottom = MainWindow::height() - 14.0f;
            Page::Scroll body("downloads#filters#" + std::to_string((int) query.kind), top, bottom, filterHeight(),
                              Page::LEFT, SIDE_W);
            bool outside = body.begin();
            shownTop = top;
            shownBottom = bottom;
            filterGroups(body.top - body.offset, outside || covered);
            body.end();
        }

        void openProject(const Modrinth::Hit& hit) {
            project = hit;
            versionFilter = query.gameVersion;
            loaderFilter = query.loaders.size() == 1 ? query.loaders.front() : std::string();
            views.go(VIEW_PROJECT);
        }

        void resultRow(const Modrinth::Hit& hit, float x, float y, float w, int index, int count, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, RESULT_H);
            float hover = UiState::motion("downloads#hit#" + hit.id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, RESULT_H, CARD_RADIUS, UiTheme::overlay(Ui::round(14 * hover)),
                                    corners(index, count));
            }
            projectIcon(hit.iconUrl, x + 12.0f, y + (RESULT_H - RESULT_ICON) * 0.5f, RESULT_ICON);
            float textX = x + 12.0f + RESULT_ICON + 12.0f;
            float right = x + w - 14.0f;
            float titleY = y + 9.0f;
            float titleCentre = titleY + 10.0f;

            std::string downloads = Modrinth::count(hit.downloads);
            float downloadsW = UiFont::width(downloads, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float downloadsX = right - downloadsW;
            UiRender::textY(downloads, downloadsX, titleY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            UiWidgets::iconBeside(Ui::glyph(0xf019), downloadsX - 9.0f, downloads, UiFont::MEDIUM,
                                  UiWidgets::TEXT_SMALL, titleY, 20.0f, UiTheme::textFaint(), 9.0f);
            float limit = downloadsX - 22.0f;

            std::string title = fitEnd(hit.title, limit - textX, UiWidgets::TEXT_BODY);
            UiRender::textY(title, textX, titleY, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float tagX = textX + UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 8.0f;
            for (const std::string& category : hit.categories) {
                float tagW = tag(Modrinth::label(category), tagX, titleCentre, limit, UiTheme::surfaceRaised(),
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
            int count = std::max(1, (total + Modrinth::PAGE_SIZE - 1) / Modrinth::PAGE_SIZE);
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
            float x = MAIN_X + (mainW() - totalW) * 0.5f;
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
            const Modrinth::Results& results = Modrinth::search(query);
            static const Modrinth::Results* shownFrom = nullptr;
            static bool shownReady = false;
            bool ready = results.status == Modrinth::Status::Ready;
            if (ready && (shownFrom != &results || !shownReady)) {
                shown = results;
            }
            shownFrom = &results;
            shownReady = ready;
            bool loading = results.status == Modrinth::Status::Loading;
            float y = Page::TOP;
            filters(y, shown.total, !shown.hits.empty() || results.status == Modrinth::Status::Ready, covered);
            float listTop = y + FILTER_H + 10.0f;
            const float pagesH = 28.0f;
            float pagesY = MainWindow::height() - 12.0f - pagesH;
            float listBottom = pagesY - 8.0f;

            if (results.status == Modrinth::Status::Failed && shown.hits.empty()) {
                message("搜索失败：" + results.error, UiTheme::danger(), listTop, listBottom, false);
                std::string retry = "重试";
                float w = Page::buttonWidth(retry);
                if (Page::button("downloads#retry", retry, MAIN_X + (mainW() - w) * 0.5f,
                                 (listTop + listBottom) * 0.5f + 20.0f, w, false, covered)) {
                    Modrinth::refresh(query);
                }
                return;
            }
            if (shown.hits.empty()) {
                if (loading) {
                    message("正在搜索 Modrinth", UiTheme::textMuted(), listTop, listBottom, true);
                } else {
                    message("没有找到符合条件的内容", UiTheme::textMuted(), listTop, listBottom, false);
                }
                return;
            }
            bool stale = loading || results.status == Modrinth::Status::Failed;
            float contentH = shown.hits.size() * RESULT_H;
            Page::Scroll body("downloads#results#" + std::to_string((int) query.kind), listTop, listBottom, contentH,
                              MAIN_X, mainW());
            bool outside = body.begin();
            bool blocked = outside || covered || stale;
            float rowsY = body.top - body.offset;
            float saved = UiRender::alphaMul;
            if (stale) {
                UiRender::alphaMul = saved * 0.5f;
            }
            card(MAIN_X, rowsY, mainW(), contentH);
            for (size_t i = 1; i < shown.hits.size(); i++) {
                UiRender::hLine(MAIN_X + 14.0f, Page::right() - 14.0f, rowsY + i * RESULT_H, UiTheme::outlineSoft());
            }
            for (size_t i = 0; i < shown.hits.size(); i++) {
                float rowY = rowsY + i * RESULT_H;
                if (rowY + RESULT_H < listTop || rowY > listBottom) {
                    continue;
                }
                resultRow(shown.hits[i], MAIN_X, rowY, mainW(), (int) i, (int) shown.hits.size(), blocked);
            }
            UiRender::alphaMul = saved;
            body.end();
            if (loading) {
                Page::spinner(MAIN_X + mainW() * 0.5f, (listTop + listBottom) * 0.5f, 9.0f, UiTheme::accent());
            }
            pages(pagesY, shown.total, covered || loading);
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

        void openModpackInstall(const Modrinth::Version& version) {
            const Modrinth::File* file = version.file();
            if (file == nullptr) {
                return;
            }
            NewInstanceScreen::Modpack setup;
            setup.title = project.title;
            setup.version = version.name.empty() ? version.number : version.name;
            setup.minecraft = version.gameVersions.empty() ? std::string() : version.gameVersions.front();
            setup.pack.minecraft = setup.minecraft;
            setup.pack.urls = Modrinth::fileUrls(file->url);
            setup.pack.sha1 = file->sha1;
            setup.pack.size = file->size;
            for (const std::string& tag : version.loaders) {
                setup.loaders.push_back(Modrinth::label(tag));
                using b_launcher_base::install::loader_type;
                if (tag == "fabric") {
                    setup.pack.loaders.push_back(loader_type::fabric);
                } else if (tag == "forge") {
                    setup.pack.loaders.push_back(loader_type::forge);
                } else if (tag == "neoforge") {
                    setup.pack.loaders.push_back(loader_type::neoforge);
                } else if (tag == "quilt") {
                    setup.pack.loaders.push_back(loader_type::quilt);
                }
            }
            NewInstanceScreen::openModpack(setup);
            MainWindow::showNewInstance();
        }

        void versionAction(const Modrinth::Version& version, float rowY, bool blocked) {
            float centreY = rowY + Page::ROW_H * 0.5f;
            float right = Page::right() - 12.0f;
            ProjectInstaller::State state = ProjectInstaller::state(version.id);
            if (state == ProjectInstaller::State::Running) {
                progressRing(right - 20.0f, centreY, ProjectInstaller::progress(version.id));
                return;
            }
            Kind kind = query.kind;
            bool intoInstance = !ProjectInstaller::folder(kind).empty();
            if ((intoInstance && ProjectInstaller::installed(version, kind, target)) ||
                (kind == Kind::Modpack && state == ProjectInstaller::State::Done)) {
                std::string done = "已安装";
                float doneW = UiFont::width(done, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(done, right - doneW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                UiWidgets::check(right - doneW - 12.0f, centreY, UiTheme::accent(), 9.0f);
                return;
            }
            std::string text = kind == Kind::Datapack ? "下载" : "安装";
            float w = Page::buttonWidth(text, 0xf019);
            float x = right - w;
            bool usable = !intoInstance || Instances::find(target) != nullptr;
            float saved = UiRender::alphaMul;
            if (!usable) {
                UiRender::alphaMul = saved * 0.45f;
            }
            bool pressed = Page::button("downloads#install#" + version.id, text, x, rowY + Page::FIELD_Y, w, false,
                                        blocked || !usable, 0xf019);
            UiRender::alphaMul = saved;
            if (!usable && !blocked && NewUi::hit(x, rowY + Page::FIELD_Y, w, Page::FIELD_H)) {
                tip("先在上方选择要安装到的实例", x, rowY + Page::FIELD_Y, w, Page::FIELD_H);
            }
            if (!pressed || !usable) {
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
                default: ProjectInstaller::install(version, kind, project.title, target); break;
            }
        }

        bool separator(char c) {
            return c == ' ' || c == '-' || c == '_' || c == ':' || c == '|' || c == '/' || c == '(' || c == '[';
        }

        char lower(char c) {
            return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
        }

        size_t sameStart(const std::string& text, const std::string& head) {
            size_t same = 0;
            while (same < head.size() && same < text.size() && lower(text[same]) == lower(head[same])) {
                same++;
            }
            return same;
        }

        std::string dropStart(const std::string& name, const std::string& head) {
            if (head.empty() || sameStart(name, head) != head.size()) {
                return name;
            }
            if (head.size() < name.size() && !separator(head.back()) && !separator(name[head.size()])) {
                return name;
            }
            size_t start = head.size();
            while (start < name.size() && separator(name[start])) {
                start++;
            }
            return name.substr(start);
        }

        const std::string& sharedPrefix(const Modrinth::Versions& versions) {
            static std::string cachedProject;
            static size_t cachedCount = (size_t) -1;
            static std::string prefix;
            if (cachedProject == project.id && cachedCount == versions.list.size()) {
                return prefix;
            }
            cachedProject = project.id;
            cachedCount = versions.list.size();
            prefix.clear();
            if (versions.list.size() >= 2) {
                prefix = versions.list.front().name;
                for (const Modrinth::Version& version : versions.list) {
                    prefix.resize(sameStart(version.name, prefix));
                    if (prefix.empty()) {
                        break;
                    }
                }
            }
            size_t cut = prefix.size();
            while (cut > 0 && !separator(prefix[cut - 1])) {
                cut--;
            }
            prefix.resize(cut);
            return prefix;
        }

        std::string shortName(const Modrinth::Version& version, const std::string& prefix) {
            std::string name = dropStart(version.name, prefix);
            name = dropStart(name, project.title);
            if (name.empty()) {
                name = version.number.empty() ? version.name : version.number;
            }
            return name;
        }

        std::vector<std::pair<uint32_t, std::string>> loaderIcons(const Modrinth::Version& version) {
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

        void versionRow(const Modrinth::Version& version, const std::string& prefix, float rowY, int index, int count,
                        size_t iconSlots, bool blocked) {
            bool hovered = !blocked && NewUi::hit(MAIN_X, rowY, mainW(), Page::ROW_H);
            float hover = UiState::motion("downloads#version#" + version.id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(MAIN_X, rowY, mainW(), Page::ROW_H, CARD_RADIUS,
                                    UiTheme::overlay(Ui::round(10 * hover)), corners(index, count));
            }
            float centreY = rowY + Page::ROW_H * 0.5f;
            float x = MAIN_X + 14.0f;
            bool overIcon = false;
            if (iconSlots > 0) {
                float iconX = x;
                float iconY = centreY - LOADER_ICON * 0.5f;
                for (const auto& [image, loader] : loaderIcons(version)) {
                    UiRender::texture(image, iconX, iconY, LOADER_ICON, LOADER_ICON);
                    if (hovered && NewUi::hit(iconX - 2.0f, iconY - 2.0f, LOADER_ICON + 4.0f, LOADER_ICON + 4.0f)) {
                        overIcon = true;
                        tip(Modrinth::label(loader), iconX, iconY, LOADER_ICON, LOADER_ICON);
                    }
                    iconX += LOADER_ICON + 4.0f;
                }
                x += iconSlots * (LOADER_ICON + 4.0f) + 6.0f;
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
            std::string name = shortName(version, prefix);
            std::string shownName = fitEnd(name, std::max(60.0f, limit - x - typeRoom), UiWidgets::TEXT_CONTROL);
            UiRender::textY(shownName, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (shownName != name && hovered && !overIcon) {
                tip(name, x, rowY + 8.0f, UiFont::width(shownName, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL),
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
            if (iconButton("downloads#back", 0xf053, "返回", MAIN_X - 4.0f, Page::TOP + 2.0f, covered)) {
                views.go(VIEW_RESULTS);
            }
            float iconX = MAIN_X + 30.0f;
            projectIcon(project.iconUrl, iconX, Page::TOP, 32.0f);
            float textX = iconX + 44.0f;
            float linkX = Page::right() - 28.0f;
            UiRender::textY(fitEnd(project.title, linkX - 8.0f - textX, Page::TITLE_SIZE), textX, Page::TOP - 2.0f,
                            22.0f, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            std::string byline = project.author.empty() ? std::string() : project.author + "  ";
            byline += Modrinth::count(project.downloads) + " 次下载";
            UiRender::textY(fitEnd(byline, linkX - 8.0f - textX, UiWidgets::TEXT_SMALL), textX, Page::TOP + 20.0f,
                            16.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (iconButton("downloads#open", 0xf35d, "在 Modrinth 上查看", linkX, Page::TOP + 2.0f, covered)) {
                Platform::openUrl("https://modrinth.com/" + std::string(Modrinth::projectType(query.kind)) + "/" +
                                  (project.slug.empty() ? project.id : project.slug));
            }

            float rowY = Page::TOP + 46.0f;
            float filterX = MAIN_X;
            if (usesInstance(query.kind)) {
                float instanceW = 150.0f;
                instancePicker(MAIN_X, rowY, instanceW, covered);
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
            const Modrinth::Versions& versions = Modrinth::versions(project.id);
            if (versions.status == Modrinth::Status::Loading) {
                message("正在获取版本列表", UiTheme::textMuted(), listTop, listBottom, true);
                return;
            }
            if (versions.status == Modrinth::Status::Failed) {
                message("获取版本失败：" + versions.error, UiTheme::danger(), listTop, listBottom, false);
                std::string retry = "重试";
                float w = Page::buttonWidth(retry);
                if (Page::button("downloads#versions#retry", retry, MAIN_X + (mainW() - w) * 0.5f,
                                 (listTop + listBottom) * 0.5f + 20.0f, w, false, covered)) {
                    Modrinth::refreshVersions(project.id);
                }
                return;
            }
            const std::string& prefix = sharedPrefix(versions);
            std::vector<const Modrinth::Version*> matching;
            for (const Modrinth::Version& version : versions.list) {
                if ((versionFilter.empty() || contains(version.gameVersions, versionFilter)) &&
                    (loaderFilter.empty() || contains(version.loaders, loaderFilter))) {
                    matching.push_back(&version);
                }
            }
            if (matching.empty()) {
                message("没有符合条件的版本", UiTheme::textMuted(), listTop, listBottom, false);
                return;
            }
            size_t iconSlots = 0;
            for (const Modrinth::Version* version : matching) {
                iconSlots = std::max(iconSlots, loaderIcons(*version).size());
            }
            float contentH = matching.size() * Page::ROW_H;
            Page::Scroll body("downloads#versions#" + project.id, listTop, listBottom, contentH, MAIN_X, mainW());
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            card(MAIN_X, y, mainW(), contentH);
            for (size_t i = 1; i < matching.size(); i++) {
                UiRender::hLine(MAIN_X + 14.0f, Page::right() - 14.0f, y + i * Page::ROW_H, UiTheme::outlineSoft());
            }
            for (size_t i = 0; i < matching.size(); i++) {
                float versionY = y + i * Page::ROW_H;
                if (versionY + Page::ROW_H < listTop || versionY > listBottom) {
                    continue;
                }
                versionRow(*matching[i], prefix, versionY, (int) i, (int) matching.size(), iconSlots, blocked);
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
            target.clear();
            saveTarget();
        }
        bool covered = NewUi::overlayCoversMouse();
        side(covered);
        views.begin();
        if (views.page == VIEW_PROJECT) {
            projectView(covered);
        } else {
            resultsView(covered);
        }
        views.end();
    }
}

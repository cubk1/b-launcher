#include "ui/imgui/screens/instances/InstanceMods.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "app/instances/ModManager.h"
#include "app/instances/Mods.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/instances/ModChanges.h"
#include "ui/imgui/screens/instances/ModGraph.h"
#include "ui/imgui/screens/instances/ModVersionPicker.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/overlay/UiOverlays.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <core/base/json.hpp>

#include "imgui.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace InstanceMods {

    namespace blb = b_launcher_base;

    namespace {
        const char* const SEARCH_OWNER = "mods#search";
        const char* const VIEW_KEY = "modsView";
        constexpr float ICON = 24.0f;
        constexpr float VERSION_W = 110.0f;
        std::string searchText;

        std::shared_ptr<TextBinding> searchValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        SEARCH_OWNER, [] { return searchText; }, [](const std::string& text) { searchText = text; },
                        "搜索模组");
                binding->limit = 60;
                return binding;
            }();
            return value;
        }

        uint32_t warningInk() {
            return UiTheme::isLight() ? 0xFFB36B00u : 0xFFF0B04Cu;
        }

        uint32_t tint(uint32_t ink) {
            return (ink & 0x00FFFFFFu) | 0x2E000000u;
        }

        float tagWidth(const std::string& text) {
            return UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
        }

        float tag(const std::string& text, const std::string& hint, float x, float rowY, uint32_t fill, uint32_t ink,
                  bool blocked) {
            float w = Page::tag(text, x, rowY, fill, ink);
            float y = rowY + (Page::ROW_H - 18.0f) * 0.5f;
            if (!hint.empty() && !blocked && NewUi::hit(x, y, w, 18.0f)) {
                Page::tooltip(hint, x, y, w, 18.0f);
            }
            return w;
        }

        std::string joined(const std::vector<std::string>& items) {
            std::string out;
            for (const std::string& item : items) {
                out += (out.empty() ? "" : "、") + item;
            }
            return out;
        }

        bool graphView() {
            return blb::json_text(Config::data(), VIEW_KEY) == "graph";
        }

        void setGraphView(bool graph) {
            if (graphView() == graph) {
                return;
            }
            Config::data()[VIEW_KEY] = graph ? "graph" : "list";
            Config::save();
        }

        void viewSwitch(float x, float y, bool blocked) {
            constexpr float SEGMENT = 30.0f;
            UiRender::roundRect(x, y, SEGMENT * 2.0f, Page::FIELD_H, 7.0f, UiTheme::surfaceRaised());
            bool graph = graphView();
            const std::pair<int, const char*> options[] = {{0xf03a, "列表"}, {0xf542, "节点"}};
            for (int i = 0; i < 2; i++) {
                float segmentX = x + (float) i * SEGMENT;
                bool active = (i == 1) == graph;
                bool hovered = !blocked && NewUi::hit(segmentX, y, SEGMENT, Page::FIELD_H);
                if (active) {
                    UiRender::roundRect(segmentX + 2.0f, y + 2.0f, SEGMENT - 4.0f, Page::FIELD_H - 4.0f, 5.0f,
                                        UiTheme::surface());
                    UiRender::outline(segmentX + 2.0f, y + 2.0f, SEGMENT - 4.0f, Page::FIELD_H - 4.0f, 5.0f,
                                      UiTheme::outlineSoft());
                }
                uint32_t ink = active ? UiTheme::textPrimary()
                        : hovered     ? UiTheme::textSecondary()
                                      : UiTheme::textMuted();
                UiWidgets::icon(Ui::glyph(options[i].first), segmentX + SEGMENT * 0.5f, y + Page::FIELD_H * 0.5f, ink,
                                11.0f);
                if (hovered) {
                    Page::tooltip(options[i].second, segmentX, y, SEGMENT, Page::FIELD_H);
                }
                if (!blocked && NewUi::click(segmentX, y, SEGMENT, Page::FIELD_H)) {
                    setGraphView(i == 1);
                }
            }
        }

        void rowMenu(const std::string& key, const Mods::Mod& mod, float x, float y) {
            std::filesystem::path file = mod.info.file;
            std::string homepage = mod.info.homepage;
            const ModManager::Change* staged = ModManager::changeFor(key, file);
            bool hasChange = staged != nullptr;
            ModManager::Change change = hasChange ? *staged : ModManager::Change();
            NewUi::openMenu(
                    "mods#menu",
                    [key, file, homepage, hasChange, change] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("切换版本", 0xf021,
                                                             [key, file] { ModVersionPicker::open(key, file); }));
                        if (hasChange) {
                            items.push_back(ActionMenuItem::make("撤销更改", 0xf0e2,
                                                                 [key, change] { ModManager::unstage(key, change); }));
                        }
                        items.push_back(
                                ActionMenuItem::make("在文件夹中显示", 0xf07c, [file] { Platform::reveal(file); }));
                        if (homepage.rfind("http", 0) == 0) {
                            items.push_back(ActionMenuItem::make("打开主页", 0xf35d,
                                                                 [homepage] { Platform::openUrl(homepage); }));
                        }
                        items.push_back(ActionMenuItem::make("删除", 0xf2ed, [key, file] { Mods::remove(key, file); })
                                                ->dangerous()
                                                ->confirmFirst());
                        return items;
                    },
                    x, y, 160.0f);
        }

        void row(const std::string& key, const Mods::Mod& mod, int index, int count, float rowY, bool blocked) {
            std::string id = "mods#row#" + std::to_string(index);
            bool hovered = !blocked && NewUi::hit(Page::left(), rowY, Page::width(), Page::ROW_H);
            Page::rowHover(id, rowY, hovered, Page::rowCorners(index, count));

            float menuX = Page::controlRight() - 24.0f;
            float toggleX = menuX - 10.0f - 30.0f;
            float limit = toggleX - 14.0f;

            float savedAlpha = UiRender::alphaMul;
            if (!mod.info.enabled) {
                UiRender::alphaMul = savedAlpha * 0.5f;
            }
            float iconX = Page::left() + 14.0f;
            float iconY = rowY + (Page::ROW_H - ICON) * 0.5f;
            if (uint32_t icon = Textures::of(mod.icon)) {
                ShaderUtil::roundTex(UiRender::transformX(iconX), UiRender::transformY(iconY),
                                     UiRender::transformW(ICON), UiRender::transformW(ICON), UiRender::transformW(6.0f),
                                     icon, 0.0f, 0.0f, 1.0f, 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                UiRender::roundRect(iconX, iconY, ICON, ICON, 6.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(0xf12e), iconX + ICON * 0.5f, iconY + ICON * 0.5f, UiTheme::textFaint(),
                                11.0f);
            }

            std::vector<std::pair<std::string, std::string>> tags;
            if (!mod.info.problem.empty()) {
                tags.push_back({"无法读取", mod.info.problem});
            }
            if (!mod.missing.empty()) {
                tags.push_back({"缺少前置", "缺少：" + joined(mod.missing)});
            }
            if (!mod.info.enabled) {
                tags.push_back({"已禁用", ""});
            }
            float tagsW = 0.0f;
            for (const auto& item : tags) {
                tagsW += tagWidth(item.first) + 4.0f;
            }
            const ModManager::Change* change = ModManager::changeFor(key, mod.info.file);
            std::string versionText =
                    change != nullptr ? mod.info.version + " → " + change->target.number : mod.info.version;
            float versionLimit = change != nullptr ? VERSION_W * 1.8f : VERSION_W;
            std::string version = UiFont::fitEnd(versionText, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, versionLimit);
            float versionW =
                    version.empty() ? 0.0f : UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 8.0f;
            float x = Page::left() + 48.0f;
            float nameRoom = std::max(60.0f, limit - x - versionW - tagsW - 4.0f);
            std::string name =
                    UiFont::fitEnd(ModManager::displayName(mod), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, nameRoom);
            float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(name, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (!blocked && NewUi::hit(x, rowY + 8.0f, nameW, Page::ROW_H - 16.0f)) {
                Page::tooltip(mod.info.file_name, x, rowY + 8.0f, nameW, Page::ROW_H - 16.0f);
            }
            x += nameW + 8.0f;
            if (!version.empty()) {
                UiRender::textY(version, x, rowY, Page::ROW_H,
                                change != nullptr ? UiTheme::accentText() : UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                x += versionW;
            }
            for (const auto& item : tags) {
                if (x + tagWidth(item.first) > limit) {
                    break;
                }
                bool problem = item.first == "无法读取";
                bool missing = item.first == "缺少前置";
                uint32_t ink = problem ? UiTheme::danger() : missing ? warningInk() : UiTheme::textSecondary();
                uint32_t fill = problem || missing ? tint(ink) : UiTheme::surfaceRaised();
                x += tag(item.first, item.second, x, rowY, fill, ink, blocked) + 4.0f;
            }
            UiRender::alphaMul = savedAlpha;

            std::filesystem::path file = mod.info.file;
            bool enabled = mod.info.enabled;
            auto flag = std::make_shared<FlagBinding>(
                    id + "#toggle", [enabled] { return enabled; },
                    [key, file](bool on) { Mods::setEnabled(key, file, on); });
            ToggleWidget toggle(id + "#toggle", flag, true);
            float toggleY = rowY + (Page::ROW_H - 18.0f) * 0.5f;
            toggle.bounds(toggleX, toggleY, 30.0f, 18.0f)
                    .hitArea(toggleX - 4.0f, rowY + 6.0f, 38.0f, Page::ROW_H - 12.0f)
                    .block(blocked);
            toggle.paint();
            if (Page::iconButton(id + "#menu", 0xf141, "更多操作", menuX, rowY + (Page::ROW_H - 28.0f) * 0.5f, blocked)) {
                rowMenu(key, mod, menuX - 132.0f, rowY + Page::ROW_H - 4.0f);
            }
        }

        void message(const std::string& text, float top, bool loading) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float lead = loading ? 24.0f : 0.0f;
            float x = Page::left() + (Page::width() - w - lead) * 0.5f;
            if (loading) {
                Page::spinner(x + 8.0f, top + 82.0f, 8.0f, UiTheme::accent());
            }
            UiRender::textY(text, x + lead, top + 70.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }
    }

    namespace {
        constexpr float FLOAT_GAP = 6.0f;
        constexpr float FLOAT_H = 40.0f;
        constexpr float SEARCH_W = 220.0f;
        constexpr float SWITCH_W = 60.0f;
        constexpr float UNFOLD_SECONDS = 0.85f;

        float unfold = 0.0f;
        int lastFrame = -10;
        std::string lastKey;

        float phase(float value, float from, float to) {
            return std::clamp((value - from) / (to - from), 0.0f, 1.0f);
        }

        float easeInOut(float value) {
            float back = -2.0f * value + 2.0f;
            return value < 0.5f ? 4.0f * value * value * value : 1.0f - back * back * back * 0.5f;
        }

        ModGraph::Area mix(const ModGraph::Area& from, const ModGraph::Area& to, float t) {
            return ModGraph::Area{UiRender::lerp(from.x, to.x, t), UiRender::lerp(from.y, to.y, t),
                                  UiRender::lerp(from.w, to.w, t), UiRender::lerp(from.h, to.h, t)};
        }

        void counts(const Mods::List& list, float left, float right, float y, float h) {
            if (list.mods.empty()) {
                return;
            }
            int enabled = 0;
            int lacking = 0;
            for (const Mods::Mod& mod : list.mods) {
                enabled += mod.info.enabled ? 1 : 0;
                lacking += mod.missing.empty() ? 0 : 1;
            }
            std::string total =
                    "共 " + std::to_string(list.mods.size()) + " 个，已启用 " + std::to_string(enabled) + " 个";
            std::string lack = lacking > 0 ? "，" + std::to_string(lacking) + " 个缺少前置" : std::string();
            float lackW = UiFont::width(lack, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float totalW = UiFont::width(total, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (totalW + lackW > right - left) {
                return;
            }
            float x = right - lackW - totalW;
            UiRender::textY(total, x, y, h, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (!lack.empty()) {
                UiRender::textY(lack, x + totalW, y, h, warningInk(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
        }

        void addMods(const Instances::Entry& entry) {
            std::filesystem::path folder = entry.gameDirectory / "mods";
            std::vector<std::filesystem::path> files =
                    Platform::pickFiles(folder, L"添加模组", L"模组 (*.jar)", L"*.jar");
            Mods::add(entry.key, files, "mods", "模组");
        }

        void openFolder(const Instances::Entry& entry) {
            std::filesystem::path folder = entry.gameDirectory / "mods";
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            Platform::open(folder);
        }

        ModGraph::Area floatingArea() {
            return ModGraph::Area{Page::left() - 8.0f, MainWindow::TITLE_HEIGHT + FLOAT_GAP, Page::width() + 16.0f,
                                  FLOAT_H};
        }

        float barRowY() {
            return Page::TOP + 40.0f;
        }

        float switchCentreX() {
            return Page::left() + SEARCH_W + 8.0f + SWITCH_W * 0.5f;
        }

        float switchCentreY() {
            return barRowY() + Page::FIELD_H * 0.5f;
        }

        bool toolbar(const Instances::Entry& entry, const Mods::List& list, float lift, bool blocked) {
            ModGraph::Area bar = floatingArea();
            float rowY = barRowY();
            ModGraph::Area dock{Page::left() - 8.0f, Page::TOP - 8.0f, Page::width() + 16.0f,
                                rowY + Page::FIELD_H + 8.0f - (Page::TOP - 8.0f)};
            if (lift > 0.001f) {
                ModGraph::Area back = mix(dock, bar, lift);
                float saved = UiRender::alphaMul;
                UiRender::alphaMul = saved * lift;
                UiRender::shadow(back.x, back.y, back.w, back.h, 12.0f, 14.0f, 5.0f, UiTheme::shadow());
                UiRender::blur(back.x, back.y, back.w, back.h, 12.0f, 8, true);
                UiRender::roundRect(back.x, back.y, back.w, back.h, 12.0f,
                                    (UiTheme::surface() & 0x00FFFFFFu) | 0xD9000000u);
                UiRender::outline(back.x, back.y, back.w, back.h, 12.0f, UiTheme::outlineSoft());
                UiRender::alphaMul = saved;
            }
            float cy = bar.y + bar.h * 0.5f;
            bool back = Page::iconButton("mods#back", 0xf053, "返回",
                                         UiRender::lerp(Page::left() - 4.0f, bar.x + 6.0f, lift),
                                         UiRender::lerp(Page::TOP - 2.0f, cy - 14.0f, lift), blocked);
            const char* title = "模组";
            float titleW = UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE);
            float titleX = UiRender::lerp(Page::left() + 30.0f, bar.x + 38.0f, lift);
            float titleY = UiRender::lerp(Page::TOP, bar.y + (bar.h - 24.0f) * 0.5f, lift);
            UiRender::textY(title, titleX, titleY, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);

            float refreshX = UiRender::lerp(Page::right() - 28.0f, bar.x + bar.w - 6.0f - 28.0f, lift);
            float iconY = UiRender::lerp(Page::TOP - 2.0f, cy - 14.0f, lift);
            if (Page::iconButton("mods#refresh", 0xf2f1, "刷新", refreshX, iconY, blocked)) {
                Mods::scan(entry.key);
            }
            float folderX = refreshX - 32.0f;
            if (Page::iconButton("mods#folder", 0xf07c, "打开模组文件夹", folderX, iconY, blocked)) {
                openFolder(entry);
            }
            std::string add = "添加";
            float addW = Page::buttonWidth(add, 0xf067);
            float addX = folderX - 8.0f - addW;
            if (Page::button("mods#add", add, addX, UiRender::lerp(Page::TOP + 1.0f, cy - Page::FIELD_H * 0.5f, lift),
                             addW, false, blocked, 0xf067)) {
                addMods(entry);
            }
            if (lift < 0.999f) {
                float saved = UiRender::alphaMul;
                UiRender::alphaMul = saved * (1.0f - std::min(1.0f, lift * 2.0f));
                float nameX = Page::left() + 30.0f + titleW + 10.0f;
                UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                               std::max(0.0f, addX - 12.0f - nameX)),
                                nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::alphaMul = saved;
            }

            float searchX = UiRender::lerp(Page::left(), bar.x + 38.0f + titleW + 14.0f, lift);
            float fieldY = UiRender::lerp(rowY, cy - Page::FIELD_H * 0.5f, lift);
            TextFieldWidget field(searchValue());
            field.box(searchX, fieldY, SEARCH_W, Page::FIELD_H)
                    .bounds(searchX, fieldY, SEARCH_W, Page::FIELD_H)
                    .block(blocked);
            field.paint();
            float switchX = searchX + SEARCH_W + 8.0f;
            viewSwitch(switchX, fieldY, blocked);
            counts(list, switchX + SWITCH_W + 12.0f, UiRender::lerp(Page::right(), addX - 14.0f, lift),
                   UiRender::lerp(rowY, bar.y, lift), UiRender::lerp(Page::FIELD_H, bar.h, lift));
            return back;
        }

        std::vector<const Mods::Mod*> filtered(const Mods::List& list, const std::string& query) {
            std::vector<const Mods::Mod*> shown;
            for (const Mods::Mod& mod : list.mods) {
                if (ModManager::matches(mod, query)) {
                    shown.push_back(&mod);
                }
            }
            return shown;
        }

        std::vector<ModGraph::Row> visibleRows(const std::string& key, const Mods::List& list, const std::string& query,
                                               float top, float bottom) {
            std::vector<ModGraph::Row> rows;
            std::vector<const Mods::Mod*> shown = filtered(list, query);
            float y0 = top - Page::scrollOffset("mods#body#" + key);
            for (size_t i = 0; i < shown.size(); i++) {
                float rowY = y0 + (float) i * Page::ROW_H;
                if (rowY + Page::ROW_H <= top || rowY >= bottom) {
                    continue;
                }
                rows.push_back(ModGraph::Row{shown[i]->info.file,
                                             ModGraph::Area{Page::left(), rowY, Page::width(), Page::ROW_H}});
            }
            return rows;
        }

        void body(const std::string& key, const Mods::List& list, const std::string& query, float top, float bottom,
                  bool covered) {
            if (list.mods.empty()) {
                if (list.loading) {
                    message("正在读取模组…", top, true);
                } else {
                    message("还没有模组，点击“添加”或把 .jar 文件拖进窗口", top, false);
                }
                return;
            }
            if (!ModManager::namesReady(list)) {
                message("正在读取模组…", top, true);
                return;
            }
            std::vector<const Mods::Mod*> shown = filtered(list, query);
            if (shown.empty()) {
                message("没有找到“" + query + "”", top, false);
                return;
            }
            int count = (int) shown.size();
            Page::Scroll scroll("mods#body#" + key, top, bottom, count * Page::ROW_H);
            bool outside = scroll.begin();
            bool blocked = outside || covered;
            float y0 = scroll.top - scroll.offset;
            Page::card(y0, count);
            for (int i = 0; i < count; i++) {
                float rowY = y0 + i * Page::ROW_H;
                if (Page::shown(rowY)) {
                    row(key, *shown[i], i, count, rowY, blocked);
                }
            }
            scroll.end();
        }
    }

    bool graphShown(const std::string& key) {
        return key == lastKey && ImGui::GetFrameCount() - lastFrame <= 1 && unfold >= 1.0f && graphView();
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        const Mods::List& list = Mods::of(key);
        ModManager::prepare(key);
        std::string query = Ui::trim(NewUi::textEditing == SEARCH_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : searchText);
        bool graph = graphView();
        MainWindow::claimSearch();
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false) && !NewUi::popupOpen &&
            UiOverlays::isEmpty() && NewUi::textEditing != SEARCH_OWNER) {
            NewUi::beginTextEdit(searchValue());
        }
        int frameNumber = ImGui::GetFrameCount();
        if (frameNumber - lastFrame > 1 || lastKey != key) {
            unfold = 0.0f;
        }
        lastFrame = frameNumber;
        lastKey = key;
        float target = graph ? 1.0f : 0.0f;
        float step = io.DeltaTime / UNFOLD_SECONDS;
        unfold = target > unfold ? std::min(target, unfold + step) : std::max(target, unfold - step);
        if (unfold != target) {
            UiState::keepAnimating();
        }
        float expand = easeInOut(phase(unfold, 0.0f, 0.45f));
        float lift = easeInOut(phase(unfold, 0.05f, 0.55f));
        bool listReady = unfold <= 0.0f && !graph;
        bool graphReady = unfold >= 1.0f && graph;
        bool still = listReady || graphReady;

        float top = barRowY() + Page::FIELD_H + 12.0f;
        float bottom = MainWindow::height() - 14.0f;
        float changesH = ModChanges::height(key);
        float listBottom = changesH > 0.0f ? bottom - changesH - 10.0f : bottom;
        bool back = false;
        if (unfold <= 0.0f) {
            body(key, list, query, top, listBottom, covered || !listReady);
        }
        if (unfold > 0.0f) {
            float sideW = MainWindow::sidebarWidth();
            ModGraph::Area from{Page::left(), top, Page::width(), listBottom - top};
            ModGraph::Area to{sideW, 0.0f, MainWindow::width() - sideW, MainWindow::height()};
            ModGraph::Frame frame;
            frame.clip = mix(from, to, expand);
            frame.layout = to;
            frame.chrome = 1.0f - expand;
            frame.grid = phase(unfold, 0.15f, 0.65f);
            frame.nodes = phase(unfold, 0.35f, 1.0f);
            frame.morph = unfold;
            frame.focused = true;
            frame.focusX = switchCentreX();
            frame.focusY = switchCentreY();
            frame.rows = visibleRows(key, list, query, top, listBottom);
            ModGraph::Area bar = floatingArea();
            frame.top = bar.y + bar.h + 8.0f;
            frame.bottom = changesH > 0.0f ? MainWindow::height() - listBottom + 8.0f : 0.0f;
            frame.inputTop = MainWindow::TITLE_HEIGHT;
            frame.holes.push_back(bar);
            if (changesH > 0.0f) {
                frame.holes.push_back(ModGraph::Area{Page::left(), listBottom + 10.0f, Page::width(), changesH});
            }
            ModGraph::draw(entry, query, frame, covered || !graphReady);
        }
        if (toolbar(entry, list, lift, covered || !still) && still) {
            back = true;
        }
        if (changesH > 0.0f) {
            ModChanges::draw(key, Page::left(), listBottom + 10.0f, Page::width(), covered || !still);
        }
        return back;
    }
}

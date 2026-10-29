#include "ui/imgui/screens/instances/InstanceMods.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "app/instances/Mods.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Platform.h"

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/IconButtonWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace InstanceMods {

    namespace {
        const char* const SEARCH_OWNER = "mods#search";
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

        char lower(char c) {
            return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
        }

        bool contains(const std::string& text, const std::string& query) {
            return std::search(text.begin(), text.end(), query.begin(), query.end(),
                               [](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

        bool matches(const Mods::Mod& mod, const std::string& query) {
            return query.empty() || contains(mod.info.name, query) || contains(mod.info.id, query) ||
                    contains(mod.info.file_name, query);
        }

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
            float w = tagWidth(text);
            float y = rowY + (Page::ROW_H - 18.0f) * 0.5f;
            UiRender::roundRect(x, y, w, 18.0f, 6.0f, fill);
            UiRender::textY(text, x + 6.0f, y, 18.0f, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!hint.empty() && !blocked && NewUi::hit(x, y, w, 18.0f)) {
                tip(hint, x, y, w, 18.0f);
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

        void rowMenu(const std::string& key, const Mods::Mod& mod, float x, float y) {
            std::filesystem::path file = mod.info.file;
            std::string homepage = mod.info.homepage;
            NewUi::openMenu(
                    "mods#menu",
                    [key, file, homepage] {
                        MenuItems items;
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
            Page::rowHover(id, rowY, hovered, (index == 0 ? 1 | 2 : 0) | (index == count - 1 ? 4 | 8 : 0));

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
                UiRender::texture(icon, iconX, iconY, ICON, ICON);
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
            std::string version = UiFont::fitEnd(mod.info.version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, VERSION_W);
            float versionW =
                    version.empty() ? 0.0f : UiFont::width(version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 8.0f;
            float x = Page::left() + 48.0f;
            float nameRoom = std::max(60.0f, limit - x - versionW - tagsW - 4.0f);
            std::string name = UiFont::fitEnd(mod.info.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, nameRoom);
            float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(name, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (!blocked && NewUi::hit(x, rowY + 8.0f, nameW, Page::ROW_H - 16.0f)) {
                tip(mod.info.file_name, x, rowY + 8.0f, nameW, Page::ROW_H - 16.0f);
            }
            x += nameW + 8.0f;
            if (!version.empty()) {
                UiRender::textY(version, x, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
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
            if (iconButton(id + "#menu", 0xf141, "更多操作", menuX, rowY + (Page::ROW_H - 28.0f) * 0.5f, blocked)) {
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

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        bool back = iconButton("mods#back", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, covered);
        const char* title = "模组";
        Page::title(title, Page::left() + 30.0f, Page::TOP);
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;

        float refreshX = Page::right() - 28.0f;
        if (iconButton("mods#refresh", 0xf2f1, "刷新", refreshX, Page::TOP - 2.0f, covered)) {
            Mods::rescan(key);
        }
        float folderX = refreshX - 32.0f;
        std::filesystem::path folder = entry.gameDirectory / "mods";
        if (iconButton("mods#folder", 0xf07c, "打开模组文件夹", folderX, Page::TOP - 2.0f, covered)) {
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            Platform::open(folder);
        }
        std::string add = "添加";
        float addW = Page::buttonWidth(add, 0xf067);
        float addX = folderX - 8.0f - addW;
        if (Page::button("mods#add", add, addX, Page::TOP + 1.0f, addW, false, covered, 0xf067)) {
            std::vector<std::filesystem::path> files =
                    Platform::pickFiles(folder, L"添加模组", L"模组 (*.jar)", L"*.jar");
            Mods::add(key, files, "mods", "模组");
        }
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                       std::max(0.0f, addX - 12.0f - nameX)),
                        nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

        const Mods::List& list = Mods::of(key);
        float barY = Page::TOP + 40.0f;
        const float searchW = 220.0f;
        TextFieldWidget search(searchValue());
        search.box(Page::left(), barY, searchW, Page::FIELD_H)
                .bounds(Page::left(), barY, searchW, Page::FIELD_H)
                .block(covered);
        search.paint();
        std::string query = Ui::trim(NewUi::textEditing == SEARCH_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : searchText);
        int enabled = 0;
        int lacking = 0;
        for (const Mods::Mod& mod : list.mods) {
            enabled += mod.info.enabled ? 1 : 0;
            lacking += mod.missing.empty() ? 0 : 1;
        }
        if (!list.mods.empty()) {
            std::string counts =
                    "共 " + std::to_string(list.mods.size()) + " 个，已启用 " + std::to_string(enabled) + " 个";
            std::string lack = lacking > 0 ? "，" + std::to_string(lacking) + " 个缺少前置" : std::string();
            float lackW = UiFont::width(lack, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float countsW = UiFont::width(counts, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float countsX = Page::right() - lackW - countsW;
            UiRender::textY(counts, countsX, barY, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (!lack.empty()) {
                UiRender::textY(lack, countsX + countsW, barY, Page::FIELD_H, warningInk(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
        }

        float top = barY + Page::FIELD_H + 12.0f;
        float bottom = MainWindow::height() - 14.0f;
        if (list.mods.empty()) {
            if (list.loading) {
                message("正在读取模组…", top, true);
            } else {
                message("还没有模组，点击“添加”或把 .jar 文件拖进窗口", top, false);
            }
            return back;
        }
        std::vector<const Mods::Mod*> shown;
        for (const Mods::Mod& mod : list.mods) {
            if (matches(mod, query)) {
                shown.push_back(&mod);
            }
        }
        if (shown.empty()) {
            message("没有找到“" + query + "”", top, false);
            return back;
        }
        int count = (int) shown.size();
        Page::Scroll body("mods#body#" + key, top, bottom, count * Page::ROW_H);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y0 = body.top - body.offset;
        Page::card(y0, count);
        for (int i = 0; i < count; i++) {
            float rowY = y0 + i * Page::ROW_H;
            if (Page::shown(rowY)) {
                row(key, *shown[i], i, count, rowY, blocked);
            }
        }
        body.end();
        return back;
    }
}

#include "ui/imgui/screens/instances/ModVersionPicker.h"
#include "app/instances/ModManager.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/Page.h"

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

#include <core/base/strings.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace ModVersionPicker {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float DIALOG_W = 480.0f;
        constexpr float DIALOG_H = 460.0f;
        constexpr float PAD = 20.0f;
        constexpr float ROW = 34.0f;
        const char* const DIALOG = "mods#versions";

        struct Search {
            std::string owner;
            std::string text;
            std::shared_ptr<TextBinding> binding;
        };

        std::map<std::string, Search> searches;
        std::string pickerKey;
        std::filesystem::path pickerFile;

        Search& searchOf(const std::string& id) {
            auto found = searches.find(id);
            if (found != searches.end()) {
                return found->second;
            }
            Search& entry = searches[id];
            entry.owner = id + "#search";
            entry.binding = std::make_shared<TextBinding>(
                    entry.owner, [id] { return searches[id].text; },
                    [id](const std::string& text) { searches[id].text = text; }, "搜索版本");
            entry.binding->limit = 40;
            return entry;
        }

        uint32_t tint(uint32_t ink) {
            return (ink & 0x00FFFFFFu) | 0x2E000000u;
        }

        uint32_t warningInk() {
            return UiTheme::isLight() ? 0xFFB36B00u : 0xFFF0B04Cu;
        }

        float pill(const std::string& text, float x, float cy, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            UiRender::roundRect(x, cy - 9.0f, w, 18.0f, 6.0f, fill);
            UiRender::textY(text, x + 6.0f, cy - 9.0f, 18.0f, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        void message(const std::string& text, float x, float y, float w, bool loading) {
            std::string fitted = UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 40.0f);
            float textW = UiFont::width(fitted, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float lead = loading ? 24.0f : 0.0f;
            float left = x + (w - textW - lead) * 0.5f;
            if (loading) {
                Page::spinner(left + 8.0f, y + 12.0f, 8.0f, UiTheme::accent());
            }
            UiRender::textY(fitted, left + lead, y, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        const Mods::Mod* current() {
            for (const Mods::Mod& mod : Mods::of(pickerKey).mods) {
                if (mod.info.file == pickerFile) {
                    return &mod;
                }
            }
            return nullptr;
        }

        void body(float x, float y, float w, float h) {
            const Mods::Mod* mod = current();
            if (mod == nullptr) {
                Dialog::close();
                return;
            }
            const std::string title = "切换版本";
            UiRender::textY(title, x + PAD, y + 16.0f, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            float nameX = x + PAD + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            UiRender::textY(
                    UiFont::fitEnd(mod->info.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, x + w - PAD - nameX), nameX,
                    y + 16.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (list(DIALOG, pickerKey, *mod, x + PAD, y + 54.0f, w - 2.0f * PAD, h - 54.0f - 14.0f, false)) {
                Dialog::close();
            }
        }
    }

    bool list(const std::string& id, const std::string& key, const Mods::Mod& mod, float x, float y, float w, float h,
              bool blocked) {
        Search& search = searchOf(id);
        std::string query =
                Ui::trim(NewUi::textEditing == search.owner ? UiInput::get(NewUi::INPUT_TEXT) : search.text);
        float top = y + Page::FIELD_H + 8.0f;
        float bottom = y + h;
        float middle = top + (bottom - top) * 0.35f;
        auto searchField = [&](float width) {
            TextFieldWidget field(search.binding);
            field.box(x, y, width, Page::FIELD_H).bounds(x, y, width, Page::FIELD_H).block(blocked);
            field.paint();
        };

        const ModManager::Remote* remote = ModManager::remote(mod);
        if (remote == nullptr) {
            searchField(w);
            if (ModManager::identifying(key)) {
                message("正在从 Modrinth 识别…", x, middle, w, true);
            } else if (!ModManager::identifyError(key).empty()) {
                message("无法连接 Modrinth：" + ModManager::identifyError(key), x, middle, w, false);
            } else {
                message("没有在 Modrinth 上找到这个模组", x, middle, w, false);
            }
            return false;
        }
        const Addons::Versions& versions = ModManager::versions(key, remote->projectId);
        if (versions.status == Addons::Status::Loading) {
            searchField(w);
            message("正在获取版本…", x, middle, w, true);
            return false;
        }
        if (versions.status == Addons::Status::Failed) {
            searchField(w);
            message("无法获取版本：" + versions.error, x, middle - 18.0f, w, false);
            float retryW = Page::buttonWidth("重试", 0xf2f1);
            if (Page::button(id + "#retry", "重试", x + (w - retryW) * 0.5f, middle + 14.0f, retryW, false, blocked,
                             0xf2f1)) {
                ModManager::refreshVersions(key, remote->projectId);
            }
            return false;
        }
        std::vector<const Addons::Version*> shown;
        for (const Addons::Version& version : versions.list) {
            if (query.empty() || blb::icontains(version.number, query) || blb::icontains(version.name, query)) {
                shown.push_back(&version);
            }
        }
        if (shown.empty()) {
            searchField(w);
            message(versions.list.empty() ? "没有适用于 " + ModManager::gameVersion(key) + " 的版本" : "没有匹配的版本",
                    x, middle, w, false);
            return false;
        }
        int count = (int) shown.size();
        float listW = (float) count * ROW > bottom - top ? w - 10.0f : w;
        searchField(listW);

        const ModManager::Change* change = ModManager::changeFor(key, mod.info.file);
        std::string chosen = change != nullptr ? change->target.id : remote->versionId;
        Page::Scroll scroll(id + "#scroll", top, bottom, count * ROW, x, listW);
        bool outside = scroll.begin();
        const Addons::Version* picked = nullptr;
        for (int i = 0; i < count; i++) {
            float rowY = top - scroll.offset + i * ROW;
            if (rowY + ROW < top || rowY > bottom) {
                continue;
            }
            const Addons::Version& version = *shown[i];
            bool selected = version.id == chosen;
            bool hovered = !outside && !blocked && NewUi::hit(x, rowY, listW, ROW);
            float lit = UiState::motion(id + "#" + version.id, hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x, rowY + 2.0f, listW, ROW - 4.0f, 8.0f, UiTheme::overlay(Ui::round(14 * lit)));
            }
            if (selected) {
                UiRender::outline(x, rowY + 2.0f, listW, ROW - 4.0f, 8.0f, UiTheme::accent(), 0xF, 1.4f);
            }
            float cy = rowY + ROW * 0.5f;
            float right = x + listW - 10.0f;
            if (selected) {
                UiWidgets::icon(Ui::glyph(0xf00c), right - 6.0f, cy, UiTheme::accentText(), 11.0f);
                right -= 22.0f;
            }
            std::string date = version.published.substr(0, std::min<size_t>(10, version.published.size()));
            float dateW = UiFont::width(date, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(date, right - dateW, rowY, ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float textX = x + 10.0f;
            bool prerelease = version.type == "beta" || version.type == "alpha";
            bool installed = version.id == remote->versionId;
            float tagsW = (prerelease ? 58.0f : 0.0f) + (installed ? 42.0f : 0.0f);
            std::string number = UiFont::fitEnd(version.number, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                std::max(40.0f, right - dateW - 12.0f - textX - tagsW));
            UiRender::textY(number, textX, rowY, ROW, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float tagX = textX + UiFont::width(number, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
            if (prerelease) {
                tagX += pill(version.type == "beta" ? "测试版" : "预览版", tagX, cy, tint(warningInk()), warningInk()) +
                        4.0f;
            }
            if (installed) {
                pill("当前", tagX, cy, UiTheme::surfaceRaised(), UiTheme::textSecondary());
            }
            if (!outside && !blocked && NewUi::click(x, rowY, listW, ROW)) {
                picked = &version;
            }
        }
        scroll.end();
        if (picked == nullptr) {
            return false;
        }
        Addons::Version target = *picked;
        ModManager::stage(key, mod.info.file, target);
        return true;
    }

    void open(const std::string& key, const std::filesystem::path& file) {
        pickerKey = key;
        pickerFile = file;
        Dialog::open(DIALOG, DIALOG_W, DIALOG_H, body);
    }
}

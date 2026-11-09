#include "ui/imgui/screens/instances/InstanceShortcut.h"
#include "ui/imgui/screens/instances/InstancePage.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/instances/Shortcuts.h"
#include "app/system/Platform.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace InstanceShortcut {

    namespace {
        constexpr float SELECT_W = 180.0f;
        constexpr float PREVIEW = 22.0f;
        constexpr int PREVIEW_PIXELS = 64;
        // In the order of Shortcuts::Icon.
        const char* const ICON_LABELS[] = {"启动器图标", "实例图标", "自定义图片"};
        struct Preview {
            std::filesystem::path file;
            uint32_t texture = 0;
        };

        Preview preview;

        uint32_t customTexture(const std::filesystem::path& file) {
            if (file == preview.file) {
                return preview.texture;
            }
            if (preview.texture != 0) {
                GLuint old = preview.texture;
                UiGL::DeleteTextures(1, &old);
                preview.texture = 0;
            }
            preview.file = file;
            std::vector<unsigned char> rgba = Shortcuts::pixels(file, PREVIEW_PIXELS);
            if (rgba.size() == (size_t) PREVIEW_PIXELS * PREVIEW_PIXELS * 4) {
                preview.texture = UiImages::upload(rgba.data(), PREVIEW_PIXELS, PREVIEW_PIXELS);
            }
            return preview.texture;
        }

        void iconRow(const Instances::Entry& entry, float rowY, bool blocked) {
            const std::string key = entry.key;
            Page::label("图标", rowY);
            float selectX = Page::controlRight() - SELECT_W;
            float previewX = selectX - 12.0f - PREVIEW;
            float previewY = rowY + (Page::ROW_H - PREVIEW) * 0.5f;
            const Shortcuts::Draft& draft = Shortcuts::draft(key);
            if (draft.icon == Shortcuts::Icon::Launcher) {
                Logo::draw(previewX, previewY, PREVIEW);
            } else if (draft.icon == Shortcuts::Icon::Instance) {
                if (uint32_t texture = InstancePage::icon(entry)) {
                    UiRender::texture(texture, previewX, previewY, PREVIEW, PREVIEW);
                }
            } else if (!draft.custom.empty()) {
                if (uint32_t texture = customTexture(draft.custom)) {
                    UiRender::texture(texture, previewX, previewY, PREVIEW, PREVIEW);
                }
            }
            auto choice = std::make_shared<ModeBinding>(
                    "shortcut#icon#" + key,
                    [key] { return std::string(ICON_LABELS[(int) Shortcuts::draft(key).icon]); },
                    [key](const std::string& label) {
                        for (int i = 0; i < (int) std::size(ICON_LABELS); i++) {
                            if (label == ICON_LABELS[i]) {
                                Shortcuts::draft(key).icon = (Shortcuts::Icon) i;
                            }
                        }
                    },
                    [] { return std::vector<std::string>(std::begin(ICON_LABELS), std::end(ICON_LABELS)); });
            SelectWidget widget("shortcut#icon#" + key, choice);
            widget.bounds(selectX, rowY + Page::FIELD_Y, SELECT_W, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void fileRow(const std::string& key, float rowY, bool blocked) {
            const std::string label = "图标文件";
            Page::label(label, rowY);
            Shortcuts::Draft& draft = Shortcuts::draft(key);
            float pickX = Page::controlRight() - 27.0f;
            if (Page::iconButton("shortcut#pick#" + key, 0xf07c, "选择图标", pickX, rowY + 6.0f, blocked)) {
                std::filesystem::path start = draft.custom.empty() ? Platform::userDirectory() : draft.custom;
                if (std::optional<std::filesystem::path> picked = Platform::pickIcon(start)) {
                    draft.custom = *picked;
                }
            }
            float right = pickX - 8.0f;
            float labelRight =
                    Page::left() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            std::string shown = draft.custom.empty()
                    ? std::string("未选择")
                    : UiFont::fitStart(Platform::display(draft.custom), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                       std::max(0.0f, right - labelRight));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void headlessRow(const std::string& key, float rowY, bool blocked) {
            Page::label("不显示启动器窗口", rowY);
            std::string id = "shortcut#headless#" + key;
            auto flag = std::make_shared<FlagBinding>(
                    id, [key] { return Shortcuts::draft(key).headless; },
                    [key](bool on) { Shortcuts::draft(key).headless = on; });
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::left(), rowY, Page::width(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        bool back = Page::iconButton("shortcut#back", 0xf053, "返回", Page::left() - 4.0f, Page::top() - 2.0f, covered);
        const char* title = "生成快捷方式";
        Page::title(title, Page::left() + 30.0f, Page::top());
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::right() - nameX),
                        nameX, Page::top(), 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

        bool custom = Shortcuts::draft(key).icon == Shortcuts::Icon::Custom;
        int rows = custom ? 3 : 2;
        float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
        Page::Scroll body("shortcut#body#" + key, Page::top() + 40.0f, buttonTop - 14.0f,
                          Page::CAPTION_H + rows * Page::ROW_H);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y = body.top - body.offset;
        Page::caption("快捷方式", y);
        float rowY = y + Page::CAPTION_H;
        Page::card(rowY, rows);
        iconRow(entry, rowY, blocked);
        rowY += Page::ROW_H;
        if (custom) {
            fileRow(key, rowY, blocked);
            rowY += Page::ROW_H;
        }
        headlessRow(key, rowY, blocked);
        body.end();

        float w = std::max(88.0f, Page::buttonWidth("生成", 0xf108));
        if (Page::button("shortcut#go", "生成", Page::right() - w, buttonTop, w, true, covered, 0xf108) &&
            Shortcuts::create(key)) {
            back = true;
        }
        return back;
    }
}

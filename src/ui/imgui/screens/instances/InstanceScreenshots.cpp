#include "ui/imgui/screens/instances/InstanceScreenshots.h"
#include "app/instances/Screenshots.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace InstanceScreenshots {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float CELL_W = 190.0f;
        constexpr float CELL_GAP = 14.0f;
        constexpr float CELL_RADIUS = 10.0f;
        constexpr float NAME_H = 42.0f;
        constexpr float CELL_PAD = 10.0f;
        constexpr float ASPECT = 9.0f / 16.0f;
        constexpr float VIEWER_TOOLBAR_H = 56.0f;

        std::filesystem::path viewingPath;
        std::string viewingKey;

        float imageHeight() {
            return CELL_W * ASPECT;
        }

        float cellHeight() {
            return imageHeight() + NAME_H;
        }

        std::string sizeOf(uint64_t bytes) {
            char text[32];
            if (bytes >= (1ull << 20)) {
                std::snprintf(text, sizeof(text), "%.1f MB", bytes / 1048576.0);
            } else if (bytes >= 1024) {
                std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
            } else {
                std::snprintf(text, sizeof(text), "%llu B", (unsigned long long) bytes);
            }
            return text;
        }

        std::string dateOf(int64_t mtime) {
            auto ticks = blb::fs::mtime_ticks_per_second();
            if (ticks <= 0) {
                return {};
            }
            double diff = (double) (blb::fs::mtime_now() - mtime) / ticks;
            char text[48];
            if (diff < 60) {
                return "刚刚";
            }
            if (diff < 3600) {
                std::snprintf(text, sizeof(text), "%d 分钟前", (int) (diff / 60));
                return text;
            }
            if (diff < 86400) {
                std::snprintf(text, sizeof(text), "%d 小时前", (int) (diff / 3600));
                return text;
            }
            if (diff < 7 * 86400) {
                std::snprintf(text, sizeof(text), "%d 天前", (int) (diff / 86400));
                return text;
            }
            auto changed = std::chrono::system_clock::now() -
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double>(diff));
            auto seconds = std::chrono::system_clock::to_time_t(changed);
            std::tm local{};
            if (localtime_s(&local, &seconds) != 0) {
                return {};
            }
            std::snprintf(text, sizeof(text), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
            return text;
        }

        void closeViewer() {
            viewingPath.clear();
            Screenshots::forgetFullImage();
        }

        void rowMenu(const std::string& key, const Screenshots::Entry& entry, float x, float y) {
            std::filesystem::path path = entry.path;
            NewUi::openMenu(
                    "screenshots#menu",
                    [key, path] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("查看", 0xf06e, [key, path] { open(key, path); }));
                        items.push_back(ActionMenuItem::make("复制", 0xf0c5, [path] { Screenshots::copy(path); }));
                        items.push_back(
                                ActionMenuItem::make("在文件夹中显示", 0xf07c, [path] { Platform::reveal(path); }));
                        items.push_back(
                                ActionMenuItem::make("删除", 0xf2ed, [key, path] { Screenshots::remove(key, path); })
                                        ->dangerous()
                                        ->confirmFirst());
                        return items;
                    },
                    x, y, 160.0f);
        }

        void image(const std::filesystem::path& path, int64_t modified, float x, float y, float w, float h) {
            UiRender::roundRect(x, y, w, h, CELL_RADIUS, UiTheme::surfaceRaised());
            std::shared_ptr<const Pixels> thumb = Screenshots::thumbnail(path, modified);
            if (uint32_t texture = Textures::of(thumb)) {
                float imageW = (float) thumb->width;
                float imageH = (float) thumb->height;
                float scale = std::min(w / imageW, h / imageH);
                float drawW = imageW * scale;
                float drawH = imageH * scale;
                float ix = x + (w - drawW) * 0.5f;
                float iy = y + (h - drawH) * 0.5f;
                ShaderUtil::roundTex(UiRender::transformX(ix), UiRender::transformY(iy), UiRender::transformW(drawW),
                                     UiRender::transformW(drawH), UiRender::transformW(CELL_RADIUS), texture, 0.0f,
                                     0.0f, 1.0f, 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                Page::spinner(x + w * 0.5f, y + h * 0.5f, 7.0f, UiTheme::accent());
            }
        }

        void cell(const std::string& key, const Screenshots::Entry& entry, int index, float x, float y, bool blocked) {
            float w = CELL_W;
            float imgH = imageHeight();
            bool hovered = !blocked && NewUi::hit(x, y, w, cellHeight());
            float hover = UiState::motion("screenshots#" + std::to_string(index), hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x - 4.0f, y - 4.0f, w + 8.0f, cellHeight() + 8.0f, CELL_RADIUS + 4.0f,
                                    UiTheme::overlay(Ui::round(14 * hover)));
            }
            image(entry.path, entry.modified, x, y, w, imgH);
            float textX = x + CELL_PAD;
            float textW = w - 2.0f * CELL_PAD;
            float nameY = y + imgH + 8.0f;
            std::string name = UiFont::fitEnd(entry.filename, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, textW);
            UiRender::textY(name, textX, nameY, 20.0f, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            std::string meta = dateOf(entry.modified) + "  " + sizeOf(entry.size);
            UiRender::textY(UiFont::fitEnd(meta, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, textW), textX, nameY + 20.0f,
                            18.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (hovered && name != entry.filename) {
                Page::tooltip(entry.filename, x, y, w, cellHeight());
            }
            if (!blocked && hovered && NewUi::rightClick) {
                NewUi::rightClick = false;
                rowMenu(key, entry, NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(x, y, w, cellHeight())) {
                UiSound::request(UiSound::TICK);
                open(key, entry.path);
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

        bool drawViewer(const Instances::Entry& entry, const Screenshots::List& list) {
            auto found = std::find_if(list.entries.begin(), list.entries.end(),
                                      [](const Screenshots::Entry& item) { return item.path == viewingPath; });
            if (found == list.entries.end()) {
                closeViewer();
                return false;
            }
            int viewing = (int) std::distance(list.entries.begin(), found);
            const Screenshots::Entry& current = *found;
            bool covered = NewUi::overlayCoversMouse();
            bool back = Page::iconButton("viewer#back", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, covered);
            if (back) {
                closeViewer();
                return false;
            }
            std::string title = UiFont::fitEnd(current.filename, UiFont::MEDIUM, Page::TITLE_SIZE,
                                               std::max(0.0f, Page::width() - 200.0f));
            Page::title(title, Page::left() + 30.0f, Page::TOP);
            std::string progress = std::to_string(viewing + 1) + " / " + std::to_string((int) list.entries.size());
            float progressW = UiFont::width(progress, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(progress, Page::right() - progressW, Page::TOP + 4.0f, 20.0f, UiTheme::textMuted(),
                            UiFont::MEDIUM, UiWidgets::TEXT_SMALL);

            float top = Page::TOP + 44.0f;
            float bottom = MainWindow::height() - 14.0f;
            float toolbarY = bottom - VIEWER_TOOLBAR_H;
            float imageArea = toolbarY - top;
            float imageX = Page::left() + 54.0f;
            float imageW = Page::width() - 108.0f;

            std::shared_ptr<const Pixels> full = Screenshots::fullImage(current.path, current.modified);
            std::shared_ptr<const Pixels> fallback = Screenshots::thumbnail(current.path, current.modified);
            std::shared_ptr<const Pixels> shown = full ? full : fallback;
            if (uint32_t texture = Textures::of(shown)) {
                float imgW = (float) shown->width;
                float imgH = (float) shown->height;
                float scale = std::min(imageW / imgW, imageArea / imgH);
                float drawW = imgW * scale;
                float drawH = imgH * scale;
                float dx = imageX + (imageW - drawW) * 0.5f;
                float dy = top + (imageArea - drawH) * 0.5f;
                ShaderUtil::roundTex(UiRender::transformX(dx), UiRender::transformY(dy), UiRender::transformW(drawW),
                                     UiRender::transformW(drawH), UiRender::transformW(8.0f), texture, 0.0f, 0.0f, 1.0f,
                                     1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                Page::spinner(imageX + imageW * 0.5f, top + imageArea * 0.5f, 10.0f, UiTheme::accent());
            }

            float arrowY = top + imageArea * 0.5f - 24.0f;
            if (viewing > 0) {
                if (Page::iconButton("viewer#prev", 0xf053, "上一张", Page::left() + 2.0f, arrowY, covered)) {
                    open(viewingKey, list.entries[viewing - 1].path);
                    return false;
                }
            }
            if (viewing + 1 < (int) list.entries.size()) {
                if (Page::iconButton("viewer#next", 0xf054, "下一张", Page::right() - 30.0f, arrowY, covered)) {
                    open(viewingKey, list.entries[viewing + 1].path);
                    return false;
                }
            }

            std::string meta = dateOf(current.modified) + "  " + sizeOf(current.size) + "  " +
                    (shown ? std::to_string(shown->width) + "×" + std::to_string(shown->height) : std::string("…"));
            UiRender::textY(meta, Page::left() + 2.0f, toolbarY + 2.0f, 22.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);

            float buttonY = toolbarY + VIEWER_TOOLBAR_H - 10.0f - Page::FIELD_H;
            std::string copyLabel = "复制";
            std::string revealLabel = "在文件夹中显示";
            std::string deleteLabel = "删除";
            float deleteW = Page::buttonWidth(deleteLabel, 0xf2ed);
            float revealW = Page::buttonWidth(revealLabel, 0xf07c);
            float copyW = Page::buttonWidth(copyLabel, 0xf0c5);
            float x = Page::right() - deleteW;
            if (Page::button("viewer#delete", deleteLabel, x, buttonY, deleteW, false, covered, 0xf2ed)) {
                std::filesystem::path path = current.path;
                std::string key = viewingKey;
                int at = viewing;
                Screenshots::remove(key, path);
                const Screenshots::List& updated = Screenshots::of(key);
                if (updated.entries.empty()) {
                    closeViewer();
                } else {
                    open(key, updated.entries[std::min(at, (int) updated.entries.size() - 1)].path);
                }
                return false;
            }
            x -= 10.0f + revealW;
            if (Page::button("viewer#reveal", revealLabel, x, buttonY, revealW, false, covered, 0xf07c)) {
                Platform::reveal(current.path);
            }
            x -= 10.0f + copyW;
            if (Page::button("viewer#copy", copyLabel, x, buttonY, copyW, false, covered, 0xf0c5)) {
                Screenshots::copy(current.path);
            }

            if (!covered) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    closeViewer();
                    return false;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) && viewing > 0) {
                    open(viewingKey, list.entries[viewing - 1].path);
                } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && viewing + 1 < (int) list.entries.size()) {
                    open(viewingKey, list.entries[viewing + 1].path);
                }
            }
            (void) entry;
            return false;
        }
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        if (viewingKey != key) {
            closeViewer();
            viewingKey = key;
        }
        const Screenshots::List& list = Screenshots::of(key);
        if (!viewingPath.empty()) {
            return drawViewer(entry, list);
        }

        bool covered = NewUi::overlayCoversMouse();
        bool back =
                Page::iconButton("screenshots#back", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, covered);
        const char* title = "截图";
        Page::title(title, Page::left() + 30.0f, Page::TOP);
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;

        float refreshX = Page::right() - 28.0f;
        if (Page::iconButton("screenshots#refresh", 0xf2f1, "刷新", refreshX, Page::TOP - 2.0f, covered)) {
            Screenshots::scan(key);
        }
        float folderX = refreshX - 32.0f;
        std::filesystem::path folder = entry.gameDirectory / "screenshots";
        if (Page::iconButton("screenshots#folder", 0xf07c, "打开截图文件夹", folderX, Page::TOP - 2.0f, covered)) {
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            Platform::open(folder);
        }
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                       std::max(0.0f, folderX - 12.0f - nameX)),
                        nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

        float barY = Page::TOP + 40.0f;
        if (!list.entries.empty()) {
            std::string counts = "共 " + std::to_string(list.entries.size()) + " 张";
            float countsW = UiFont::width(counts, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(counts, Page::right() - countsW, barY, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        float top = barY + 24.0f;
        float bottom = MainWindow::height() - 14.0f;
        if (list.entries.empty()) {
            if (list.loading) {
                message("正在读取截图…", top, true);
            } else {
                message("还没有截图，游戏里按 F2 可以截图", top, false);
            }
            return back;
        }
        int columns = std::max(1, (int) ((Page::width() + CELL_GAP) / (CELL_W + CELL_GAP)));
        int count = (int) list.entries.size();
        int rows = (count + columns - 1) / columns;
        float stride = CELL_W + CELL_GAP;
        float rowStride = cellHeight() + CELL_GAP;
        float contentW = columns * CELL_W + (columns - 1) * CELL_GAP;
        float left = Page::left() + (Page::width() - contentW) * 0.5f;
        float contentH = rows * cellHeight() + (rows - 1) * CELL_GAP + 8.0f;
        Page::Scroll body("screenshots#body#" + key, top, bottom, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y0 = body.top - body.offset + 4.0f;
        for (int i = 0; i < count; i++) {
            int row = i / columns;
            int col = i % columns;
            float cx = left + col * stride;
            float cy = y0 + row * rowStride;
            if (cy + cellHeight() < body.top || cy > body.bottom) {
                continue;
            }
            cell(key, list.entries[i], i, cx, cy, blocked);
        }
        body.end();
        return back;
    }

    void open(const std::string& key, const std::filesystem::path& path) {
        viewingKey = key;
        viewingPath = path;
        Screenshots::forgetFullImage();
    }
}

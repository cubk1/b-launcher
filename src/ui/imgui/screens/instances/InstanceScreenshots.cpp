#include "ui/imgui/screens/instances/InstanceScreenshots.h"
#include "app/instances/Screenshots.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/instances/ScreenshotViewer.h"
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

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace InstanceScreenshots {

    namespace blb = b_launcher_base;

    std::string sizeText(std::uint64_t bytes) {
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

    std::string dateText(std::int64_t mtime) {
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
#if defined(_WIN32)
        if (localtime_s(&local, &seconds) != 0) {
#else
        if (localtime_r(&seconds, &local) == nullptr) {
#endif
            return {};
        }
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        return text;
    }

    namespace {
        constexpr float CELL_MIN_W = 180.0f;
        constexpr float CELL_GAP = 14.0f;
        constexpr float CELL_RADIUS = 10.0f;
        constexpr float NAME_H = 48.0f;
        constexpr float ASPECT = 9.0f / 16.0f;

        struct Grid {
            int columns = 1;
            float cellW = 0.0f;
            float imageH = 0.0f;
            float cellH = 0.0f;
        };

        Grid gridOf(float width) {
            Grid grid;
            grid.columns = std::max(1, (int) ((width + CELL_GAP) / (CELL_MIN_W + CELL_GAP)));
            grid.cellW = (width - (float) (grid.columns - 1) * CELL_GAP) / (float) grid.columns;
            grid.imageH = grid.cellW * ASPECT;
            grid.cellH = grid.imageH + NAME_H;
            return grid;
        }

        void rowMenu(const std::string& key, const Screenshots::Entry& entry, float x, float y) {
            std::filesystem::path path = entry.path;
            NewUi::openMenu(
                    "screenshots#menu",
                    [key, path] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("查看", 0xf06e,
                                                             [key, path] { ScreenshotViewer::open(key, path); }));
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

        void image(const std::filesystem::path& path, int64_t modified, float x, float y, float w, float h,
                   float zoom) {
            std::shared_ptr<const Pixels> thumb = Screenshots::thumbnail(path, modified);
            uint32_t texture = Textures::of(thumb);
            if (texture != 0 && thumb->width > 0 && thumb->height > 0) {
                float imageAspect = (float) thumb->width / (float) thumb->height;
                float cellAspect = w / h;
                float spanU = (imageAspect > cellAspect ? cellAspect / imageAspect : 1.0f) * zoom;
                float spanV = (imageAspect > cellAspect ? 1.0f : imageAspect / cellAspect) * zoom;
                float u0 = (1.0f - spanU) * 0.5f;
                float v0 = (1.0f - spanV) * 0.5f;
                ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(w),
                                     UiRender::transformW(h), UiRender::transformW(CELL_RADIUS), texture, u0, v0,
                                     u0 + spanU, v0 + spanV, UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                UiRender::roundRect(x, y, w, h, CELL_RADIUS, UiTheme::surfaceRaised());
                Page::spinner(x + w * 0.5f, y + h * 0.5f, 7.0f, UiTheme::accent());
            }
        }

        void cell(const std::string& key, const Screenshots::Entry& entry, int index, float x, float y,
                  const Grid& grid, bool blocked) {
            float w = grid.cellW;
            float imgH = grid.imageH;
            bool hovered = !blocked && NewUi::hit(x, y, w, grid.cellH);
            float hover = UiState::motion("screenshots#" + std::to_string(index), hovered ? 1.0f : 0.0f, 20.0f);
            image(entry.path, entry.modified, x, y, w, imgH, 1.0f - 0.05f * hover);
            UiRender::outline(x, y, w, imgH, CELL_RADIUS,
                              UiRender::mix(UiTheme::border(UiTheme::outlineSoft()), UiTheme::accent(), hover));
            float textX = x + 2.0f;
            float textW = w - 4.0f;
            float nameY = y + imgH + 8.0f;
            std::string stem = entry.filename.substr(0, entry.filename.rfind('.'));
            std::string name = UiFont::fitEnd(stem, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, textW);
            uint32_t nameColor = UiRender::mix(UiTheme::textPrimary(), UiTheme::accentText(), hover);
            UiRender::textY(name, textX, nameY, 20.0f, nameColor, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            std::string meta = dateText(entry.modified) + "  " + sizeText(entry.size);
            UiRender::textY(UiFont::fitEnd(meta, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, textW), textX, nameY + 20.0f,
                            18.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (hovered && name != stem) {
                Page::tooltip(entry.filename, x, y, w, grid.cellH);
            }
            if (!blocked && hovered && NewUi::rightClick) {
                NewUi::rightClick = false;
                rowMenu(key, entry, NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(x, y, w, grid.cellH)) {
                UiSound::request(UiSound::TICK);
                ScreenshotViewer::open(key, entry.path);
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
        const Screenshots::List& list = Screenshots::of(key);

        bool covered = NewUi::overlayCoversMouse();
        bool back =
                Page::iconButton("screenshots#back", 0xf053, "返回", Page::left() - 4.0f, Page::top() - 2.0f, covered);
        const char* title = "截图";
        Page::title(title, Page::left() + 30.0f, Page::top());

        float refreshX = Page::right() - 28.0f;
        if (Page::iconButton("screenshots#refresh", 0xf2f1, "刷新", refreshX, Page::top() - 2.0f, covered)) {
            Screenshots::scan(key);
        }
        float folderX = refreshX - 32.0f;
        std::filesystem::path folder = entry.gameDirectory / "screenshots";
        if (Page::iconButton("screenshots#folder", 0xf07c, "打开截图文件夹", folderX, Page::top() - 2.0f, covered)) {
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            Platform::open(folder);
        }
        if (!list.entries.empty()) {
            std::string counts = std::to_string(list.entries.size()) + " 张";
            float countsW = UiFont::width(counts, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(counts, folderX - 10.0f - countsW, Page::top(), 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }

        float top = Page::top() + 44.0f;
        float bottom = MainWindow::height() - 14.0f;
        if (list.entries.empty()) {
            if (list.loading) {
                message("正在读取截图…", top, true);
            } else {
                message("还没有截图，游戏里按 F2 可以截图", top, false);
            }
            return back;
        }
        Grid grid = gridOf(Page::width());
        int count = (int) list.entries.size();
        int rows = (count + grid.columns - 1) / grid.columns;
        float stride = grid.cellW + CELL_GAP;
        float rowStride = grid.cellH + CELL_GAP;
        float contentH = (float) rows * rowStride - CELL_GAP + 8.0f;
        Page::Scroll body("screenshots#body#" + key, top, bottom, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y0 = body.top - body.offset + 4.0f;
        for (int i = 0; i < count; i++) {
            int row = i / grid.columns;
            int col = i % grid.columns;
            float cx = Page::left() + (float) col * stride;
            float cy = y0 + (float) row * rowStride;
            if (cy + grid.cellH < body.top || cy > body.bottom) {
                continue;
            }
            cell(key, list.entries[i], i, cx, cy, grid, blocked);
        }
        body.end();
        return back;
    }
}

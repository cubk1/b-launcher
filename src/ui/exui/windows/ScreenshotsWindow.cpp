#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ScreenshotsWindow.h"
#include "app/instances/Instances.h"
#include "app/instances/Screenshots.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/ImageView.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"

#include <core/base/format.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <chrono>
#include <optional>

namespace ForgottenMelody {

    namespace {
        namespace blb = b_launcher_base;

        class ScreenshotsWindow : public LiveForm {
        public:
            enum : uint32_t { TITLE = 1, LIST, PREVIEW, NAME, DETAILS, PREVIOUS, POSITION, FIT, NEXT,
                              STATUS, REFRESH, FOLDER, REVEAL, COPY, REMOVE, DONE };

            explicit ScreenshotsWindow(std::string key) : key(std::move(key)) {}
            ~ScreenshotsWindow() override { Screenshots::forgetFullImage(); }

            bool create(HWND parent) {
                const Instances::Entry* entry = Instances::find(key);
                if (entry == nullptr || !createDialog(*this, parent, L"截图管理", 900, 570,
                        exui::CaptionButtonMask::Close | exui::CaptionButtonMask::Maximize)) return false;
                using exui::ControlKind;
                const exui::ControlSpec controls[] = {
                        {TITLE, ControlKind::Label, {}, L"截图管理 - " + Platform::widen(entry->info.id), L"微软雅黑", 16},
                        {LIST, ControlKind::List, {}},
                        {NAME, ControlKind::Label, {}},
                        {DETAILS, ControlKind::Label, {}},
                        {PREVIOUS, ControlKind::Button, {}, L"上一张"},
                        {POSITION, ControlKind::Label, {}},
                        {FIT, ControlKind::Button, {}, L"适应窗口"},
                        {NEXT, ControlKind::Button, {}, L"下一张"},
                        {STATUS, ControlKind::Label, {}},
                        {REFRESH, ControlKind::Button, {}, L"刷新"},
                        {FOLDER, ControlKind::Button, {}, L"打开文件夹"},
                        {REVEAL, ControlKind::Button, {}, L"定位文件"},
                        {COPY, ControlKind::Button, {}, L"复制图片"},
                        {REMOVE, ControlKind::Button, {}, L"删除"},
                        {DONE, ControlKind::Button, {}, L"关闭"},
                };
                AddControls(controls);
                AddControl(PREVIEW, std::make_shared<exui::ImageView>());
                FindControl(PREVIEW)->SetTooltipText(L"滚轮缩放，拖动查看；按 0 或点击“适应窗口”还原");
                for (uint32_t id : {TITLE, NAME, DETAILS, STATUS}) {
                    Control<exui::Label>(id).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                }
                Control<exui::Label>(POSITION).SetAlignment(Gdiplus::StringAlignmentCenter,
                                                           Gdiplus::StringAlignmentCenter);
                auto& list = Control<exui::ListView>(LIST);
                list.InsertColumn(L"截图文件", 272);
                list.SetRowHeight(30);
                FindControl(REFRESH)->SetTooltipText(L"重新读取截图（F5）");
                FindControl(COPY)->SetTooltipText(L"复制选中的截图到剪贴板（Ctrl+C）");
                FindControl(REMOVE)->SetTooltipText(L"把选中的截图移到回收站（Delete）");
                using exui::Layout;
                SetLayout(Layout::Column({
                        {*FindControl(TITLE), 26},
                        Layout::Row({
                                {*FindControl(LIST), 286},
                                Layout::Column({
                                        {*FindControl(PREVIEW)},
                                        {*FindControl(NAME), 22},
                                        {*FindControl(DETAILS), 22},
                                        Layout::Row({{*FindControl(PREVIOUS), 76}, {*FindControl(POSITION)},
                                                     {*FindControl(FIT), 80}, {*FindControl(NEXT), 76}}, 8, 30),
                                }),
                        }, 14),
                        Layout::Row({{*FindControl(STATUS)}, {*FindControl(REFRESH), 64},
                                     {*FindControl(FOLDER), 96}, {*FindControl(REVEAL), 80},
                                     {*FindControl(COPY), 80}, {*FindControl(REMOVE), 64},
                                     {*FindControl(DONE), 64}}, 8, 35),
                }, 12), {18, 36, 18, 14});
                reload();
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const Instances::Entry* entry = Instances::find(key);
                if (entry == nullptr) {
                    CloseWindow();
                    return;
                }
                UpdateText(TITLE, L"截图管理 - " + Platform::widen(entry->info.id));
                const Screenshots::List& screenshots = Screenshots::of(key);
                auto now = std::chrono::steady_clock::now();
                if (now >= nextScan && !screenshots.loading) {
                    nextScan = now + std::chrono::seconds(5);
                    Screenshots::scan(key);
                }
                auto& list = Control<exui::ListView>(LIST);
                if (seenSerial != screenshots.serial || rows.empty()) {
                    Rows next;
                    std::vector<std::string> keys;
                    for (const Screenshots::Entry& shot : screenshots.entries) {
                        next.push_back({Platform::widen(shot.filename)});
                        keys.push_back(blb::path_to_utf8(shot.path));
                    }
                    fill(list, rows, shownKeys, std::move(next), std::move(keys));
                    seenSerial = screenshots.serial;
                    if (!selectedOnce && !shownKeys.empty()) {
                        list.SetSelectedRow(0);
                        selectedOnce = true;
                    }
                }
                UpdateText(STATUS, screenshots.loading ? L"正在读取截图…"
                           : screenshots.entries.empty() ? L"还没有截图，游戏里按 F2 可以截图"
                           : L"共 " + std::to_wstring(screenshots.entries.size()) + L" 张截图");
                updatePreview();
            }

            void OnSelection(uint32_t id, int) override {
                if (id == LIST) updatePreview();
            }

            void OnAction(uint32_t id) override {
                if (id == DONE) { CloseWindow(); return; }
                if (id == REFRESH) { reload(); return; }
                if (id == FIT) { Control<exui::ImageView>(PREVIEW).ResetView(); return; }
                if (id == FOLDER) {
                    if (const Instances::Entry* entry = Instances::find(key)) {
                        std::filesystem::path folder = entry->gameDirectory / "screenshots";
                        std::error_code error;
                        std::filesystem::create_directories(folder, error);
                        if (error || !Platform::open(folder)) {
                            message(GetHwnd(), L"无法打开截图文件夹", L"截图管理");
                        }
                    }
                    return;
                }
                if (id == PREVIOUS || id == NEXT) {
                    auto& list = Control<exui::ListView>(LIST);
                    int at = list.GetSelectedRow() + (id == PREVIOUS ? -1 : 1);
                    if (at >= 0 && at < list.GetRowCount()) {
                        list.SetSelectedRow(at);
                        list.EnsureRowVisible(at);
                        updatePreview();
                    }
                    return;
                }
                // A modal question can refresh/reorder the list, so keep the path by value.
                std::optional<Screenshots::Entry> shot = selected();
                if (!shot) return;
                if (id == COPY) Screenshots::copy(shot->path);
                if (id == REVEAL) Platform::reveal(shot->path);
                if (id == REMOVE && message(GetHwnd(), L"要把截图 “" + Platform::widen(shot->filename) +
                        L"” 移到回收站吗？", L"删除截图", exui::MsgBoxButtons::OkCancel, {L"取消", L"删除"}) == 0) {
                    Screenshots::remove(key, shot->path);
                    refresh();
                }
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_KEYDOWN) {
                    if (wp == VK_ESCAPE) { CloseWindow(); return 0; }
                    if (wp == VK_F5) { OnAction(REFRESH); return 0; }
                    if (wp == VK_DELETE) { OnAction(REMOVE); return 0; }
                    if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) { OnAction(COPY); return 0; }
                }
                LRESULT result = LiveForm::HandleMessage(msg, wp, lp);
                if (msg == WM_GETMINMAXINFO && lp) {
                    auto& info = *reinterpret_cast<MINMAXINFO*>(lp);
                    info.ptMinTrackSize = {ToPhysical(820), ToPhysical(480)};
                }
                return result;
            }

        private:
            std::string key, previewKey;
            Rows rows;
            std::vector<std::string> shownKeys;
            int seenSerial = -1;
            bool selectedOnce = false;
            std::chrono::steady_clock::time_point nextScan;
            std::shared_ptr<const Pixels> shownPixels;

            std::optional<Screenshots::Entry> selected() const {
                std::string path = selectedKey(Control<exui::ListView>(LIST), shownKeys);
                const auto& entries = Screenshots::of(key).entries;
                auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
                    return blb::path_to_utf8(entry.path) == path;
                });
                return found == entries.end() ? std::nullopt : std::optional<Screenshots::Entry>(*found);
            }

            void reload() {
                Screenshots::forgetFullImage();
                nextScan = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                Screenshots::scan(key);
                refresh();
            }

            void updatePreview() {
                auto shot = selected();
                auto& list = Control<exui::ListView>(LIST);
                int row = list.GetSelectedRow();
                FindControl(PREVIOUS)->SetEnabled(shot && row > 0);
                FindControl(NEXT)->SetEnabled(shot && row + 1 < list.GetRowCount());
                for (uint32_t id : {COPY, REVEAL, REMOVE}) FindControl(id)->SetEnabled(shot.has_value());
                FindControl(FIT)->SetEnabled(shot.has_value());
                auto& preview = Control<exui::ImageView>(PREVIEW);
                std::string nextKey = shot ? blb::path_to_utf8(shot->path) + "#" + std::to_string(shot->modified) : "";
                if (previewKey != nextKey) {
                    previewKey = nextKey;
                    shownPixels.reset();
                    preview.SetImage(nullptr);
                    Screenshots::forgetFullImage();
                }
                if (!shot) {
                    UpdateText(NAME, L"");
                    UpdateText(DETAILS, L"");
                    UpdateText(POSITION, L"");
                    UpdateText(PREVIEW, Screenshots::of(key).loading ? L"正在读取截图…"
                               : shownKeys.empty() ? L"还没有截图，游戏里按 F2 可以截图" : L"选择左侧的截图来预览");
                    return;
                }
                auto full = Screenshots::fullImage(shot->path, shot->modified);
                auto pixels = full ? full : Screenshots::thumbnail(shot->path, shot->modified);
                FindControl(FIT)->SetEnabled(pixels != nullptr);
                bool failed = Screenshots::fullImageFailed(shot->path, shot->modified);
                if (shownPixels != pixels) {
                    shownPixels = pixels;
                    preview.SetImage(pixels ? exui::BitmapFromRgba(pixels->width, pixels->height, pixels->rgba) : nullptr,
                                     false);
                }
                UpdateText(PREVIEW, failed ? L"无法预览这张截图，点击“刷新”重试" : L"正在读取截图…");
                UpdateText(NAME, Platform::widen(shot->filename));
                FindControl(NAME)->SetTooltipText(Platform::widen(blb::path_to_utf8(shot->path)));
                std::string size = shot->size >= 1048576 ? blb::sformat("{:.1f} MB", shot->size / 1048576.0)
                                                        : blb::sformat("{:.1f} KB", shot->size / 1024.0);
                std::wstring detail = full ? std::to_wstring(full->width) + L" × " + std::to_wstring(full->height)
                                           : failed ? L"原图读取失败，请刷新重试" : L"正在读取原图…";
                UpdateText(DETAILS, detail + L"  ·  " + Platform::widen(size));
                UpdateText(POSITION, std::to_wstring(row + 1) + L" / " + std::to_wstring(shownKeys.size()));
            }
        };
    }

    void showScreenshots(HWND parent, const std::string& instanceKey) {
        ScreenshotsWindow window(instanceKey);
        if (window.create(parent)) runModal(window);
    }
}

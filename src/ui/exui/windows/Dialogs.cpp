#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/Dialogs.h"
#include "app/system/Platform.h"
#include "ui/exui/Resources.h"
#include "ui/exui/widgets/Button.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"

#include <algorithm>
#include <cstdint>

namespace ForgottenMelody {

    namespace {
        // EXUI's own timers use addresses as ids, so a small number never collides with them.
        constexpr UINT_PTR REFRESH_TIMER = 1;

        class NameDialog : public exui::Form {
        public:
            enum : uint32_t { PROMPT = 1, NAME, SAVE, CANCEL };

            explicit NameDialog(std::function<std::wstring(const std::wstring&)> onSave) : save(std::move(onSave)) {}

            bool create(HWND parent, const std::wstring& title, const std::wstring& prompt, const std::wstring& name) {
                static const exui::ControlSpec CONTROLS[] = {
                        {PROMPT, exui::ControlKind::Label, {15, 15, 80, 25}},
                        {NAME, exui::ControlKind::Edit, {105, 15, 100, 25}},
                        {SAVE, exui::ControlKind::Button, {215, 15, 70, 25}, L"保存"},
                        {CANCEL, exui::ControlKind::Button, {290, 15, 70, 25}, L"取消"},
                };
                if (!createDialog(*this, parent, title, 375, 58, 0)) {
                    return false;
                }
                AddControls(CONTROLS);
                SetText(PROMPT, prompt);
                SetText(NAME, name);
                return true;
            }

        protected:
            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_KEYDOWN && wp == VK_RETURN) {
                    commit();
                    return 0;
                }
                if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
                    CloseWindow();
                    return 0;
                }
                return Form::HandleMessage(msg, wp, lp);
            }

            void OnAction(uint32_t id) override {
                if (id == SAVE) {
                    commit();
                } else if (id == CANCEL) {
                    CloseWindow();
                }
            }

        private:
            std::function<std::wstring(const std::wstring&)> save;

            void commit() {
                std::wstring problem = save(Text(NAME));
                if (!problem.empty()) {
                    message(GetHwnd(), problem, GetTitle());
                    return;
                }
                CloseWindow();
            }
        };
    }

    // The 2014 launcher averages the skin without its bottom row and left column, yet divides by every pixel.
    COLORREF skinBaseColor(exui::Skin skin) {
        Gdiplus::Bitmap* bitmap = exui::SkinBitmap(skin);
        const int width = bitmap != nullptr ? (int) bitmap->GetWidth() : 0;
        const int height = bitmap != nullptr ? (int) bitmap->GetHeight() : 0;
        Gdiplus::Rect whole(0, 0, width, height);
        Gdiplus::BitmapData data;
        if (width <= 0 || height <= 0 ||
            bitmap->LockBits(&whole, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB, &data) != Gdiplus::Ok) {
            return 0;
        }
        uint64_t r = 0, g = 0, b = 0;
        for (int y = 0; y + 1 < height; y++) {
            const auto* row = (const uint32_t*) ((const uint8_t*) data.Scan0 + (ptrdiff_t) y * data.Stride);
            for (int x = 1; x < width; x++) {
                r += (row[x] >> 16) & 255;
                g += (row[x] >> 8) & 255;
                b += row[x] & 255;
            }
        }
        bitmap->UnlockBits(&data);
        uint64_t count = (uint64_t) width * height;
        return RGB(r / count, g / count, b / count);
    }

    exui::WindowBackground messageBackground() {
        static const COLORREF base = skinBaseColor(exui::Skin::DialogBackground);
        exui::WindowBackground background;
        std::span<const uint8_t> skin = exui::SkinBytes(exui::Skin::DialogBackground);
        background.skinBytes.assign(skin.begin(), skin.end());
        background.baseColor = base;
        background.windowAlpha = 255;
        background.aeroEnabled = false;
        background.cornerRadius = 0;
        background.mode = exui::SkinBackgroundMode::BaseColorOnly;
        return background;
    }

    int message(HWND parent, const std::wstring& text, const std::wstring& title, exui::MsgBoxButtons buttons,
                const std::vector<std::wstring>& labels) {
        int pressed = exui::MsgBox::Show(parent, text, title, exui::MsgBoxIcon::None, buttons, labels, true,
                                         messageBackground());
        return exui::MsgBox::ResultCode(exui::MsgBox::Measure(text, exui::MsgBoxIcon::None, buttons, labels), pressed);
    }

    bool createDialog(exui::Window& window, HWND parent, const std::wstring& title, int width, int height,
                      uint32_t captionMask) {
        if (!window.CreateHostWindow(parent, title, width, height, exui::SkinBytes(exui::Skin::DialogBackground),
                                     0x808080, 7, captionMask, false)) {
            return false;
        }
        window.SetWindowAnimationTypes(exui::WindowAnimationKind::ZoomFade, exui::WindowAnimationKind::ZoomFade);
        window.SetControlAnimationsEnabled(false);
        window.SetWindowAlpha(150);
        window.SetBackgroundMode(exui::SkinBackgroundMode::EdgeFade);
        return true;
    }

    void runModal(exui::Window& window, exui::Widget* focus) {
        window.ShowWindowWithAnimation();
        if (focus != nullptr) {
            window.SetFocusedWidget(focus);
        }
        window.RunModalLoop(true);
    }

    void askName(HWND parent, const std::wstring& title, const std::wstring& prompt, const std::wstring& name,
                 const std::function<std::wstring(const std::wstring&)>& save) {
        NameDialog dialog(save);
        if (dialog.create(parent, title, prompt, name)) {
            runModal(dialog, dialog.FindControl(NameDialog::NAME));
        }
    }

    std::string trimmed(const std::wstring& text) {
        std::string value = Platform::narrow(text);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        return value;
    }

    void LiveForm::startRefreshing() {
        refresh();
        SetTimer(GetHwnd(), REFRESH_TIMER, 100, nullptr);
    }

    LRESULT LiveForm::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_TIMER && wp == REFRESH_TIMER) {
            refresh();
            return 0;
        }
        return Form::HandleMessage(msg, wp, lp);
    }

    bool SectionedForm::createFrame(HWND parent, const std::wstring& caption, const std::wstring& title,
                                    std::vector<Section> list, const Frame& frame) {
        const exui::Skin skin = exui::Skin::DownloadCenterBackground;
        if (!CreateHostWindow(parent, caption, frame.width, frame.height, exui::SkinBytes(skin), skinBaseColor(skin), 7,
                              exui::CaptionButtonMask::Close, false)) {
            return false;
        }
        SetWindowAnimationTypes(exui::WindowAnimationKind::RevealLeft, exui::WindowAnimationKind::ZoomFade, false);
        SetBackgroundMode(exui::SkinBackgroundMode::EdgeFade);
        sections = std::move(list);
        subject = title;

        const int height = frame.bottom - frame.top;
        // As much room on the left as on the right: 下载中心's list started 2 pixels further left, and now ends where
        // it did, so the backing stays in place.
        const int edge = 15;
        const int left = frame.listWidth > 0 ? 13 + frame.listWidth + 3 : edge, right = frame.width - edge;
        if (frame.listWidth > 0) {
            auto names = std::make_shared<exui::ListView>(edge, frame.top, frame.listWidth - 2, height);
            names->SetHeaderVisible(false);
            names->SetRowHeight(30);
            names->InsertColumn(L"", frame.listWidth - 6);
            for (const Section& section : sections) {
                names->InsertRow({section.name});
            }
            AddControl(SECTIONS, names);
        }
        // 打开下载's button, moved right to line up with the backing.
        auto done = std::make_shared<exui::Button>(right - frame.doneWidth, frame.height - 15 - frame.doneHeight,
                                                   frame.doneWidth, frame.doneHeight, L"完成");
        done->SetFont(L"宋体", frame.doneHeight >= 50 ? 15.0f : 12.0f);
        AddControl(DONE, done);
        auto banner = std::make_shared<exui::Label>(25, (frame.top - 50) / 2, 600, 50, std::wstring(), 0xFEFFFFFF);
        banner->SetFont(L"微软雅黑", 22, Gdiplus::FontStyleBold);
        banner->SetAutoGlowColor();
        AddControl(TITLE, banner);
        // Where 下载中心 had its web page: a backing in the look of the list, so that the skin's blocks stay behind
        // the controls.
        auto backing = std::make_shared<exui::Label>(left, frame.top, right - left, height, std::wstring());
        backing->SetBackgroundColor(0xD2FFFFFF);
        backing->SetBorderColor(0x50000000);
        AddControl(BACKING, backing);
        for (const Section& section : sections) {
            AddControl(section.panel, std::make_shared<exui::Panel>(left, frame.top, right - left, height));
        }
        return true;
    }

    void SectionedForm::showSection(int index) {
        for (int i = 0; i < (int) sections.size(); i++) {
            FindControl(sections[i].panel)->SetVisible(i == index);
        }
        SetText(TITLE, subject.empty() ? sections[index].name : subject + L" · " + sections[index].name);
        auto* names = dynamic_cast<exui::ListView*>(FindControl(SECTIONS));
        if (names != nullptr && names->GetSelectedRow() != index) {
            names->SetSelectedRow(index);
        }
        sectionShown(index);
    }

    void SectionedForm::choose(uint32_t id, const std::vector<std::wstring>& options, int selected) {
        auto& combo = Control<exui::ComboBox>(id);
        for (const std::wstring& option : options) {
            combo.AddItem(option);
        }
        combo.SetSelectedIndex(selected);
    }

    void SectionedForm::OnSelection(uint32_t id, int index) {
        if (id == SECTIONS && index >= 0 && index < (int) sections.size()) {
            showSection(index);
        }
    }

    void SectionedForm::OnAction(uint32_t id) {
        if (id == DONE) {
            CloseWindow();
        }
    }

    bool LiveForm::fill(exui::ListView& list, Rows& shown, Rows rows) {
        if (rows == shown) {
            return false;
        }
        bool sameShape = rows.size() == shown.size() &&
                std::equal(rows.begin(), rows.end(), shown.begin(),
                           [](const auto& next, const auto& now) { return next.size() == now.size(); });
        if (sameShape) {
            for (int row = 0; row < (int) rows.size(); row++) {
                for (int column = 0; column < (int) rows[row].size(); column++) {
                    if (rows[row][column] != shown[row][column]) {
                        list.SetCellText(row, column, rows[row][column]);
                    }
                }
            }
        } else {
            list.ClearRows();
            for (const auto& row : rows) {
                list.InsertRow(row);
            }
        }
        shown = std::move(rows);
        return true;
    }

    void LiveForm::fill(exui::ListView& list, Rows& shown, std::vector<std::string>& shownKeys, Rows rows,
                        std::vector<std::string> keys) {
        std::vector<std::string> kept = selectedKeys(list, shownKeys);
        if (fill(list, shown, std::move(rows)) && !kept.empty()) {
            std::vector<int> again;
            for (const std::string& key : kept) {
                auto at = std::find(keys.begin(), keys.end(), key);
                if (at != keys.end()) {
                    again.push_back((int) (at - keys.begin()));
                }
            }
            list.SetSelectedRows(again);
        }
        shownKeys = std::move(keys);
    }

    std::string LiveForm::selectedKey(const exui::ListView& list, const std::vector<std::string>& shownKeys) {
        int row = list.GetSelectedRow();
        return row >= 0 && row < (int) shownKeys.size() ? shownKeys[row] : std::string();
    }

    std::vector<std::string> LiveForm::selectedKeys(const exui::ListView& list,
                                                    const std::vector<std::string>& shownKeys) {
        std::vector<std::string> keys;
        if (std::string current = selectedKey(list, shownKeys); !current.empty()) {
            keys.push_back(current);
        }
        for (int row : list.GetSelectedRows()) {
            if (row < (int) shownKeys.size() && !shownKeys[row].empty() &&
                std::find(keys.begin(), keys.end(), shownKeys[row]) == keys.end()) {
                keys.push_back(shownKeys[row]);
            }
        }
        return keys;
    }
}

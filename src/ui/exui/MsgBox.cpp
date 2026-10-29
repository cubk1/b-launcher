#include "ui/exui/MsgBox.h"
#include "ui/exui/Skins.h"
#include "ui/exui/widgets/Button.h"

namespace exui {
    using namespace Gdiplus;
    namespace {
        std::vector<std::pair<std::wstring, int>> StandardButtons(MsgBoxButtons type) {
            // From right to left, with their result codes.
            switch (type) {
                case MsgBoxButtons::OkCancel: return {{L"取消", 1}, {L"确认", 0}};
                case MsgBoxButtons::AbortRetryIgnore: return {{L"忽略", 4}, {L"重试", 3}, {L"放弃", 2}};
                case MsgBoxButtons::YesNoCancel: return {{L"取消", 1}, {L"否(&N)", 6}, {L"是(&Y)", 5}};
                case MsgBoxButtons::YesNo: return {{L"否(&N)", 6}, {L"是(&Y)", 5}};
                case MsgBoxButtons::RetryCancel: return {{L"取消", 1}, {L"重试", 3}};
                default: return {{L"确认", 0}};
            }
        }
        class MessageWindow final : public Window {
        public:
            MessageWindow() {
                // Esc closes the box once the current message is handled.
                OnWindowMessageFilter = [this](UINT msg, WPARAM wp, LPARAM, LRESULT&) {
                    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) PostMessageW(GetHwnd(), WM_CLOSE, 0, 0);
                    return false;
                };
            }
            MsgBoxLayout layout;
            std::wstring message;
            MsgBoxIcon icon = MsgBoxIcon::None;
            void PaintClientBackground(Graphics& g) override {
                MsgBox::PaintBody(g, GetWidth(), GetHeight(), icon);
            }
            void PaintClientForeground(Graphics& g) override {
                DrawGlowText(g, message, DefaultFontFamily(), 12, FontStyleRegular,
                             Adjust(layout.message, 2, 2, -4, -4), 0xD2000000, 0, StringAlignmentNear,
                             StringAlignmentNear, 0);
            }
        };
    }

    MsgBoxLayout MsgBox::Measure(std::wstring_view message, MsgBoxIcon icon, MsgBoxButtons type,
                                 const std::vector<std::wstring>& custom) {
        MsgBoxLayout layout;
        if (!Initialize()) return layout;
        Bitmap scratch(1, 1, PixelFormat32bppPARGB);
        Graphics g(&scratch);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        RectF measured;
        // Measured unbounded, without a string format.
        g.MeasureString(message.data(), static_cast<INT>(message.size()), DefaultFont(), RectF(0, 0, 0, 0), nullptr,
                        &measured);
        const bool hasIcon = icon != MsgBoxIcon::None;
        const float horizontal = hasIcon ? 120.0f : 60.0f;
        layout.size = Size(std::max(320, static_cast<int>(measured.Width + horizontal)),
                           std::max(150, static_cast<int>(measured.Height + 100)));
        layout.message = RectF(hasIcon ? 85.0f : 30.0f, measured.Height < 30 ? 47.0f : 43.0f,
                               measured.Width > 320 - horizontal ? measured.Width + 10 : 320 - horizontal,
                               measured.Height > 50 ? measured.Height + 5 : 50);
        layout.icon = hasIcon ? Rect(30, 40, 40, 40) : Rect();
        auto labels = StandardButtons(type);
        if (!custom.empty()) {
            const auto standard = labels;
            labels.clear();
            for (size_t i = 0; i < custom.size(); ++i)
                labels.emplace_back(custom[i], i < standard.size() ? standard[i].second : static_cast<int>(i));
        }
        // Fixed widths: neither long labels nor many buttons widen the box.
        for (size_t i = 0; i < labels.size(); ++i)
            layout.buttons.push_back(
                    {labels[i].first,
                     Rect(layout.size.Width - static_cast<int>(i + 1) * 75 - 10, layout.size.Height - 36, 70, 24),
                     labels[i].second});
        return layout;
    }
    Rect MsgBox::IconSlice(MsgBoxIcon icon) {
        switch (icon) {
            case MsgBoxIcon::None: return {};
            case MsgBoxIcon::Warning: return {40, 0, 40, 40};
            case MsgBoxIcon::Question: return {80, 0, 40, 40};
            case MsgBoxIcon::Info: return {120, 0, 40, 40};
            case MsgBoxIcon::Extra: return {160, 0, 40, 40};
            default: return {0, 0, 40, 40};
        }
    }
    void MsgBox::PaintBody(Graphics& g, int w, int h, MsgBoxIcon icon) {
        if (auto* border = SkinBitmap(Skin::MsgBoxBorder)) {
            g.DrawImage(border, RectF(7, 7, float(w - 14), 96), 0, 0, 1, 96, UnitPixel);
            // The middle strip is read upwards from row 97.
            g.DrawImage(border, RectF(7, 103, float(w - 14), float(h - 145)), 0, 97, 1, -21, UnitPixel);
            g.DrawImage(border, RectF(7, float(h - 42), float(w - 14), 35), 0, 129, 1, 31, UnitPixel);
        }
        if (auto* icons = SkinBitmap(Skin::MsgBoxSkin); icons && icon != MsgBoxIcon::None)
            g.DrawImage(icons, RectF(30, 40, 40, 40), float(IconSlice(icon).X), 0, 40, 40, UnitPixel);
    }
    int MsgBox::ResultCode(const MsgBoxLayout& layout, int index) {
        if (index < 0 || static_cast<size_t>(index) >= layout.buttons.size()) return 1;
        return layout.buttons[index].resultCode;
    }
    WindowBackground MsgBox::ResolveBackground(HWND parent, const std::optional<WindowBackground>& explicitBackground) {
        if (explicitBackground) return *explicitBackground;
        if (const auto* host = Window::FromHandle(parent)) return host->GetBackground();
        WindowBackground background;
        background.baseColor = 16746496;
        background.windowAlpha = 210;
        background.cornerRadius = 7;
        return background;
    }
    int MsgBox::Show(HWND parent, std::wstring_view message, std::wstring_view title, MsgBoxIcon icon,
                     MsgBoxButtons type, const std::vector<std::wstring>& custom, bool show,
                     const std::optional<WindowBackground>& explicitBackground, bool beep) {
        // A disabled parent cannot take activation back when the box closes, and Windows would activate
        // another application instead. The modal window it waits for owns the box then.
        if (parent && !IsWindowEnabled(parent))
            if (const HWND popup = GetWindow(parent, GW_ENABLEDPOPUP)) parent = popup;
        MessageWindow window;
        window.layout = Measure(message, icon, type, custom);
        if (window.layout.size.Width <= 0) return -1;
        window.message = message;
        window.icon = icon;
        int result = -1;
        const auto background = ResolveBackground(parent, explicitBackground);
        if (!window.CreateHostWindow(parent, std::wstring(title), window.layout.size.Width, window.layout.size.Height,
                                     background.skinBytes, background.baseColor, background.cornerRadius,
                                     CaptionButtonMask::Title | CaptionButtonMask::Close, false))
            return -1;
        window.SetBackground(background);
        for (const auto& spec : window.layout.buttons)
            window.Add<Button>(spec.bounds.X, spec.bounds.Y, spec.bounds.Width, spec.bounds.Height, spec.caption);
        window.OnButtonClicked = [&](Widget* clicked) {
            const auto& widgets = window.GetWidgets();
            for (size_t i = 0; i < widgets.size(); ++i)
                if (widgets[i].get() == clicked) result = static_cast<int>(i);
            // Closes after the click is handled, not inside it.
            if (window.GetHwnd()) PostMessageW(window.GetHwnd(), WM_CLOSE, 0, 0);
        };
        // Enter picks the leftmost (affirmative) button, as in standard dialogs; Esc closes.
        if (!window.GetWidgets().empty()) window.SetFocusedWidget(window.GetWidgets().back().get());
        // The owner stays enabled but loses its hover; the modal loop drops input
        // aimed at other windows.
        if (show && beep && icon != MsgBoxIcon::None) MessageBeep(static_cast<UINT>(icon));
        if (parent) SendMessageW(parent, WM_MOUSELEAVE, 0, 0);
        if (show) window.ShowWindowWithAnimation(SW_SHOWNORMAL);
        // Topmost; a hidden box (automation) is not activated.
        if (show) SetForegroundWindow(window.GetHwnd());
        SetWindowPos(window.GetHwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | (show ? 0 : SWP_NOACTIVATE));
        if (parent) PostMessageW(parent, WM_MOUSELEAVE, 0, 0);
        window.RunModalLoop(true);
        window.CloseWindow();
        if (show && IsWindow(parent)) SetActiveWindow(parent);
        return result;
    }
} // namespace exui

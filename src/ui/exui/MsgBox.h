#pragma once
#include "ui/exui/Window.h"

namespace exui {
    enum class MsgBoxIcon : int { None = 0, Error = 16, Question = 32, Warning = 48, Info = 64, Extra = 128 };
    enum class MsgBoxButtons : int {
        Ok = 0,
        OkCancel = 1,
        AbortRetryIgnore = 2,
        YesNoCancel = 3,
        YesNo = 4,
        RetryCancel = 5
    };
    struct MsgBoxButtonLayout {
        std::wstring caption;
        Gdiplus::Rect bounds;
        // The standard choice: OK 0, Cancel 1, Abort 2, Retry 3, Ignore 4, Yes 5, No 6.
        int resultCode = 0;
    };
    struct MsgBoxLayout {
        Gdiplus::Size size;
        Gdiplus::RectF message;
        Gdiplus::Rect icon;
        std::vector<MsgBoxButtonLayout> buttons; // from right to left
    };

    // The skinned message box: icon, text and a row of 70px buttons at the bottom
    // right. It inherits the look of an EXUI owner window.
    class MsgBox {
    public:
        // Returns the index of the clicked button (counted from the right), or -1
        // when the box was closed. Custom labels replace the captions of the
        // buttons type lays out, keeping their result codes.
        static int Show(HWND parent, std::wstring_view message, std::wstring_view title = L"提示：",
                        MsgBoxIcon icon = MsgBoxIcon::None, MsgBoxButtons buttons = MsgBoxButtons::Ok,
                        const std::vector<std::wstring>& customLabels = {}, bool showWindow = true,
                        const std::optional<WindowBackground>& background = std::nullopt, bool beep = true);
        // The given background, else the parent's if it is an EXUI window, else translucent blue.
        static WindowBackground ResolveBackground(HWND parent,
                                                  const std::optional<WindowBackground>& background = std::nullopt);
        // The result code of a button index from Show; closing the box counts as Cancel.
        static int ResultCode(const MsgBoxLayout& layout, int buttonIndex);
        static MsgBoxLayout Measure(std::wstring_view message, MsgBoxIcon icon = MsgBoxIcon::None,
                                    MsgBoxButtons buttons = MsgBoxButtons::Ok,
                                    const std::vector<std::wstring>& customLabels = {});
        static Gdiplus::Rect IconSlice(MsgBoxIcon icon);
        // The body: three stretched strips of the border skin, and the icon.
        static void PaintBody(Gdiplus::Graphics& graphics, int width, int height, MsgBoxIcon icon);
    };
} // namespace exui

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/Resources.h"
#include "ui/exui/Skins.h"

#include <cstdint>

namespace ForgottenMelody {

    namespace {
        // The 2014 launcher averages the skin without its bottom row and left column, yet divides by every pixel.
        COLORREF baseColorOf(Gdiplus::Bitmap* bitmap) {
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
    }

    exui::WindowBackground messageBackground() {
        static const COLORREF base = baseColorOf(exui::SkinBitmap(exui::Skin::DialogBackground));
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
}

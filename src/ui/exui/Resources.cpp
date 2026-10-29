#include "ui/exui/Resources.h"
#include "ui/exui/Window.h"

namespace exui {
    using namespace Gdiplus;
    namespace {
        ULONG_PTR token = 0;
        std::wstring fontFamily = L"宋体";
        std::unique_ptr<Font> defaultFont;
        const wchar_t* appIconResource = nullptr;
        std::unique_ptr<Bitmap> skinCache[int(Skin::MenuCheckmark) + 1];
    } // namespace

    bool Initialize(const wchar_t* appIcon) {
        if (token) return true;
        appIconResource = appIcon;
        GdiplusStartupInput input;
        if (GdiplusStartup(&token, &input, nullptr) != Ok) {
            token = 0;
            return false;
        }
        FontFamily primary(L"宋体");
        if (primary.GetLastStatus() == Ok) {
            fontFamily = L"宋体";
        } else {
            NONCLIENTMETRICSW metrics{};
            metrics.cbSize = sizeof(metrics);
            fontFamily = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)
                    ? metrics.lfCaptionFont.lfFaceName
                    : L"宋体";
            // The caption font, except Tahoma; 宋体 again when that is missing too.
            FontFamily fallback(fontFamily.c_str());
            if (fontFamily == L"Tahoma" || fallback.GetLastStatus() != Ok) fontFamily = L"宋体";
        }
        defaultFont = std::make_unique<Font>(fontFamily.c_str(), 12.0f, FontStyleRegular, UnitPixel);
        if (defaultFont->GetLastStatus() != Ok || !SkinBitmap(Skin::FrameAtlas) || !Window::RegisterWindowClass()) {
            Shutdown();
            return false;
        }
        return true;
    }
    void Shutdown() {
        defaultFont.reset();
        for (auto& cached : skinCache)
            cached.reset();
        if (token) {
            GdiplusShutdown(token);
            token = 0;
        }
    }
    const std::wstring& DefaultFontFamily() {
        return fontFamily;
    }
    const Font* DefaultFont() {
        return defaultFont.get();
    }
    Bitmap* SkinBitmap(Skin skin) {
        auto& cached = skinCache[int(skin)];
        if (!cached && token) cached = DecodeImage(SkinBytes(skin));
        return cached.get();
    }
    HICON AppIcon(int size) {
        if (!appIconResource) return nullptr;
        return static_cast<HICON>(
                LoadImageW(GetModuleHandleW(nullptr), appIconResource, IMAGE_ICON, size, size, LR_SHARED));
    }

} // namespace exui

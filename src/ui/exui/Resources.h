#pragma once
#include "ui/exui/Graphics.h"
#include "ui/exui/Skins.h"
#include <string>

namespace exui {

    // Starts GDI+ and loads the shared resources; repeated calls do nothing.
    // appIcon names an icon resource of the executable, used by every window.
    bool Initialize(const wchar_t* appIcon = nullptr);
    // Destroy all windows and surfaces first.
    void Shutdown();

    HICON AppIcon(int size);
    // 宋体, or the caption font where 宋体 is missing.
    const std::wstring& DefaultFontFamily();
    const Gdiplus::Font* DefaultFont();
    // Decoded once per Initialize.
    Gdiplus::Bitmap* SkinBitmap(Skin skin);

} // namespace exui

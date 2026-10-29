#pragma once
#include <windows.h>
#include <objidl.h>
#include <algorithm>
namespace Gdiplus {
    using std::max;
    using std::min;
}
#include <gdiplus.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace exui {

    // Top-down premultiplied 32bpp DIB, usable by both GDI+ and GdiAlphaBlend.
    class Surface {
    public:
        Surface() = default;
        ~Surface();
        Surface(const Surface&) = delete;
        Surface& operator=(const Surface&) = delete;
        bool Resize(int width, int height);
        bool CopyFrom(const Surface& other);
        void Reset();
        HDC GetDC() const noexcept {
            return m_dc;
        }
        Gdiplus::Bitmap* GetBitmap() const noexcept {
            return m_bitmap.get();
        }
        std::span<std::uint32_t> Pixels() noexcept;
        std::span<const std::uint32_t> Pixels() const noexcept;
        int Width() const noexcept {
            return m_width;
        }
        int Height() const noexcept {
            return m_height;
        }

    private:
        HDC m_dc = nullptr;
        HBITMAP m_dib = nullptr;
        HGDIOBJ m_previous = nullptr;
        std::uint32_t* m_bits = nullptr;
        int m_width = 0, m_height = 0;
        std::unique_ptr<Gdiplus::Bitmap> m_bitmap;
    };

    // ---------------------------------------------------------------------------
    // Colors

    constexpr Gdiplus::ARGB ToArgb(COLORREF bgr, uint8_t alpha = 255) noexcept {
        return (uint32_t(alpha) << 24) | ((bgr & 255) << 16) | (bgr & 0xFF00) | ((bgr >> 16) & 255);
    }
    constexpr bool IsDark(Gdiplus::ARGB color) noexcept {
        return ((color >> 16) & 255) * 0.299 + ((color >> 8) & 255) * 0.587 + (color & 255) * 0.114 < 192 - 1e-7;
    }
    // A glow that sets the text off: light around dark text, dark around light text.
    constexpr Gdiplus::ARGB GlowColorFor(Gdiplus::ARGB text) noexcept {
        return IsDark(text) ? 0x55FFFFFF : 0x4B000000;
    }

    // ---------------------------------------------------------------------------
    // Bitmaps

    // Decodes an image into a PARGB bitmap that owns its pixels.
    std::unique_ptr<Gdiplus::Bitmap> DecodeImage(std::span<const uint8_t> bytes);
    // GDI+ Clone can keep referring to the source pixels; this copy owns them.
    std::unique_ptr<Gdiplus::Bitmap> CopyBitmap(Gdiplus::Bitmap& bitmap);
    void DrawWithOpacity(Gdiplus::Graphics& g, Gdiplus::Bitmap& bitmap, const Gdiplus::Rect& destination,
                         uint8_t opacity);

    // ---------------------------------------------------------------------------
    // Shapes. Rounded corners are given by the arc diameter.

    Gdiplus::RectF Adjust(Gdiplus::RectF rect, float dx, float dy, float dw, float dh);
    void RoundedRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float diameter);
    void DrawRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float diameter, Gdiplus::ARGB color,
                         float width = 1);
    void FillRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float diameter, Gdiplus::ARGB color);
    void FillGradient(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, Gdiplus::ARGB start, Gdiplus::ARGB end,
                      Gdiplus::LinearGradientMode mode = Gdiplus::LinearGradientModeVertical);
    void FillGradientRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float diameter,
                                 const Gdiplus::PointF& from, const Gdiplus::PointF& to, Gdiplus::ARGB start,
                                 Gdiplus::ARGB end);
    // mode: 0 none, 1 sunken, 2 raised, 3 solid, 4 dashed; any other value is the line color.
    void DrawBorder(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, Gdiplus::ARGB modeOrColor,
                    Gdiplus::ARGB color = 0, float width = 1);
    // Fixed corners and edges from the margins, stretched middle parts.
    void DrawNineGrid(Gdiplus::Graphics& g, Gdiplus::Image* image, const Gdiplus::Rect& source,
                      const Gdiplus::Rect& destination, int left, int top, int right, int bottom);
    // Fades the chosen edges of rect (moved by inset) into color over extent pixels.
    void DrawEdgeFade(Gdiplus::Graphics& g, Gdiplus::RectF rect, COLORREF color, int extent = 100, int inset = 6,
                      bool fadeTop = false, bool fadeLeft = false, bool fadeBottom = true, bool fadeRight = true);

    // ---------------------------------------------------------------------------
    // Text and controls

    // Text over a soft glow: the glyphs are drawn at five offsets, shrunk by
    // glowScale and enlarged again. A glow color of 0 draws plain text.
    void DrawGlowText(Gdiplus::Graphics& g, std::wstring_view text, const Gdiplus::Font& font,
                      const Gdiplus::RectF& rect, const Gdiplus::StringFormat& format, Gdiplus::ARGB textColor,
                      Gdiplus::ARGB glowColor, float glowScale = 3);
    // The same with a font family, falling back to the default font.
    void DrawGlowText(Gdiplus::Graphics& g, std::wstring_view text, std::wstring_view family, float sizePx,
                      int fontStyle, const Gdiplus::RectF& rect, Gdiplus::ARGB textColor, Gdiplus::ARGB glowColor,
                      Gdiplus::StringAlignment hAlign = Gdiplus::StringAlignmentNear,
                      Gdiplus::StringAlignment vAlign = Gdiplus::StringAlignmentCenter,
                      int flags = Gdiplus::StringFormatFlagsNoWrap, float glowScale = 3);
    // Translucent frame of edit-like controls: 1 rounded, 2 sunken, 3 flat.
    void DrawGlassFrame(Gdiplus::Graphics& g, float width, float height, int style, bool active);
    // Vector push button; state 0 normal, 1 hover, 2 pressed.
    void DrawButtonFrame(Gdiplus::Graphics& g, float width, float height, int state, COLORREF baseColor = 0xFFFFFF,
                         bool focused = false);

} // namespace exui

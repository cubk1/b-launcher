#include "ui/exui/Resources.h"
#include <cstring>
#include <limits>

namespace exui {
    using namespace Gdiplus;

    Surface::~Surface() {
        Reset();
    }
    void Surface::Reset() {
        m_bitmap.reset();
        if (m_dc && m_previous) SelectObject(m_dc, m_previous);
        if (m_dib) DeleteObject(m_dib);
        if (m_dc) DeleteDC(m_dc);
        m_dc = nullptr;
        m_dib = nullptr;
        m_previous = nullptr;
        m_bits = nullptr;
        m_width = m_height = 0;
    }
    bool Surface::Resize(int width, int height) {
        if (width == m_width && height == m_height && m_bitmap) return true;
        Reset();
        if (width <= 0 || height <= 0 || width > (std::numeric_limits<int>::max)() / 4) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        m_dc = CreateCompatibleDC(nullptr);
        if (!m_dc) return false;
        m_dib = CreateDIBSection(m_dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&m_bits), nullptr, 0);
        if (!m_dib || !m_bits) {
            Reset();
            return false;
        }
        m_previous = SelectObject(m_dc, m_dib);
        if (!m_previous || m_previous == HGDI_ERROR) {
            m_previous = nullptr;
            Reset();
            return false;
        }
        m_width = width;
        m_height = height;
        std::fill(Pixels().begin(), Pixels().end(), 0U);
        m_bitmap = std::make_unique<Bitmap>(width, height, width * 4, PixelFormat32bppPARGB,
                                            reinterpret_cast<BYTE*>(m_bits));
        if (m_bitmap->GetLastStatus() != Ok) {
            Reset();
            return false;
        }
        return true;
    }
    std::span<std::uint32_t> Surface::Pixels() noexcept {
        return {m_bits, static_cast<size_t>(m_width) * m_height};
    }
    std::span<const std::uint32_t> Surface::Pixels() const noexcept {
        return {m_bits, static_cast<size_t>(m_width) * m_height};
    }
    bool Surface::CopyFrom(const Surface& other) {
        if (this == &other) return m_bitmap != nullptr;
        if (!other.GetBitmap() || !Resize(other.Width(), other.Height())) return false;
        GdiFlush();
        std::copy(other.Pixels().begin(), other.Pixels().end(), Pixels().begin());
        return true;
    }
    void DrawWithOpacity(Graphics& g, Bitmap& bitmap, const Rect& destination, uint8_t opacity) {
        if (!opacity) return;
        ColorMatrix matrix{};
        for (int i = 0; i < 5; ++i)
            matrix.m[i][i] = 1;
        matrix.m[3][3] = opacity / 255.f;
        ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix);
        g.DrawImage(&bitmap, destination, 0, 0, bitmap.GetWidth(), bitmap.GetHeight(), UnitPixel, &attributes);
    }

    std::unique_ptr<Bitmap> DecodeImage(std::span<const uint8_t> bytes) {
        if (bytes.empty()) return nullptr;
        const auto& complete = bytes;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, complete.size());
        if (!memory) return nullptr;
        void* data = GlobalLock(memory);
        if (!data) {
            GlobalFree(memory);
            return nullptr;
        }
        std::memcpy(data, complete.data(), complete.size());
        GlobalUnlock(memory);
        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
            GlobalFree(memory);
            return nullptr;
        }
        std::unique_ptr<Bitmap> decoded(Bitmap::FromStream(stream));
        std::unique_ptr<Bitmap> result;
        if (decoded && decoded->GetLastStatus() == Ok) result = CopyBitmap(*decoded);
        decoded.reset();
        stream->Release();
        return result && result->GetLastStatus() == Ok ? std::move(result) : nullptr;
    }

    std::unique_ptr<Bitmap> CopyBitmap(Bitmap& source) {
        const int w = static_cast<int>(source.GetWidth()), h = static_cast<int>(source.GetHeight());
        if (w <= 0 || h <= 0) return {};
        auto copy = std::make_unique<Bitmap>(w, h, PixelFormat32bppPARGB);
        Rect rect(0, 0, w, h);
        BitmapData input{}, output{};
        if (source.LockBits(&rect, ImageLockModeRead, PixelFormat32bppPARGB, &input) != Ok) return {};
        if (copy->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppPARGB, &output) != Ok) {
            source.UnlockBits(&input);
            return {};
        }
        for (int y = 0; y < h; ++y)
            std::memcpy(static_cast<BYTE*>(output.Scan0) + static_cast<ptrdiff_t>(y) * output.Stride,
                        static_cast<const BYTE*>(input.Scan0) + static_cast<ptrdiff_t>(y) * input.Stride,
                        static_cast<size_t>(w) * 4);
        copy->UnlockBits(&output);
        source.UnlockBits(&input);
        if (source.GetHorizontalResolution() > 0 && source.GetVerticalResolution() > 0)
            copy->SetResolution(source.GetHorizontalResolution(), source.GetVerticalResolution());
        return copy;
    }

    RectF Adjust(RectF r, float dx, float dy, float dw, float dh) {
        return {r.X + dx, r.Y + dy, r.Width + dw, r.Height + dh};
    }
    void RoundedRectPath(GraphicsPath& path, const RectF& r, float d) {
        path.Reset();
        if (r.Width <= 0 || r.Height <= 0) return;
        if (d <= 0) {
            path.AddRectangle(RectF(r.X, r.Y, r.Width - 1, r.Height - 1));
            return;
        }
        const float right = r.X + r.Width - d - 1, bottom = r.Y + r.Height - d - 1;
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(right, r.Y, d, d, 270, 90);
        path.AddArc(right, bottom, d, d, 0, 90);
        path.AddArc(r.X, bottom, d, d, 90, 90);
        path.CloseFigure();
    }
    void DrawRoundedRect(Graphics& g, const RectF& r, float d, ARGB color, float width) {
        GraphicsPath path;
        RoundedRectPath(path, r, d);
        Pen pen(Color(color), width);
        g.DrawPath(&pen, &path);
    }
    void FillRoundedRect(Graphics& g, const RectF& r, float d, ARGB color) {
        GraphicsPath path;
        RoundedRectPath(path, r, d);
        SolidBrush brush{Color(color)};
        g.FillPath(&brush, &path);
    }
    void FillGradient(Graphics& g, const RectF& r, ARGB start, ARGB end, LinearGradientMode mode) {
        if (r.Width <= 1 || r.Height <= 1) return;
        LinearGradientBrush brush(r, Color(start), Color(end), mode);
        brush.SetWrapMode(WrapModeTileFlipXY);
        g.FillRectangle(&brush, RectF(r.X, r.Y, r.Width - 1, r.Height - 1));
    }
    void FillGradientRoundedRect(Graphics& g, const RectF& r, float d, const PointF& p1, const PointF& p2, ARGB start,
                                 ARGB end) {
        if (r.Width <= 1 || r.Height <= 1 || (p1.X == p2.X && p1.Y == p2.Y)) return;
        LinearGradientBrush brush(p1, p2, Color(start), Color(end));
        brush.SetWrapMode(WrapModeTileFlipXY);
        if (d > 0) {
            GraphicsPath path;
            RoundedRectPath(path, r, d);
            g.FillPath(&brush, &path);
        } else {
            g.FillRectangle(&brush, RectF(r.X, r.Y, r.Width - 1, r.Height - 1));
        }
    }
    void DrawBorder(Graphics& g, const RectF& r, ARGB mode, ARGB color, float width) {
        if (!mode || r.Width <= 0 || r.Height <= 0) return;
        const float x = r.X, y = r.Y, right = x + r.Width - 1, bottom = y + r.Height - 1;
        if (mode == 1 || mode == 2) {
            Pen top(Color(mode == 1 ? 0xA0000000U : 0xF5FFFFFFU), mode == 1 ? width : width / 2);
            Pen lower(Color(mode == 1 ? 0xF5FFFFFFU : 0xA8000000U), mode == 1 ? width / 2 : width);
            g.DrawLine(&top, x, y, right - (mode == 2 ? 1 : 0), y);
            g.DrawLine(&top, x, y, x, bottom - (mode == 2 ? 1 : 0));
            g.DrawLine(&lower, x, bottom, right, bottom);
            g.DrawLine(&lower, right, y, right, bottom - 1);
        } else {
            // Without a color, solid lines are gray and dashed (focus) lines translucent black.
            Pen pen(Color(mode == 3 || mode == 4 ? (color ? color
                                                            : mode == 4 ? 0x78000000U
                                                                        : ToArgb(0x646464))
                                                 : mode),
                    width);
            if (mode == 4) pen.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&pen, RectF(x, y, r.Width - 1, r.Height - 1));
        }
    }
    void DrawNineGrid(Graphics& g, Image* image, const Rect& src, const Rect& dst, int left, int top, int right,
                      int bottom) {
        if (!image || src.Width <= 0 || src.Height <= 0 || dst.Width <= 0 || dst.Height <= 0) return;
        // The corners keep their size even when the middle part would be negative.
        const int sx[]{src.X, src.X + left, src.GetRight() - right, src.GetRight()};
        const int sy[]{src.Y, src.Y + top, src.GetBottom() - bottom, src.GetBottom()};
        const int dx[]{dst.X, dst.X + left, dst.GetRight() - right, dst.GetRight()};
        const int dy[]{dst.Y, dst.Y + top, dst.GetBottom() - bottom, dst.GetBottom()};
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                g.DrawImage(image, Rect(dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]), sx[x], sy[y],
                            sx[x + 1] - sx[x], sy[y + 1] - sy[y], UnitPixel);
            }
    }
    void DrawEdgeFade(Graphics& g, RectF r, COLORREF color, int extent, int inset, bool fadeTop, bool fadeLeft,
                      bool fadeBottom, bool fadeRight) {
        r.X += inset;
        r.Y += inset;
        const int w = static_cast<int>(double(r.Width) - extent <= 1e-7 ? r.Width / 2 : extent);
        const int h = static_cast<int>(double(r.Height) - extent <= 1e-7 ? r.Height / 2 : extent);
        const ARGB solid = ToArgb(color), transparent = ToArgb(color, 0);
        if (fadeTop) FillGradient(g, RectF(r.X - inset, r.Y - inset, r.Width, float(h)), solid, transparent);
        if (fadeLeft)
            FillGradient(g, RectF(r.X - inset, r.Y - inset, float(w), r.Height), solid, transparent,
                         LinearGradientModeHorizontal);
        if (fadeBottom) FillGradient(g, RectF(r.X, r.Y + r.Height - h, r.Width, float(h + 1)), transparent, solid);
        if (fadeRight)
            FillGradient(g, RectF(r.X + r.Width - w, r.Y, float(w + 1), r.Height), transparent, solid,
                         LinearGradientModeHorizontal);
    }
    void DrawGlowText(Graphics& g, std::wstring_view text, const Font& font, const RectF& r, const StringFormat& format,
                      ARGB foreground, ARGB glow, float scale) {
        if (text.empty() || r.Width <= 0 || r.Height <= 0) return;
        const auto saved = g.Save();
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const int w = int(r.Width + 7), h = int(r.Height + 7);
        const int sw = scale > 0 ? int(w / scale) : 0, sh = scale > 0 ? int(h / scale) : 0;
        if (glow && sw > 0 && sh > 0) {
            Bitmap full(w, h, PixelFormat32bppARGB), shrunk(sw, sh, PixelFormat32bppARGB);
            {
                Graphics halo(&full);
                halo.Clear(Color(0));
                halo.SetSmoothingMode(SmoothingModeAntiAlias);
                halo.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                SolidBrush brush{Color(glow)};
                constexpr int ox[]{5, 3, 4, 4, 4}, oy[]{4, 4, 3, 5, 4};
                for (int i = 0; i < 5; ++i)
                    halo.DrawString(text.data(), INT(text.size()), &font,
                                    RectF(float(ox[i]), float(oy[i]), r.Width, r.Height), &format, &brush);
            }
            {
                Graphics shrink(&shrunk);
                shrink.Clear(Color(0));
                shrink.SetSmoothingMode(SmoothingModeAntiAlias);
                shrink.SetCompositingQuality(CompositingQualityGammaCorrected);
                shrink.SetInterpolationMode(InterpolationModeHighQualityBilinear);
                shrink.DrawImage(&full, RectF(0, 0, float(sw), float(sh)), 0, 0, float(w), float(h), UnitPixel);
            }
            g.DrawImage(&shrunk, RectF(r.X - 4, r.Y - 4, r.Width + 7, r.Height + 7), 0, 0, float(sw), float(sh),
                        UnitPixel);
        }
        SolidBrush brush{Color(foreground)};
        g.DrawString(text.data(), INT(text.size()), &font, r, &format, &brush);
        g.Restore(saved);
    }

    void DrawGlowText(Graphics& g, std::wstring_view text, std::wstring_view family, float size, int style,
                      const RectF& r, ARGB foreground, ARGB glow, StringAlignment hAlign, StringAlignment vAlign,
                      int flags, float scale) {
        if (size <= 0) return;
        // Font() would silently substitute a generic family for a missing one.
        const std::wstring name(family);
        FontFamily requested(name.empty() ? DefaultFontFamily().c_str() : name.c_str());
        FontFamily fallback(DefaultFontFamily().c_str());
        Font font(requested.GetLastStatus() == Ok ? &requested : &fallback, size, style, UnitPixel);
        const Font* used = font.GetLastStatus() == Ok ? &font : DefaultFont();
        if (!used) return;
        StringFormat format(flags);
        format.SetTrimming(StringTrimmingNone);
        format.SetAlignment(hAlign);
        format.SetLineAlignment(vAlign);
        DrawGlowText(g, text, *used, r, format, foreground, glow, scale);
    }

    void DrawGlassFrame(Graphics& g, float w, float h, int style, bool active) {
        const RectF r(0, 0, w, h);
        if (style == 0) return;
        const ARGB fill = active ? 0x6EFFFFFF : 0x3CFFFFFF;
        if (style == 1) {
            FillRoundedRect(g, Adjust(r, 2, 2, -4, -4), 4, fill);
            DrawRoundedRect(g, r, 4, 0x5A000000);
            DrawRoundedRect(g, Adjust(r, 1, 1, -2, -2), 4, 0x50FFFFFF);
        } else {
            SolidBrush brush{Color(fill)};
            g.FillRectangle(&brush, RectF(2, 2, w - 4, h - 4));
            if (style == 2) {
                for (int i = 0; i < 2; ++i) {
                    const BYTE alpha = i ? 50 : 90;
                    Pen dark(Color(alpha, 0, 0, 0)), light(Color(alpha, 255, 255, 255));
                    const float edge = float(i);
                    g.DrawLine(&dark, edge, edge, w - edge - 1, edge);
                    g.DrawLine(&dark, edge, edge, edge, h - edge - 1);
                    g.DrawLine(&light, edge, h - edge - 1, w - edge - 1, h - edge - 1);
                    g.DrawLine(&light, w - edge - 1, edge, w - edge - 1, h - edge - 1);
                }
            } else if (style == 3) {
                DrawBorder(g, r, 0x5A000000);
                DrawBorder(g, Adjust(r, 1, 1, -2, -2), 0x50FFFFFF);
            }
        }
    }
    void DrawButtonFrame(Graphics& g, float w, float h, int state, COLORREF base, bool focused) {
        // A light body under a gradient of the base color that deepens with the state.
        const auto saved = g.Save();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        const RectF whole(0, 0, w, h), r(0, 0, w - 1, h - 1);
        GraphicsPath clip;
        RoundedRectPath(clip, whole, 4);
        g.SetClip(&clip, CombineModeIntersect);
        FillRoundedRect(g, r, 3, 0xCCECECEC);
        const uint8_t first = state == 2 ? 153 : state == 1 ? 21 : 0;
        const uint8_t last = state == 2 ? 21 : state == 1 ? 153 : 102;
        FillGradient(g, r, ToArgb(base, first), ToArgb(base, last));
        g.Restore(saved);
        const auto outlines = g.Save();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        DrawRoundedRect(g, r, 3, state ? 0x46000000 : 0x50000000);
        DrawRoundedRect(g, Adjust(r, 1, 1, -2, -2), 3, 0x33FFFFFF);
        Pen shadow(Color(state == 2 ? 0x0A000000U : state == 1 ? 0x1E000000U : 0x19000000U));
        g.DrawLine(&shadow, 1.0f, h - 1, w - 3, h - 1);
        if (focused) DrawBorder(g, RectF(3, 3, w - 6, h - 6), 4, 0x78000000);
        g.Restore(outlines);
    }
} // namespace exui

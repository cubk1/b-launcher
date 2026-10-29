#include "ui/exui/Window.h"
#include "ui/exui/widgets/CaptionButton.h"

namespace exui {
    using namespace Gdiplus;

    namespace {
        // Four soft rings around translucent windows.
        void PaintShadow(Graphics& g, int w, int h, bool rounded) {
            constexpr int diameters[]{10, 12, 15, 18}, sharp[]{1, 4, 7, 11};
            constexpr BYTE alphas[]{60, 35, 15, 5};
            for (int i = 0; i < 4; ++i)
                DrawRoundedRect(g, RectF(float(5 - i), float(5 - i), float(w - 10 + i * 2), float(h - 10 + i * 2)),
                                float(rounded ? diameters[i] : sharp[i]), ARGB(alphas[i]) << 24);
        }

        void ClipToFrame(Graphics& g, int w, int h, int corner) {
            const HRGN region = CreateRoundRectRgn(6, 6, w - 5, h - 5, corner ? 4 : 0, corner ? 4 : 0);
            if (!region) return;
            Region clip(region);
            g.SetClip(&clip, CombineModeIntersect);
            DeleteObject(region);
        }
    } // namespace

    // Base color and skin, before the window alpha is applied.
    void Window::PaintSkin(Graphics& g) {
        const auto saved = g.Save();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        const Rect whole(0, 0, m_skin ? int(m_skin->GetWidth()) : 0, m_skin ? int(m_skin->GetHeight()) : 0);
        if (m_cornerRadius == 1) {
            // A bare popup surface: no base fill and no frame inset.
            if (m_skin && m_skinMode == SkinBackgroundMode::NineGrid)
                DrawNineGrid(g, m_skin.get(), whole, {0, 0, m_width, m_height}, m_skinMargins.left, m_skinMargins.top,
                             m_skinMargins.right, m_skinMargins.bottom);
            else if (m_skin)
                g.DrawImage(m_skin.get(), 0, 0);
            g.Restore(saved);
            return;
        }
        ClipToFrame(g, m_width, m_height, m_cornerRadius);
        // Without a skin the base color carries the window alpha itself.
        SolidBrush base{Color(ToArgb(m_baseColor, m_skin ? 255 : uint8_t(m_windowAlpha)))};
        const RectF content(5, 5, float(m_width - 10), float(m_height - 10));
        g.FillRectangle(&base, content);
        if (m_skin) {
            switch (m_skinMode) {
                case SkinBackgroundMode::Stretch: g.DrawImage(m_skin.get(), content); break;
                case SkinBackgroundMode::Tile: {
                    TextureBrush brush(m_skin.get(), WrapModeTile);
                    brush.TranslateTransform(5, 5);
                    g.FillRectangle(&brush, content);
                    break;
                }
                case SkinBackgroundMode::NineGrid:
                    DrawNineGrid(g, m_skin.get(), whole, Rect(5, 5, m_width - 10, m_height - 10), m_skinMargins.left,
                                 m_skinMargins.top, m_skinMargins.right, m_skinMargins.bottom);
                    break;
                case SkinBackgroundMode::EdgeFade: {
                    // Natural size at the top left; the bottom and right edges fade into the base color.
                    const RectF natural(5, 5, float(whole.Width), float(whole.Height));
                    g.DrawImage(m_skin.get(), natural);
                    DrawEdgeFade(g, RectF(0, 0, natural.Width, natural.Height), m_baseColor, 100, 6, false, false, true,
                                 true);
                    break;
                }
                case SkinBackgroundMode::BaseColorOnly: break;
            }
        }
        g.Restore(saved);
    }

    // Skin, icon, frame, shadow and title: everything under the widgets.
    void Window::PaintScene(Graphics& g, bool includeForeground) {
        g.Clear(Color(0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        const bool framed = m_cornerRadius != 1;
        if (m_windowAlpha < 255 && framed && m_skin) {
            // The window alpha fades the skin layer only.
            Bitmap layer(PhysicalWidth(), PhysicalHeight(), PixelFormat32bppPARGB);
            {
                Graphics lg(&layer);
                lg.Clear(Color(0));
                if (m_scale != 1) {
                    lg.ScaleTransform(m_scale, m_scale);
                    lg.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                }
                PaintSkin(lg);
            }
            ColorMatrix matrix{};
            for (int i = 0; i < 5; ++i)
                matrix.m[i][i] = 1;
            matrix.m[3][3] = m_windowAlpha / 255.f;
            ImageAttributes attributes;
            attributes.SetColorMatrix(&matrix);
            g.DrawImage(&layer, RectF(0, 0, float(m_width), float(m_height)), 0, 0, float(PhysicalWidth()),
                        float(PhysicalHeight()), UnitPixel, &attributes);
        } else {
            PaintSkin(g);
        }
        const auto frame = g.Save();
        if (framed) ClipToFrame(g, m_width, m_height, m_cornerRadius);
        if (framed && (m_captionMask & CaptionButtonMask::Icon) && m_icon) {
            // GDI draws the icon straight onto the premultiplied DIB.
            g.Flush(FlushIntentionSync);
            DrawIconEx(m_scene.GetDC(), ToPhysical(11), ToPhysical(11), m_icon, ToPhysical(16), ToPhysical(16), 0,
                       nullptr, DI_NORMAL);
            GdiFlush();
        }
        PaintClientBackground(g);
        if (framed) {
            DrawRoundedRect(g, RectF(6, 6, float(m_width - 12), float(m_height - 12)), m_cornerRadius ? 7.f : 0.f,
                            0x66000000);
            DrawRoundedRect(g, RectF(7, 7, float(m_width - 14), float(m_height - 14)), m_cornerRadius ? 6.f : 0.f,
                            0x66FFFFFF);
        }
        g.Restore(frame);
        if (m_windowAlpha < 255 && framed) {
            const auto shadow = g.Save();
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            PaintShadow(g, m_width, m_height, m_cornerRadius != 0);
            g.Restore(shadow);
        }
        // Keep the background for the contrast checks of the title and labels.
        g.Flush(FlushIntentionSync);
        const auto pixels = m_scene.Pixels();
        m_backgroundPixels.assign(pixels.begin(), pixels.end());
        PaintTitle(g);
        if (includeForeground) PaintClientForeground(g);
    }

    void Window::PaintTitle(Graphics& g) {
        if (!(m_captionMask & CaptionButtonMask::Title) || m_title.empty()) return;
        const int x = (m_captionMask & CaptionButtonMask::Icon) ? 29 : 11;
        const int right = m_captionButtons.empty() ? m_width - 5 : m_captionButtons.back()->GetX();
        const RectF rect(float(x), 11, float(std::max(0, right - x)), 18);
        const ARGB color = ContrastTextColor(rect, 0xFAFFFFFF);
        DrawGlowText(g, m_title, DefaultFontFamily(), 12, FontStyleRegular, rect, color, GlowColorFor(color));
    }

    // Samples nine points of the background under rect.
    ARGB Window::ContrastTextColor(const RectF& rect, ARGB color) const {
        int dark = 0;
        const int width = PhysicalWidth(), height = PhysicalHeight();
        for (double fy : {0.2, 0.5, 0.8})
            for (double fx : {0.2, 0.5, 0.8}) {
                const int x = ToPhysical(float(static_cast<int>(rect.X + rect.Width * fx)));
                const int y = ToPhysical(float(static_cast<int>(rect.Y + rect.Height * fy)));
                if (x >= 0 && y >= 0 && x < width && y < height &&
                    m_backgroundPixels.size() == static_cast<size_t>(width) * height &&
                    IsDark(m_backgroundPixels[static_cast<size_t>(y) * width + x]))
                    ++dark;
            }
        return ToArgb(dark > 4 ? 0xFFFFFF : 0, static_cast<uint8_t>(color >> 24));
    }

    void Window::RequestFrame() {
        m_frameDirty = true;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Window::RenderFrame(bool includeWidgets, bool present) {
        if (m_rendering) return;
        // During a window effect the captured image is shown; nothing is drawn live.
        if (m_animator.IsActive()) {
            if (present) Present();
            return;
        }
        if (!m_canvas.Resize(PhysicalWidth(), PhysicalHeight())) return;
        m_rendering = true;
        if (m_sceneDirty || m_scene.Width() != m_canvas.Width() || m_scene.Height() != m_canvas.Height() ||
            m_sceneHasForeground != includeWidgets) {
            m_sceneDirty = false;
            if (m_scene.Resize(m_canvas.Width(), m_canvas.Height())) {
                Graphics g(m_scene.GetBitmap());
                if (m_scale != 1) {
                    g.ScaleTransform(m_scale, m_scale);
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                }
                PaintScene(g, includeWidgets);
                g.Flush(FlushIntentionSync);
            }
            m_sceneHasForeground = includeWidgets;
        }
        m_frameDirty = false; // widgets may invalidate themselves again while painting
        m_canvas.CopyFrom(m_scene);
        const RECT all{0, 0, m_canvas.Width(), m_canvas.Height()};
        m_chrome->Composite(m_canvas.GetDC(), m_scale, {0, 0}, all);
        if (includeWidgets) m_root->Composite(m_canvas.GetDC(), m_scale, {0, 0}, all);
        GdiFlush();
        if (present) Present();
        m_rendering = false;
    }

    void Window::Present() {
        if (m_animator.IsOpeningPrepared()) {
            PresentSurface(m_animator.Presentation());
            return;
        }
        const Surface* source = &m_canvas;
        if (m_animator.IsActive()) {
            if (m_aeroEnabled && m_cornerRadius != 1)
                if (const auto region = m_animator.PendingBlurRegion(PhysicalWidth(), PhysicalHeight()))
                    ApplyBlurBehind(true, *region, ToPhysical(6));
            if (const auto* composed = m_animator.Compose(PhysicalWidth(), PhysicalHeight())) source = composed;
        }
        PresentSurface(*source);
    }

    void Window::PresentSurface(const Surface& surface) {
        GdiFlush();
        if (!m_hwnd) return;
        // A null destination keeps the position; no screen DC is needed per frame.
        POINT origin{};
        SIZE size{surface.Width(), surface.Height()};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(m_hwnd, nullptr, nullptr, &size, surface.GetDC(), &origin, 0, &blend, ULW_ALPHA);
    }

    std::unique_ptr<Bitmap> Window::RenderToBitmap() {
        RenderFrame();
        return m_canvas.GetBitmap() ? CopyBitmap(*m_canvas.GetBitmap()) : nullptr;
    }

    std::unique_ptr<Bitmap> Window::RenderPresentationToBitmap() {
        RenderFrame();
        auto* bitmap = m_animator.IsActive() ? m_animator.Presentation().GetBitmap() : m_canvas.GetBitmap();
        return bitmap ? CopyBitmap(*bitmap) : nullptr;
    }

} // namespace exui

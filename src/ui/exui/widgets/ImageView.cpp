#include "ui/exui/widgets/ImageView.h"
#include "ui/exui/widgets/Label.h"

#include <algorithm>
#include <cmath>

namespace exui {

    void ImageView::SetImage(std::unique_ptr<Gdiplus::Bitmap> value, bool resetView) {
        image = std::move(value);
        SetTabStop(image != nullptr);
        if (resetView) ResetView();
        Invalidate();
    }

    void ImageView::ResetView() {
        zoom = 1;
        panX = panY = 0;
        dragging = false;
        Invalidate();
    }

    void ImageView::ClampPan() {
        if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) return;
        float width = (float) std::max(0, GetWidth() - 20), height = (float) std::max(0, GetHeight() - 20);
        float scale = std::min(width / image->GetWidth(), height / image->GetHeight()) * zoom;
        float maxX = std::max(0.0f, (image->GetWidth() * scale - width) * 0.5f);
        float maxY = std::max(0.0f, (image->GetHeight() * scale - height) * 0.5f);
        panX = std::clamp(panX, -maxX, maxX);
        panY = std::clamp(panY, -maxY, maxY);
    }

    bool ImageView::OnMouseDown(int x, int y, UINT button) {
        if (!image || button != VK_LBUTTON) return false;
        dragging = true;
        dragPoint = {x, y};
        return true;
    }

    bool ImageView::OnMouseMove(int x, int y, UINT) {
        if (!image) return false;
        SetCursor(LoadCursorW(nullptr, zoom > 1 ? IDC_SIZEALL : IDC_ARROW));
        if (dragging) {
            panX += x - dragPoint.x;
            panY += y - dragPoint.y;
            dragPoint = {x, y};
            ClampPan();
            Invalidate();
        }
        return true;
    }

    bool ImageView::OnMouseUp(int, int, UINT button) {
        if (button != VK_LBUTTON || !dragging) return false;
        dragging = false;
        return true;
    }

    void ImageView::OnMouseCancel() { dragging = false; }

    bool ImageView::OnMouseWheel(int delta, int x, int y) {
        if (!image) return false;
        float next = std::clamp(zoom * std::pow(1.2f, (float) delta / WHEEL_DELTA), 1.0f, 32.0f);
        float ratio = next / zoom;
        panX = panX * ratio + (x - GetWidth() * 0.5f) * (1 - ratio);
        panY = panY * ratio + (y - GetHeight() * 0.5f) * (1 - ratio);
        zoom = next;
        ClampPan();
        Invalidate();
        return true;
    }

    bool ImageView::OnKeyDown(UINT key, UINT) {
        if (key != '0' && key != VK_HOME) return false;
        ResetView();
        return true;
    }

    void ImageView::OnPaint(Gdiplus::Graphics& g) {
        using namespace Gdiplus;
        if (GetWidth() < 2 || GetHeight() < 2) return;
        SolidBrush background(Color(0xD2FFFFFF));
        Pen border(Color(0x50000000));
        g.FillRectangle(&background, Rect(0, 0, GetWidth(), GetHeight()));
        g.DrawRectangle(&border, Rect(0, 0, GetWidth() - 1, GetHeight() - 1));
        RectF content(10, 10, (REAL) std::max(0, GetWidth() - 20), (REAL) std::max(0, GetHeight() - 20));
        if (image && image->GetWidth() > 0 && image->GetHeight() > 0) {
            ClampPan();
            float scale = std::min(content.Width / image->GetWidth(), content.Height / image->GetHeight()) * zoom;
            RectF target(content.X + (content.Width - image->GetWidth() * scale) * 0.5f + panX,
                         content.Y + (content.Height - image->GetHeight() * scale) * 0.5f + panY,
                         image->GetWidth() * scale, image->GetHeight() * scale);
            auto state = g.Save();
            g.SetClip(content, CombineModeIntersect);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            ImageAttributes attributes;
            attributes.SetWrapMode(WrapModeTileFlipXY);
            g.DrawImage(image.get(), target, 0, 0, (REAL) image->GetWidth(), (REAL) image->GetHeight(), UnitPixel,
                        &attributes);
            g.Restore(state);
        } else {
            StringFormat format;
            format.SetAlignment(StringAlignmentCenter);
            format.SetLineAlignment(StringAlignmentCenter);
            format.SetTrimming(StringTrimmingEllipsisCharacter);
            widget_detail::DrawText(g, *this, content, format, GetFontStyle(), GetTextColor());
        }
    }
}

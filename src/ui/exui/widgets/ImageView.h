#pragma once

#include "ui/exui/Widget.h"

namespace exui {

    // Fits the whole image without changing its aspect ratio. Caption is shown
    // when no image is available, for empty/loading/error states.
    class ImageView : public Widget {
    public:
        void SetImage(std::unique_ptr<Gdiplus::Bitmap> image, bool resetView = true);
        void ResetView();
        void OnPaint(Gdiplus::Graphics& graphics) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        bool OnMouseWheel(int delta, int x, int y) override;
        bool OnKeyDown(UINT key, UINT flags) override;
        void OnMouseCancel() override;

    private:
        void ClampPan();
        std::unique_ptr<Gdiplus::Bitmap> image;
        float zoom = 1, panX = 0, panY = 0;
        POINT dragPoint{};
        bool dragging = false;
    };
}

#include "ui/exui/widgets/ImageList.h"

namespace exui {
    using namespace Gdiplus;

    bool ImageList::Create(int width, int height, UINT flags) {
        Destroy();
        m_handle = ImageList_Create(width, height, flags, 0, 0);
        if (m_handle) ImageList_SetBkColor(m_handle, 0xFFFFFF);
        return m_handle != nullptr;
    }
    void ImageList::Destroy() {
        if (m_handle) ImageList_Destroy(m_handle);
        m_handle = nullptr;
    }
    Size ImageList::GetSize() const {
        int width = 0, height = 0;
        if (m_handle) ImageList_GetIconSize(m_handle, &width, &height);
        return {width, height};
    }
    std::unique_ptr<Bitmap> ImageList::GetImage(int index) const {
        IMAGEINFO info{};
        if (!m_handle || !ImageList_GetImageInfo(m_handle, index, &info)) return {};
        // The image's part of the list bitmap, taken as premultiplied pixels.
        Bitmap atlas(info.hbmImage, nullptr);
        const Rect area(int(info.rcImage.left), int(info.rcImage.top), int(info.rcImage.right - info.rcImage.left),
                        int(info.rcImage.bottom - info.rcImage.top));
        std::unique_ptr<Bitmap> crop(atlas.Clone(area, atlas.GetPixelFormat()));
        if (!crop) return {};
        Rect bounds(0, 0, crop->GetWidth(), crop->GetHeight());
        BitmapData bits{};
        if (crop->LockBits(&bounds, ImageLockModeRead, crop->GetPixelFormat(), &bits) != Ok) return {};
        std::unique_ptr<Bitmap> image;
        {
            Bitmap pargb(bounds.Width, bounds.Height, bits.Stride, PixelFormat32bppPARGB,
                         static_cast<BYTE*>(bits.Scan0));
            image = CopyBitmap(pargb);
        }
        crop->UnlockBits(&bits);
        return image;
    }

} // namespace exui

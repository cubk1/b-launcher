#pragma once
#include "ui/exui/Resources.h"
#include <commctrl.h>

namespace exui {
    // Nonvisual comctl image list (0x500494), never a native child control.
    class ImageList {
    public:
        ~ImageList() {
            Destroy();
        }
        ImageList() = default;
        ImageList(const ImageList&) = delete;
        ImageList& operator=(const ImageList&) = delete;
        bool Create(int width, int height, UINT flags = ILC_COLOR32);
        void Destroy();
        int AddBitmap(HBITMAP bitmap, HBITMAP mask = nullptr) {
            return m_handle ? ImageList_Add(m_handle, bitmap, mask) : -1;
        }
        int AddIcon(HICON icon) {
            return m_handle ? ImageList_AddIcon(m_handle, icon) : -1;
        }
        bool Remove(int index) {
            return m_handle && ImageList_Remove(m_handle, index);
        }
        int GetCount() const {
            return m_handle ? ImageList_GetImageCount(m_handle) : 0;
        }
        Gdiplus::Size GetSize() const;
        HIMAGELIST GetHandle() const noexcept {
            return m_handle;
        }
        std::unique_ptr<Gdiplus::Bitmap> GetImage(int index) const;

    private:
        HIMAGELIST m_handle = nullptr;
    };
} // namespace exui

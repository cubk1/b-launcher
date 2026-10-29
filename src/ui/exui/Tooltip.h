#pragma once
#include "ui/exui/Resources.h"

namespace exui {

    // Theme-drawn tooltip in its own top-level window, without a common-control
    // HWND. It appears after the double-click time and hides ten times later.
    class Tooltip {
    public:
        Tooltip();
        ~Tooltip();
        Tooltip(const Tooltip&) = delete;
        Tooltip& operator=(const Tooltip&) = delete;

        // Restarts the delay unless the same subject and text are already pending or shown.
        void Schedule(HWND owner, const void* subject, std::wstring text);
        void Hide(HWND owner);
        // Returns false for timers that do not belong to the tooltip.
        bool OnTimer(HWND owner, UINT_PTR id);
        UINT_PTR TimerId() const noexcept {
            return reinterpret_cast<UINT_PTR>(&m_timerTag);
        }
        HWND Hwnd() const noexcept;
        std::unique_ptr<Gdiplus::Bitmap> Snapshot() const;

    private:
        struct Popup;
        std::unique_ptr<Popup> m_popup;
        BYTE m_timerTag = 0;
    };

} // namespace exui

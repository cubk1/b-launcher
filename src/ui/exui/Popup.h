#pragma once
#include "ui/exui/Window.h"

namespace exui {

    // A frameless top-level surface for drop-down lists and menus; its content is
    // an ordinary widget tree. It takes the keyboard and closes on Escape or when
    // another window is activated.
    class Popup : public Window {
    public:
        ~Popup() override;
        // screenBounds are physical pixels; the content is laid out at the owner's
        // scale. focus receives the keyboard.
        bool Open(HWND owner, RECT screenBounds, Widget* focus = nullptr);
        // Closes at once and then runs Closed. Focus returns to the owner if the popup had it.
        void Close();
        // Closes once the current message is handled, for use inside input handlers.
        void RequestClose();
        bool IsOpen() const noexcept {
            return GetHwnd() != nullptr;
        }
        std::function<void()> Closed;

    protected:
        LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;

    private:
        bool m_closing = false;
    };

} // namespace exui

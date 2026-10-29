#pragma once
#include "ui/exui/Popup.h"
#include <string>
#include <vector>

namespace exui {
    class MenuList;

    struct MenuItem {
        int command = 0;
        std::wstring text;
        bool enabled = true, checked = false, separator = false;
    };

    // A context menu in a popup: 22px items and 3px separators inside 8px of
    // padding, an icon rail on the left and a check mark for checked items.
    class Menu {
    public:
        Menu();
        ~Menu();
        void SetItems(std::vector<MenuItem> items);
        const std::vector<MenuItem>& GetItems() const noexcept {
            return m_items;
        }
        Gdiplus::Size Measure() const;
        // Draws the menu with one item highlighted.
        void Paint(Gdiplus::Graphics& g, int width, int highlighted = -1) const;
        // Opens toward the bottom right of the cursor, flipping at the work-area edges.
        static POINT PositionAtCursor(POINT cursor, Gdiplus::Size size, const RECT& workArea);
        bool Show(HWND owner, POINT screenPoint);
        void Close();
        HWND GetHwnd() const noexcept {
            return m_popup.GetHwnd();
        }
        // Runs after the menu has closed.
        std::function<void(int)> OnCommand;

    private:
        friend class MenuList;
        int HitTest(int x, int y) const;
        void Activate(int index);
        std::vector<MenuItem> m_items;
        Popup m_popup;
        MenuList* m_list = nullptr;
    };

} // namespace exui

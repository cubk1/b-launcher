#pragma once
#include "ui/exui/Popup.h"
#include <functional>

namespace exui {
    class DropdownList;

    // A drop-down list box: a skinned frame showing the selected item. The list
    // opens in a popup under the box, or above it near the bottom of the screen.
    class ComboBox : public Widget {
    public:
        ComboBox();
        ComboBox(int x, int y, int width, int height);
        ~ComboBox() override;
        // Also clears the selection.
        void ClearItems();
        void AddItem(std::wstring text);
        int GetItemCount() const noexcept {
            return int(m_items.size());
        }
        // Invalid indices leave the selection unchanged. Does not notify.
        void SetSelectedIndex(int index);
        int GetSelectedIndex() const noexcept {
            return m_selectedIndex;
        }
        void SetSelectedIndex1Based(int index) {
            if (index > 0) SetSelectedIndex(index - 1);
        }
        int GetSelectedIndex1Based() const noexcept {
            return m_selectedIndex + 1;
        }
        std::wstring GetItemText(int index) const;
        std::wstring GetSelectedText() const {
            return m_caption;
        }
        // After the user picked an item; the list is already closed.
        std::function<void(int)> OnSelectionChanged;

        bool OpenDropdown();
        void CloseDropdown();
        bool IsDropped() const noexcept {
            return m_popup.IsOpen();
        }
        // 18px rows plus 6px of frame; 130px from seven items on (and when empty).
        int GetDropdownHeight() const noexcept;
        // Draws the list as it opens, for previews.
        void PaintDropdown(Gdiplus::Graphics& g);
        Gdiplus::Rect GetArrowBounds() const;

        void OnPaint(Gdiplus::Graphics& g) override;
        void OnFocusChanged(bool focused) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        // The wheel changes the selection only while the box has the focus.
        bool OnMouseWheel(int delta, int x, int y) override;
        bool OnKeyDown(UINT vk, UINT flags) override;

    private:
        friend class DropdownList;
        void Pick(int index);
        void StepSelection(int index);
        std::vector<std::wstring> m_items;
        int m_selectedIndex = -1, m_pendingSelection = -1;
        Popup m_popup;
        DropdownList* m_list = nullptr;
        // Callbacks may destroy the combo box.
        std::shared_ptr<char> m_lifetime = std::make_shared<char>();
    };

} // namespace exui

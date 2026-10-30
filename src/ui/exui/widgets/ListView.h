#pragma once
#include "ui/exui/Widget.h"
#include "ui/exui/widgets/ScrollBar.h"
#include "ui/exui/widgets/ImageList.h"
#include <functional>
#include <string>
#include <vector>
#include <commctrl.h>

namespace exui {
    struct ListColumn {
        std::wstring title;
        int width = 100;
    };

    // A report-style list: a translucent white header, rows over a white
    // background, and hover and selection in the theme color (alpha 51 and 102).
    // Header and row heights follow the font unless set.
    class ListView : public Widget {
    public:
        ListView();
        ListView(int x, int y, int width, int height);
        ~ListView() override;
        void InsertColumn(std::wstring title, int width);
        int InsertRow(const std::vector<std::wstring>& cellTexts = {}, const std::vector<int>& images = {});
        void SetImageList(std::shared_ptr<ImageList> images) {
            m_images = std::move(images);
            Invalidate();
        }
        void SetCellImage(int row, int column, int imageIndex);
        int GetCellImage(int row, int column) const;
        void SetCellText(int rowIndex, int colIndex, std::wstring text);
        std::wstring GetCellText(int rowIndex, int colIndex) const;
        void ClearRows();
        int GetRowCount() const noexcept {
            return static_cast<int>(m_rows.size());
        }
        // Selects one row (an invalid index clears the selection). Does not notify.
        void SetSelectedRow(int index);
        int GetSelectedRow() const noexcept {
            return m_selectedRow;
        }
        void SetSingleSelection(bool single) noexcept {
            m_singleSelection = single;
        }
        bool IsSingleSelection() const noexcept {
            return m_singleSelection;
        }
        std::vector<int> GetSelectedRows() const;
        // Selects these rows, the first being the current one, and no others; invalid indices are skipped. Does not
        // notify.
        void SetSelectedRows(const std::vector<int>& rows);
        bool IsRowSelected(int row) const noexcept {
            return row >= 0 && row < GetRowCount() && m_rowStates[row];
        }
        void EnsureRowVisible(int row);
        void SetHeaderVisible(bool visible) noexcept;
        void SetRowHeight(int heightPx) noexcept;
        void SetHeaderHeight(int heightPx) noexcept;
        int GetRowHeight() const;
        int GetHeaderHeight() const;
        void SetBackgroundColor(Gdiplus::ARGB value) {
            m_background = value;
            Invalidate();
        }
        void SetBorderColor(Gdiplus::ARGB value) {
            m_border = value;
            Invalidate();
        }
        void SetRowColors(Gdiplus::ARGB hover, Gdiplus::ARGB selected, Gdiplus::ARGB hoverText = 0,
                          Gdiplus::ARGB selectedText = 0);
        // Hover and selection colors from a theme color (usually the window's base color).
        void SetThemeColor(COLORREF value);
        void ScrollTo(int pixels);
        int GetScrollPosition() const noexcept {
            return m_scrollY;
        }
        int GetHorizontalScrollPosition() const noexcept {
            return m_scrollX;
        }
        ScrollBar& GetVerticalScrollBar() {
            Layout();
            return *m_vScrollBar;
        }
        ScrollBar& GetHorizontalScrollBar() {
            Layout();
            return *m_hScrollBar;
        }
        // Original vtable+316 fires on left release over the current selection,
        // including releases without a prior press. -1 denotes the native 0/0
        // match (no selected row and no row hit); callers must handle that value.
        std::function<void(int)> OnSelectionChanged;
        void OnPaint(Gdiplus::Graphics& g) override;
        void OnMouseLeave() override;
        void OnMouseCancel() override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        bool OnMouseWheel(int delta, int x, int y) override;
        bool OnKeyDown(UINT vk, UINT flags) override;

    private:
        void Layout();
        int RowAt(int x, int y) const;
        int ColumnAt(int x, int y) const;
        // Applies a click or key: plain selects one row, Ctrl toggles, Shift selects a range
        // from the anchor. keepSelected leaves an existing multi-selection (right click).
        bool SelectRows(int row, bool control, bool shift, bool keepSelected = false);
        std::vector<ListColumn> m_columns;
        std::vector<std::vector<std::wstring>> m_rows;
        std::vector<std::vector<int>> m_rowImages;
        std::shared_ptr<ImageList> m_images;
        std::vector<uint8_t> m_rowStates; // 1 = selected
        bool m_singleSelection = true;
        int m_anchorRow = -1; // start of a Shift range
        int m_headerHeight = 0, m_rowHeight = 0;
        bool m_headerVisible = true, m_vVisible = false, m_hVisible = false;
        int m_selectedRow = -1, m_hoveredRow = -1, m_hoveredColumn = -1, m_pressedColumn = -1;
        int m_scrollX = 0, m_scrollY = 0, m_totalWidth = 0, m_contentWidth = 0;
        Gdiplus::Rect m_viewport{};
        Gdiplus::ARGB m_background = 0xD2FFFFFF, m_border = 0x50000000;
        Gdiplus::ARGB m_hoverColor = 0x33FFFFFF, m_selectedColor = 0x66FFFFFF, m_hoverText = 0, m_selectedText = 0;
        ScrollBar* m_vScrollBar = nullptr;
        ScrollBar* m_hScrollBar = nullptr;
    };
} // namespace exui

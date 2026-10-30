#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/Window.h"
#include <cmath>
#include <limits>

namespace exui {
    using namespace Gdiplus;
    namespace {
        bool Contains(const RectF& r, int x, int y) {
            return r.Contains(static_cast<float>(x), static_cast<float>(y));
        }
        int BoundedAdd(int value, int amount) {
            return static_cast<int>(std::clamp(static_cast<long long>(value) + amount, static_cast<long long>(INT_MIN),
                                               static_cast<long long>(INT_MAX)));
        }
    } // namespace

    ListView::ListView() {
        SetTabStop(true);
        m_vScrollBar = &Add<ScrollBar>();
        m_hScrollBar = &Add<ScrollBar>(0, 0, 0, 10, false);
        m_vScrollBar->SetVisible(false);
        m_hScrollBar->SetVisible(false);
        m_vScrollBar->OnScroll = [this](int pos) {
            m_scrollY = pos;
            Invalidate();
        };
        m_hScrollBar->OnScroll = [this](int pos) {
            m_scrollX = pos;
            Invalidate();
        };
    }
    ListView::ListView(int x, int y, int w, int h) : ListView() {
        SetBounds(x, y, w, h);
    }
    ListView::~ListView() = default;
    void ListView::InsertColumn(std::wstring title, int width) {
        m_columns.push_back({std::move(title), std::max(0, width)});
        for (auto& row : m_rows)
            row.resize(m_columns.size());
        for (auto& row : m_rowImages)
            row.resize(m_columns.size(), -1);
        Layout();
        Invalidate();
    }
    int ListView::InsertRow(const std::vector<std::wstring>& values, const std::vector<int>& images) {
        auto row = values;
        row.resize(m_columns.size());
        m_rows.push_back(std::move(row));
        auto icons = images;
        icons.resize(m_columns.size(), -1);
        m_rowImages.push_back(std::move(icons));
        m_rowStates.push_back(0);
        Layout();
        Invalidate();
        return static_cast<int>(m_rows.size()) - 1;
    }
    void ListView::SetCellText(int row, int col, std::wstring value) {
        if (row < 0 || row >= GetRowCount() || col < 0 || col >= static_cast<int>(m_columns.size())) return;
        m_rows[row][col] = std::move(value);
        Invalidate();
    }
    void ListView::SetCellImage(int row, int column, int index) {
        if (row < 0 || row >= GetRowCount() || column < 0 || column >= int(m_columns.size())) return;
        m_rowImages[row][column] = index;
        Invalidate();
    }
    int ListView::GetCellImage(int row, int column) const {
        return row >= 0 && row < GetRowCount() && column >= 0 && column < int(m_columns.size())
                ? m_rowImages[row][column]
                : -1;
    }
    std::wstring ListView::GetCellText(int row, int col) const {
        if (row < 0 || row >= GetRowCount() || col < 0 || col >= static_cast<int>(m_columns.size())) return {};
        return m_rows[row][col];
    }
    void ListView::ClearRows() {
        OnMouseCancel();
        SetTooltipText(L"");
        m_rows.clear();
        m_rowImages.clear();
        m_rowStates.clear();
        m_selectedRow = m_hoveredRow = m_anchorRow = -1;
        // Back to the top left; the columns stay.
        m_scrollX = m_scrollY = 0;
        Layout();
        Invalidate();
    }
    void ListView::SetSelectedRow(int row) {
        std::fill(m_rowStates.begin(), m_rowStates.end(), uint8_t(0));
        m_selectedRow = m_anchorRow = row >= 0 && row < GetRowCount() ? row : -1;
        if (m_selectedRow >= 0) m_rowStates[m_selectedRow] = 1;
        Invalidate();
    }
    void ListView::SetSelectedRows(const std::vector<int>& rows) {
        std::fill(m_rowStates.begin(), m_rowStates.end(), uint8_t(0));
        m_selectedRow = m_anchorRow = -1;
        for (int row : rows) {
            if (row < 0 || row >= GetRowCount()) continue;
            m_rowStates[row] = 1;
            if (m_selectedRow < 0) m_selectedRow = m_anchorRow = row;
        }
        Invalidate();
    }
    void ListView::EnsureRowVisible(int row) {
        if (row < 0 || row >= GetRowCount()) return;
        Layout();
        const int top = row * GetRowHeight(), bottom = top + GetRowHeight();
        if (top < m_scrollY)
            ScrollTo(top);
        else if (bottom > m_scrollY + m_viewport.Height)
            ScrollTo(bottom - m_viewport.Height);
    }
    void ListView::SetHeaderVisible(bool value) noexcept {
        m_headerVisible = value;
        Invalidate();
    }
    void ListView::SetRowHeight(int value) noexcept {
        m_rowHeight = std::max(1, value);
        Invalidate();
    }
    void ListView::SetHeaderHeight(int value) noexcept {
        m_headerHeight = std::max(1, value);
        Invalidate();
    }
    int ListView::GetHeaderHeight() const {
        if (!m_headerVisible) return 0;
        if (m_headerHeight > 0) return m_headerHeight;
        Bitmap b(1, 1, PixelFormat32bppPARGB);
        Graphics g(&b);
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        return static_cast<int>(font->GetHeight(&g) + 7);
    }
    int ListView::GetRowHeight() const {
        if (m_rowHeight > 0) return m_rowHeight;
        Bitmap b(1, 1, PixelFormat32bppPARGB);
        Graphics g(&b);
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        // Tall enough for the text and the row images.
        return std::max(static_cast<int>(font->GetHeight(&g) + 3), m_images ? m_images->GetSize().Height : 0) + 2;
    }
    void ListView::SetRowColors(ARGB hover, ARGB selected, ARGB hoverText, ARGB selectedText) {
        m_hoverColor = hover;
        m_selectedColor = selected;
        m_hoverText = hoverText;
        m_selectedText = selectedText;
        Invalidate();
    }
    void ListView::SetThemeColor(COLORREF value) {
        SetRowColors(ToArgb(value, 51), ToArgb(value, 102));
    }
    void ListView::Layout() {
        const int w = std::max(0, GetWidth() - 4), h = std::max(0, GetHeight() - 4 - GetHeaderHeight());
        long long total = 0;
        for (const auto& column : m_columns)
            total += column.width;
        m_totalWidth = static_cast<int>(std::min<long long>(total, INT_MAX));
        // At least the client width; columns wider than that get 10px of extra room.
        m_contentWidth = total <= w ? w : static_cast<int>(std::min<long long>(total + 10, INT_MAX));
        const int rowsHeight =
                static_cast<int>(std::min<long long>(static_cast<long long>(GetRowCount()) * GetRowHeight(), INT_MAX));
        m_hVisible = m_totalWidth > w;
        m_vVisible = rowsHeight > h;
        if (m_hVisible && rowsHeight > h - 10) m_vVisible = true;
        if (m_vVisible && m_totalWidth > w - 10) m_hVisible = true;
        m_viewport = {2, 2 + GetHeaderHeight(), std::max(0, w - (m_vVisible ? 10 : 0)),
                      std::max(0, h - (m_hVisible ? 10 : 0))};
        m_vScrollBar->SetVisible(m_vVisible);
        m_hScrollBar->SetVisible(m_hVisible);
        m_vScrollBar->SetBounds(m_viewport.GetRight(), m_viewport.Y, 10, m_viewport.Height);
        m_hScrollBar->SetBounds(2, m_viewport.GetBottom(), m_viewport.Width, 10);
        m_vScrollBar->SetRange(0, std::max(0, rowsHeight - m_viewport.Height), std::max(1, m_viewport.Height));
        m_vScrollBar->SetLineStep(1);
        m_hScrollBar->SetRange(0, std::max(0, m_contentWidth - m_viewport.Width), std::max(1, m_viewport.Width));
        m_hScrollBar->SetLineStep(5);
        m_scrollY = std::clamp(m_scrollY, 0, m_vScrollBar->GetMaxPosition());
        m_scrollX = std::clamp(m_scrollX, 0, m_hScrollBar->GetMaxPosition());
        m_vScrollBar->SetPosition(m_scrollY);
        m_hScrollBar->SetPosition(m_scrollX);
    }
    void ListView::ScrollTo(int pixels) {
        m_scrollY = pixels;
        Layout();
        Invalidate();
    }
    void ListView::OnPaint(Graphics& g) {
        Layout();
        const auto saved = g.Save();
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const RectF all(0, 0, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()));
        const RectF headerClip(2, 2, float(std::max(0, GetWidth() - 5)), float(std::max(0, GetHeaderHeight() - 1)));
        {
            const auto backgroundState = g.Save();
            // The white background stays out from under the translucent header.
            if (m_headerVisible) g.SetClip(headerClip, CombineModeExclude);
            Bitmap background(GetWidth(), GetHeight(), PixelFormat32bppPARGB);
            {
                Graphics paint(&background);
                paint.Clear(Color(m_background));
                if (m_border) {
                    DrawBorder(paint, all, m_border);
                    DrawBorder(paint, {1, 1, all.Width - 2, all.Height - 2}, 0x33FFFFFF);
                }
            }
            g.DrawImage(&background, 0, 0);
            g.Restore(backgroundState);
        }
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        StringFormat format;
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        format.SetLineAlignment(StringAlignmentCenter);
        if (m_headerVisible && m_totalWidth > 0 && GetHeaderHeight() > 0) {
            // The header scrolls with the columns; titles are centered on the whole font height.
            Bitmap header(m_contentWidth, GetHeaderHeight(), PixelFormat32bppPARGB);
            {
                Graphics paint(&header);
                paint.Clear(Color(0));
                paint.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                const float h = float(GetHeaderHeight());
                FillGradient(paint, RectF(0, 0, float(m_contentWidth), h), 0xCCFFFFFF, 0x99FFFFFF);
                StringFormat headerFormat(StringFormatFlagsNoWrap);
                headerFormat.SetTrimming(StringTrimmingEllipsisCharacter);
                const float textHeight = float(int(font->GetHeight(&paint)));
                float x = 0;
                Pen line{Color(0x33000000)};
                SolidBrush ink{Color(0xD2000000)};
                for (const auto& column : m_columns) {
                    if (!column.title.empty() && column.width > 0) {
                        RectF text(x + 2, 2 + (h - 6 - textHeight) / 2, float(std::max(0, column.width - 4)),
                                   textHeight);
                        const auto clip = paint.Save();
                        paint.SetClip(text);
                        paint.DrawString(column.title.c_str(), -1, font.get(), text, &headerFormat, &ink);
                        paint.Restore(clip);
                        paint.DrawLine(&line, x + column.width - 1, 2.f, x + column.width - 1, h - 4);
                    }
                    x += column.width;
                }
                paint.DrawLine(&line, 0.f, h - 2, float(m_contentWidth), h - 2);
            }
            const auto clip = g.Save();
            g.SetClip(headerClip, CombineModeIntersect);
            g.DrawImage(&header, 2 - m_scrollX, 2);
            g.Restore(clip);
        }
        const auto rowsClip = g.Save();
        g.SetClip(m_viewport, CombineModeIntersect);
        const int rowHeight = GetRowHeight(), start = m_scrollY / rowHeight;
        for (int row = start; row < GetRowCount(); ++row) {
            const int y = m_viewport.Y + row * rowHeight - m_scrollY;
            if (y >= m_viewport.GetBottom()) break;
            const bool selected = m_rowStates[row] != 0, hover = !selected && row == m_hoveredRow;
            const ARGB fill = selected ? m_selectedColor : hover ? m_hoverColor : 0;
            if (fill) {
                SolidBrush brush{Color(fill)};
                g.FillRectangle(&brush, 2, y, std::max(m_viewport.Width, m_contentWidth), rowHeight);
            }
            ARGB color = selected && m_selectedText ? m_selectedText : hover && m_hoverText ? m_hoverText : m_textColor;
            SolidBrush brush{Color(color)};
            int x = 2 - m_scrollX;
            for (int col = 0; col < static_cast<int>(m_columns.size()); ++col) {
                const auto width = m_columns[col].width;
                RectF text(static_cast<float>(x + 2), static_cast<float>(y), static_cast<float>(std::max(0, width - 4)),
                           static_cast<float>(rowHeight));
                if (m_images && m_rowImages[row][col] >= 0) {
                    auto icon = m_images->GetImage(m_rowImages[row][col]);
                    if (icon) {
                        const float iw = float(icon->GetWidth()), ih = float(icon->GetHeight());
                        RectF box(float(x + 1), float(y + int((rowHeight - ih) / 2)), iw, ih);
                        text.X += iw + 2;
                        text.Width -= iw + 2;
                        g.DrawImage(icon.get(), box, 0, 0, iw, ih, UnitPixel);
                    }
                }
                g.DrawString(m_rows[row][col].c_str(), -1, font.get(), text, &format, &brush);
                x += width;
            }
            if (row == m_selectedRow && IsTabFocused())
                DrawBorder(
                        g,
                        {2.f, static_cast<float>(y), static_cast<float>(m_totalWidth), static_cast<float>(rowHeight)},
                        4);
        }
        g.Restore(rowsClip);
        g.Restore(saved);
    }
    int ListView::RowAt(int x, int y) const {
        if (!m_viewport.Contains(x, y)) return -1;
        const int row = (y - m_viewport.Y + m_scrollY) / GetRowHeight();
        return row < GetRowCount() ? row : -1;
    }
    int ListView::ColumnAt(int x, int y) const {
        if (!m_headerVisible || y < 2 || y >= 2 + GetHeaderHeight() || x < 2 || x >= GetWidth() - 2) return -1;
        int at = 2 - m_scrollX;
        for (int i = 0; i < static_cast<int>(m_columns.size()); ++i) {
            at += m_columns[i].width;
            if (x < at) return i;
        }
        return -1;
    }
    void ListView::OnMouseLeave() {
        SetTooltipText(L"");
        m_hoveredRow = m_hoveredColumn = -1;
        Invalidate();
    }
    void ListView::OnMouseCancel() {
        m_pressedColumn = -1;
        Widget::OnMouseCancel();
        Invalidate();
    }
    bool ListView::OnMouseMove(int x, int y, UINT) {
        Layout();
        {
            m_hoveredRow = RowAt(x, y);
            m_hoveredColumn = ColumnAt(x, y);
            // The whole cell text as a tooltip, even when it fits.
            int column = -1, edge = 2 - m_scrollX;
            for (int i = 0; i < static_cast<int>(m_columns.size()); ++i) {
                const int right = edge + m_columns[i].width;
                if (x >= edge && x < right) {
                    column = i;
                    break;
                }
                edge = right;
            }
            SetTooltipText(GetCellText(m_hoveredRow, column));
        }
        Invalidate();
        return true;
    }
    std::vector<int> ListView::GetSelectedRows() const {
        std::vector<int> rows;
        for (int row = 0; row < GetRowCount(); ++row)
            if (m_rowStates[row]) rows.push_back(row);
        return rows;
    }
    bool ListView::SelectRows(int row, bool control, bool shift, bool keepSelected) {
        if (row < 0 || row >= GetRowCount()) return false;
        const auto before = m_rowStates;
        const int beforeRow = m_selectedRow;
        if (m_singleSelection || (!control && !shift)) {
            if (!(keepSelected && m_rowStates[row])) {
                std::fill(m_rowStates.begin(), m_rowStates.end(), uint8_t(0));
                m_rowStates[row] = 1;
            }
            m_anchorRow = row;
        } else if (control) {
            m_rowStates[row] = !m_rowStates[row];
            m_anchorRow = row;
        } else {
            if (m_anchorRow < 0) m_anchorRow = row;
            std::fill(m_rowStates.begin(), m_rowStates.end(), uint8_t(0));
            for (int at = std::min(m_anchorRow, row); at <= std::max(m_anchorRow, row); ++at)
                m_rowStates[at] = 1;
        }
        m_selectedRow = row;
        Invalidate();
        return m_rowStates != before || m_selectedRow != beforeRow;
    }
    bool ListView::OnMouseDown(int x, int y, UINT button) {
        if ((button != VK_LBUTTON && button != VK_RBUTTON) || !IsEffectivelyEnabled()) return false;
        Layout();
        m_pressedColumn = ColumnAt(x, y);
        const int row = RowAt(x, y);
        SelectRows(row, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                   button == VK_RBUTTON);
        Invalidate();
        // Every click on a row is reported, also when it was already selected.
        if (row >= 0 && OnSelectionChanged) OnSelectionChanged(row);
        return true;
    }
    bool ListView::OnMouseUp(int, int, UINT button) {
        if (button != VK_LBUTTON && button != VK_RBUTTON) return false;
        m_pressedColumn = -1;
        Invalidate();
        return true;
    }
    bool ListView::OnMouseWheel(int delta, int, int) {
        SetTooltipText(L"");
        Layout();
        if (!m_vVisible) return false;
        ScrollTo(BoundedAdd(m_scrollY, (delta > 0 ? -3 : 3) * GetRowHeight())); // three rows per notch
        return true;
    }
    bool ListView::OnKeyDown(UINT vk, UINT) {
        const int count = GetRowCount();
        if (!count) return false;
        Layout();
        const int page = std::max(1, m_viewport.Height / GetRowHeight());
        const int current = m_selectedRow;
        int target = current;
        switch (vk) {
            case VK_UP: target = current < 0 ? 0 : current - 1; break;
            case VK_DOWN: target = current + 1; break;
            case VK_HOME: target = 0; break;
            case VK_END: target = count - 1; break;
            case VK_PRIOR: target = current - page; break;
            case VK_NEXT: target = current + page; break;
            default: return false;
        }
        target = std::clamp(target, 0, count - 1);
        const bool changed = SelectRows(target, false, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        EnsureRowVisible(target);
        if (changed && OnSelectionChanged) OnSelectionChanged(target);
        return true;
    }
} // namespace exui

#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ScrollBar.h"
#include <cmath>
#include <utility>

namespace exui {
    using namespace Gdiplus;

    namespace {
        constexpr int RowHeight = 18, Padding = 3;
        Rect FrameSource(bool active) {
            return {active ? 20 : 0, 0, 20, 26};
        }
        Rect ArrowSource(bool dropped) {
            return {dropped ? 15 : 0, 0, 15, 12};
        }
    } // namespace

    // The open list: the combo's frame, 18px rows and a scroll bar when needed.
    class DropdownList : public Widget {
    public:
        explicit DropdownList(ComboBox& combo) : m_combo(combo) {
            SetTabStop(true);
            m_bar = &Add<ScrollBar>();
            m_bar->OnScroll = [this](int pos) {
                m_scroll = pos;
                Invalidate();
            };
        }

        static int HeightFor(size_t count) {
            return count == 0 || count >= 7 ? 130 : int(count) * RowHeight + 2 * Padding;
        }

        // Takes a snapshot of the items and starts at the selected one.
        void Reset(std::vector<std::wstring> items, int selected, int width) {
            m_items = std::move(items);
            m_scroll = 0;
            SetBounds(0, 0, width, HeightFor(m_items.size()));
            Layout();
            MoveHighlight(selected);
        }

        bool AcceptsPointerInput() const noexcept override {
            return true;
        }

        void OnPaint(Graphics& g) override {
            // The active combo frame, stretched around the list.
            if (auto* atlas = SkinBitmap(Skin::ComboButtonNormal)) {
                std::unique_ptr<Bitmap> frame(atlas->Clone(20, 0, 20, 26, PixelFormat32bppPARGB));
                if (frame) DrawNineGrid(g, frame.get(), {0, 0, 20, 26}, {0, 0, GetWidth(), GetHeight()}, 5, 5, 5, 4);
            }
            const int width = RowsWidth();
            g.SetClip(Rect(Padding, Padding, width, GetHeight() - 2 * Padding), CombineModeIntersect);
            const auto font = widget_detail::MakeFont(m_combo, m_combo.GetFontStyle());
            StringFormat format;
            format.SetFormatFlags(StringFormatFlagsNoWrap);
            format.SetLineAlignment(StringAlignmentCenter);
            format.SetTrimming(StringTrimmingEllipsisCharacter);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            for (int i = m_scroll / RowHeight; i < int(m_items.size()); ++i) {
                const int y = Padding + i * RowHeight - m_scroll;
                if (y >= GetHeight() - Padding) break;
                const bool hot = i == m_highlight;
                if (hot) {
                    SolidBrush fill{Color(0xBE0060C0)};
                    g.FillRectangle(&fill, Padding, y, width, RowHeight);
                }
                SolidBrush text{Color(hot ? 0xFFFFFFFF : 0xD2000000)};
                g.DrawString(m_items[i].c_str(), -1, font.get(),
                             RectF(5, float(y), float(std::max(0, width - 4)), float(RowHeight)), &format, &text);
            }
            g.ResetClip();
        }

        // The highlight follows the pointer and stays on the last row it touched.
        bool OnMouseMove(int x, int y, UINT) override {
            if (const int row = RowAt(x, y); row >= 0 && row != m_highlight) {
                m_highlight = row;
                Invalidate();
            }
            return true;
        }
        bool OnMouseDown(int x, int y, UINT button) override {
            return button == VK_LBUTTON && OnMouseMove(x, y, 0);
        }
        bool OnMouseUp(int x, int y, UINT button) override {
            if (button == VK_LBUTTON && RowAt(x, y) >= 0 && RowAt(x, y) == m_highlight) m_combo.Pick(m_highlight);
            return true;
        }
        bool OnMouseWheel(int delta, int, int) override {
            m_bar->ScrollBy(delta > 0 ? -54 : 54);
            return true;
        }

        bool OnKeyDown(UINT vk, UINT) override {
            const int page = std::max(1, (GetHeight() - 2 * Padding) / RowHeight);
            switch (vk) {
                case VK_UP: MoveHighlight(m_highlight < 0 ? 0 : m_highlight - 1); return true;
                case VK_DOWN: MoveHighlight(m_highlight + 1); return true;
                case VK_HOME: MoveHighlight(0); return true;
                case VK_END: MoveHighlight(int(m_items.size()) - 1); return true;
                case VK_PRIOR: MoveHighlight(m_highlight - page); return true;
                case VK_NEXT: MoveHighlight(m_highlight + page); return true;
                case VK_RETURN:
                case VK_SPACE: m_combo.Pick(m_highlight); return true;
                case VK_F4: m_combo.Pick(-1); return true;
            }
            return false;
        }

        ScrollBar& Bar() const noexcept {
            return *m_bar;
        }

    private:
        int RowsWidth() const {
            return std::max(0, GetWidth() - 2 * Padding - (m_bar->IsVisible() ? 10 : 0));
        }

        void Layout() {
            const int visible = GetHeight() - 2 * Padding;
            m_bar->SetBounds(GetWidth() - 13, Padding, 10, std::max(0, visible));
            m_bar->SetRange(0, std::max(0, int(m_items.size()) * RowHeight - visible), visible);
            m_bar->SetVisible(m_bar->GetMaxPosition() > 0);
            m_scroll = std::clamp(m_scroll, 0, m_bar->GetMaxPosition());
            m_bar->SetPosition(m_scroll);
        }

        // Scrolls just enough to show the highlighted row.
        void MoveHighlight(int index) {
            if (m_items.empty() || index < 0) {
                m_highlight = -1;
                return;
            }
            m_highlight = std::clamp(index, 0, int(m_items.size()) - 1);
            const int top = m_highlight * RowHeight, visible = GetHeight() - 2 * Padding;
            if (top < m_scroll)
                m_scroll = top;
            else if (top + RowHeight > m_scroll + visible)
                m_scroll = top + RowHeight - visible;
            Layout();
            Invalidate();
        }

        int RowAt(int x, int y) const {
            if (x < Padding || x >= Padding + RowsWidth() || y < Padding || y >= GetHeight() - Padding) return -1;
            const int row = (y - Padding + m_scroll) / RowHeight;
            return row < int(m_items.size()) ? row : -1;
        }

        ComboBox& m_combo;
        ScrollBar* m_bar;
        std::vector<std::wstring> m_items;
        int m_highlight = -1, m_scroll = 0;
    };

    ComboBox::ComboBox() {
        SetTabStop(true);
        auto list = std::make_shared<DropdownList>(*this);
        m_list = list.get();
        m_popup.Add(std::move(list));
        m_popup.Closed = [this] {
            const int choice = std::exchange(m_pendingSelection, -1);
            m_state = IsFocused() ? WidgetState::Hover : WidgetState::Normal;
            Invalidate();
            if (choice < 0 || choice >= GetItemCount()) return;
            SetSelectedIndex(choice);
            if (const auto callback = OnSelectionChanged) callback(choice);
        };
    }

    ComboBox::ComboBox(int x, int y, int w, int h) : ComboBox() {
        SetBounds(x, y, w, h);
    }

    ComboBox::~ComboBox() {
        m_popup.Closed = {};
        m_popup.Close();
    }

    void ComboBox::ClearItems() {
        m_items.clear();
        m_selectedIndex = -1;
        SetCaption(L"");
    }

    void ComboBox::AddItem(std::wstring text) {
        m_items.push_back(std::move(text));
        Invalidate();
    }

    void ComboBox::SetSelectedIndex(int index) {
        if (index < 0 || index >= GetItemCount()) return;
        m_selectedIndex = index;
        SetCaption(m_items[index]);
    }

    std::wstring ComboBox::GetItemText(int index) const {
        return index >= 0 && index < GetItemCount() ? m_items[index] : L"";
    }

    int ComboBox::GetDropdownHeight() const noexcept {
        return IsDropped() ? m_list->GetHeight() : DropdownList::HeightFor(m_items.size());
    }

    Rect ComboBox::GetArrowBounds() const {
        return {GetWidth() - 20, int(std::floor(GetHeight() / 2.f - 6 + 0.5f)), 15, 12};
    }

    void ComboBox::OnPaint(Graphics& g) {
        // The frame is lit while the list is open or the pointer or focus is on the box.
        const bool active = IsDropped() || m_state != WidgetState::Normal;
        if (auto* frame = SkinBitmap(Skin::ComboButtonNormal); frame && GetWidth() > 0 && GetHeight() > 0) {
            Bitmap nine(GetWidth(), GetHeight(), PixelFormat32bppPARGB);
            {
                Graphics paint(&nine);
                paint.Clear(Color(0));
                DrawNineGrid(paint, frame, FrameSource(active), {0, 0, GetWidth(), GetHeight()}, 5, 5, 5, 3);
            }
            DrawWithOpacity(g, nine, {0, 0, GetWidth(), GetHeight()}, 244);
        }
        if (auto* arrows = SkinBitmap(Skin::ComboButtonHover)) {
            const auto src = ArrowSource(IsDropped());
            g.DrawImage(arrows, GetArrowBounds(), src.X, src.Y, src.Width, src.Height, UnitPixel);
        }
        const auto font = widget_detail::MakeFont(*this, m_fontStyle);
        const float y = (GetHeight() - font->GetHeight(&g) - 3) / 2 + 2;
        const RectF rect(4, y, float(std::max(0, GetWidth() - 24)), float(std::max(0, GetHeight() - 8)));
        StringFormat format;
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        SolidBrush brush{Color(m_textColor)};
        g.DrawString(m_caption.c_str(), -1, font.get(), rect, &format, &brush);
    }

    void ComboBox::PaintDropdown(Graphics& g) {
        if (!IsDropped()) m_list->Reset(m_items, m_selectedIndex, GetWidth());
        m_list->OnPaint(g);
        if (auto& bar = m_list->Bar(); bar.IsVisible()) {
            const auto saved = g.Save();
            g.TranslateTransform(float(bar.GetX()), float(bar.GetY()));
            bar.OnPaint(g);
            g.Restore(saved);
        }
    }

    bool ComboBox::OpenDropdown() {
        auto* window = GetWindow();
        if (IsDropped() || !IsEffectivelyEnabled() || !window) return false;
        m_pendingSelection = -1;
        m_list->Reset(m_items, m_selectedIndex, GetWidth());
        const POINT origin = GetWindowOrigin();
        const int width = window->ToPhysical(float(GetWidth())),
                  height = window->ToPhysical(float(m_list->GetHeight()));
        POINT p{window->ToPhysical(float(origin.x)), window->ToPhysical(float(origin.y + GetHeight()))};
        ClientToScreen(window->GetHwnd(), &p);
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &monitor) &&
            p.y + height > monitor.rcWork.bottom)
            p.y -= window->ToPhysical(float(GetHeight())) + height;
        const std::weak_ptr<char> lifetime = m_lifetime;
        const bool opened = m_popup.Open(window->GetHwnd(), {p.x, p.y, p.x + width, p.y + height}, m_list);
        if (lifetime.expired()) return false;
        Invalidate();
        return opened;
    }

    void ComboBox::CloseDropdown() {
        m_popup.Close();
    }

    // The list closes first; the selection changes once it is gone.
    void ComboBox::Pick(int index) {
        m_pendingSelection = index;
        m_popup.RequestClose();
    }

    void ComboBox::OnFocusChanged(bool focused) {
        m_state = focused ? WidgetState::Hover : WidgetState::Normal;
        Widget::OnFocusChanged(focused);
    }

    // Opens on the press. A press while the list is open only lets the list close.
    bool ComboBox::OnMouseDown(int, int, UINT button) {
        if (button != VK_LBUTTON || !IsEffectivelyEnabled()) return false;
        if (IsDropped()) return true;
        m_state = WidgetState::Pressed;
        OpenDropdown();
        Invalidate();
        return true;
    }

    bool ComboBox::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON) return false;
        m_state = Contains(x, y) ? WidgetState::Hover : WidgetState::Normal;
        Invalidate();
        return true;
    }

    bool ComboBox::OnMouseWheel(int delta, int, int) {
        if (!IsFocused() || IsDropped() || !IsEffectivelyEnabled()) return false;
        StepSelection(m_selectedIndex + (delta > 0 ? -1 : 1));
        return true;
    }

    void ComboBox::StepSelection(int index) {
        if (m_items.empty()) return;
        index = std::clamp(index, 0, GetItemCount() - 1);
        if (index == m_selectedIndex) return;
        SetSelectedIndex(index);
        if (const auto callback = OnSelectionChanged) callback(index);
    }

    bool ComboBox::OnKeyDown(UINT vk, UINT) {
        switch (vk) {
            case VK_SPACE:
            case VK_RETURN:
            case VK_F4: return OnMouseDown(0, 0, VK_LBUTTON);
            case VK_UP: StepSelection(m_selectedIndex - 1); return true;
            case VK_DOWN: StepSelection(m_selectedIndex < 0 ? 0 : m_selectedIndex + 1); return true;
            case VK_HOME: StepSelection(0); return true;
            case VK_END: StepSelection(GetItemCount() - 1); return true;
        }
        return false;
    }

} // namespace exui

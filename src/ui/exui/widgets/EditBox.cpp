#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/Window.h"
#include <commctrl.h>
#include <windowsx.h>
#include <climits>
#include <cmath>

namespace exui {
    using namespace Gdiplus;

    namespace {

        int Round(float value) {
            return static_cast<int>(std::floor(value + 0.5f));
        }

        constexpr int EditFormatFlags = StringFormatFlagsNoFitBlackBox | StringFormatFlagsMeasureTrailingSpaces |
                StringFormatFlagsNoClip | StringFormatFlagsNoWrap;

        void Text(Graphics& g, std::wstring_view value, const Font& font, RectF rect, ARGB color) {
            StringFormat format;
            format.SetFormatFlags(EditFormatFlags);
            SolidBrush brush{Color(color)};
            g.DrawString(value.data(), static_cast<INT>(value.size()), &font, rect, &format, &brush);
        }

        // A 1x1 surface for measuring text outside of painting.
        struct Measure {
            Bitmap bitmap{1, 1, PixelFormat32bppPARGB};
            Graphics g{&bitmap};
            Measure() {
                g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            }
        };

        enum MenuCommand {
            CutCommand = 1,
            CopyCommand = 2,
            PasteCommand = 3,
            SelectAllCommand = 5,
            DeleteCommand = 6,
            UndoCommand = 10
        };

    } // namespace

    EditBox::EditBox() {
        SetTabStop(true);
        m_textColor = 0xE6000000;
        m_hScrollBar = &Add<ScrollBar>(0, 0, 0, 10, false);
        m_vScrollBar = &Add<ScrollBar>();
        m_hScrollBar->SetVisible(false);
        m_vScrollBar->SetVisible(false);
        m_hScrollBar->OnScroll = [this](int position) {
            m_scrollX = float(position);
            PositionIme();
            Invalidate();
        };
        m_vScrollBar->OnScroll = [this](int position) {
            m_scrollY = float(position);
            PositionIme();
            Invalidate();
        };
        m_menu.OnCommand = [this](int command) {
            switch (command) {
                case CutCommand: CutToClipboard(); break;
                case CopyCommand: CopyToClipboard(); break;
                case PasteCommand: PasteFromClipboard(); break;
                case SelectAllCommand: SelectAll(); break;
                case DeleteCommand:
                    if (CanEdit()) Commit(m_buffer.Replace({}));
                    break;
                case UndoCommand: Undo(); break;
            }
        };
    }

    EditBox::EditBox(int x, int y, int w, int h, std::wstring text, bool password) : EditBox() {
        SetBounds(x, y, w, h);
        m_passwordMode = password;
        SetText(std::move(text));
    }

    EditBox::~EditBox() {
        m_menu.Close();
        UpdateIme(false);
    }

    HWND EditBox::Hwnd() const noexcept {
        return GetWindow() ? GetWindow()->GetHwnd() : nullptr;
    }

    void EditBox::OnWindowChanged() {
        UpdateIme(false);
        m_menu.Close();
    }

    // ---------------------------------------------------------------------------
    // Content

    void EditBox::SetText(std::wstring text, bool caretToEnd) {
        const auto previous = m_caption;
        m_buffer.SetText(std::move(text), caretToEnd);
        m_caption = m_buffer.Text();
        m_scrollX = m_scrollY = 0;
        EnsureCaretVisible();
        Invalidate();
        if (m_caption != previous && OnTextChanged) OnTextChanged(m_caption);
    }

    void EditBox::SetMultiline(bool value) {
        OnMouseCancel();
        const auto previous = m_caption;
        m_buffer.SetMultiline(value);
        m_caption = m_buffer.Text();
        m_scrollX = m_scrollY = 0;
        EnsureCaretVisible();
        Invalidate();
        if (m_caption != previous && OnTextChanged) OnTextChanged(m_caption);
    }

    void EditBox::SetInputMode(EditInputMode mode) {
        SetPasswordMode(mode == EditInputMode::Password);
        SetMultiline(mode == EditInputMode::Multiline);
        m_characterMode = mode == EditInputMode::Digits || mode == EditInputMode::Decimal ? mode : EditInputMode::Text;
    }

    void EditBox::SetPasswordMode(bool value) {
        if (m_passwordMode == value) return;
        UpdateIme(false);
        m_passwordMode = value;
        UpdateIme(IsFocused());
        EnsureCaretVisible();
        Invalidate();
    }

    void EditBox::SetBorderStyle(int style) {
        m_borderStyle = std::clamp(style, 0, 3);
        Invalidate();
    }

    void EditBox::SetSelection(size_t anchor, size_t caret) {
        m_buffer.Select(anchor, caret);
        EnsureCaretVisible();
        ResetBlink();
    }

    void EditBox::SelectAll() {
        m_buffer.SelectAll();
        EnsureCaretVisible();
        ResetBlink();
    }

    std::wstring EditBox::GetDisplayText() const {
        if (!m_passwordMode) return m_caption;
        std::wstring text = m_caption;
        for (auto& ch : text)
            if (ch != L'\r' && ch != L'\n') ch = L'*';
        return text;
    }

    bool EditBox::Commit(bool changed) {
        if (changed) m_caption = m_buffer.Text();
        EnsureCaretVisible();
        ResetBlink();
        if (changed && OnTextChanged) OnTextChanged(m_caption);
        return changed;
    }

    bool EditBox::Undo() {
        return CanEdit() && Commit(m_buffer.Undo());
    }
    bool EditBox::Redo() {
        return CanEdit() && Commit(m_buffer.Redo());
    }

    // ---------------------------------------------------------------------------
    // Layout and measurement

    float EditBox::LineHeight(Graphics& g) const {
        // Multiline rows are one glyph tall; a single line adds 3px of breathing room.
        if (IsMultiline()) return GlyphHeight(g);
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        return static_cast<float>(static_cast<int>(font->GetHeight(&g) + 3));
    }

    RectF EditBox::GetTextViewport(Graphics& g) const {
        // Multiline boxes always reserve 10px (+2) for both scrollbars.
        if (IsMultiline())
            return {2, 2, static_cast<float>(std::max(0, GetWidth() - 14)),
                    static_cast<float>(std::max(0, GetHeight() - 14))};
        const float h = LineHeight(g);
        return {2, static_cast<float>(std::floor((GetHeight() - h) / 2) + 1),
                static_cast<float>(std::max(0, GetWidth() - 4)), h};
    }

    float EditBox::TextExtent(Graphics& g, std::wstring_view text) const {
        if (text.empty()) return 0;
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        StringFormat format;
        format.SetFormatFlags(EditFormatFlags);
        RectF bounds;
        g.MeasureString(text.data(), static_cast<INT>(text.size()), font.get(), PointF(0, 0), &format, &bounds);
        return bounds.Width;
    }

    // Measured on a Chinese character: where the font has no CJK glyphs, the
    // fallback font can make such lines taller than the font's own metrics.
    float EditBox::GlyphHeight(Graphics& g) const {
        const auto font = widget_detail::MakeFont(*this, m_fontStyle);
        const StringFormat format(EditFormatFlags);
        RectF measured;
        const auto state = g.Save();
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.MeasureString(L"中", 1, font.get(), PointF(0, 0), &format, &measured);
        g.Restore(state);
        return float(int(measured.Height));
    }

    // Each code unit is measured as it sits in the whole line (GDI+ measures 32
    // ranges per call). Units without a box, like the second half of a surrogate
    // pair, take no space; an unmeasured space gets the width of a space.
    std::vector<float> EditBox::CaretStops(Graphics& g, std::wstring_view line) const {
        std::vector<float> stops(line.size() + 1, 0.f);
        if (line.empty()) return stops;
        const auto font = widget_detail::MakeFont(*this, m_fontStyle);
        const auto state = g.Save();
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        StringFormat format(EditFormatFlags);
        const RectF layout(0, 0, TextExtent(g, line) + 100, GlyphHeight(g) + 4);
        std::vector<bool> measured(line.size());
        for (size_t first = 0; first < line.size(); first += 32) {
            const INT count = INT(std::min<size_t>(32, line.size() - first));
            std::vector<CharacterRange> ranges;
            for (INT i = 0; i < count; ++i)
                ranges.emplace_back(INT(first) + i, 1);
            format.SetMeasurableCharacterRanges(count, ranges.data());
            std::unique_ptr<Region[]> regions(new Region[count]);
            if (g.MeasureCharacterRanges(line.data(), INT(line.size()), font.get(), layout, &format, count,
                                         regions.get()) != Ok)
                continue;
            for (INT i = 0; i < count; ++i) {
                RectF bounds;
                regions[i].GetBounds(&bounds, &g);
                if (bounds.Width <= 0) continue;
                stops[first + i + 1] = bounds.GetRight();
                measured[first + i] = true;
            }
        }
        g.Restore(state);
        const float space = TextExtent(g, L"x x") - TextExtent(g, L"xx");
        for (size_t i = 0; i < line.size(); ++i)
            if (!measured[i])
                stops[i + 1] = stops[i] + (line[i] == L' ' ? space : 0.f);
            else
                stops[i + 1] = std::max(stops[i + 1], stops[i]);
        return stops;
    }

    RectF EditBox::GetCaretRect(Graphics& g) const {
        const auto lines = m_buffer.Lines();
        const auto text = GetDisplayText();
        const auto viewport = GetTextViewport(g);
        const size_t caret = m_buffer.Caret();
        const size_t row = m_buffer.LineIndexOf(caret);
        const auto line = lines[row];
        const auto value = std::wstring_view(text).substr(line.start, line.end - line.start);
        const size_t pos = std::min(caret - line.start, value.size());
        const float advance = CaretStops(g, value)[pos];
        return {viewport.X - m_scrollX + advance, viewport.Y + static_cast<float>(row) * LineHeight(g) - m_scrollY - 2,
                1, GlyphHeight(g) + (IsMultiline() ? 1.f : 0.f)};
    }

    PointF EditBox::GetCaretPoint(Graphics& g) const {
        const auto caret = GetCaretRect(g);
        return {caret.X, caret.Y};
    }

    void EditBox::EnsureCaretVisible() {
        if (!DefaultFont()) return; // GDI+ not started yet
        Measure m;
        const auto r = GetTextViewport(m.g);
        const auto caret = GetCaretRect(m.g);
        const auto display = GetDisplayText();
        const std::wstring_view text(display);
        const auto lines = m_buffer.Lines();
        float contentWidth = 0;
        for (const auto& line : lines)
            contentWidth =
                    std::max(contentWidth, std::trunc(TextExtent(m.g, text.substr(line.start, line.end - line.start))));
        // Horizontal: jump back a fifth of the view when the caret leaves on the left.
        const float caretX = caret.X - r.X + m_scrollX;
        if (caretX < m_scrollX)
            m_scrollX = std::max(0.f, caretX - r.Width / 5);
        else if (caretX > m_scrollX + r.Width - 1)
            m_scrollX = caretX - r.Width + 1;
        m_scrollX = std::trunc(std::clamp(m_scrollX, 0.f, std::max(0.f, contentWidth - r.Width + 1)));
        if (IsMultiline()) {
            const float pitch = LineHeight(m.g);
            const float top = static_cast<float>(m_buffer.LineIndexOf(m_buffer.Caret())) * pitch;
            if (top < m_scrollY)
                m_scrollY = top;
            else if (top + pitch > m_scrollY + r.Height)
                m_scrollY = top + pitch - r.Height;
            m_scrollY = std::trunc(std::clamp(m_scrollY, 0.f, std::max(0.f, lines.size() * pitch - r.Height)));
        }
        LayoutScrollBars(m.g);
        PositionIme();
    }

    void EditBox::LayoutScrollBars() {
        if (!DefaultFont()) return;
        Measure m;
        LayoutScrollBars(m.g);
    }

    void EditBox::LayoutScrollBars(Graphics& g) {
        m_hScrollBar->SetVisible(IsMultiline());
        m_vScrollBar->SetVisible(IsMultiline());
        if (!IsMultiline()) return;
        const auto viewport = GetTextViewport(g);
        m_hScrollBar->SetBounds(2, int(viewport.GetBottom()), int(viewport.Width), 10);
        m_vScrollBar->SetBounds(int(viewport.GetRight()), 2, 10, int(viewport.Height));
        const auto lines = m_buffer.Lines();
        const auto display = GetDisplayText();
        const std::wstring_view text(display);
        double width = 0;
        for (const auto& line : lines)
            width = std::max(width, std::trunc(double(TextExtent(g, text.substr(line.start, line.end - line.start)))));
        const double height = double(lines.size()) * LineHeight(g);
        const auto maximum = [](double extent) { return int(std::clamp(extent, 0.0, double(INT_MAX))); };
        m_hScrollBar->SetRange(0, maximum(width - viewport.Width), int(viewport.Width));
        m_vScrollBar->SetRange(0, maximum(height - viewport.Height), int(viewport.Height));
        m_hScrollBar->SetLineStep(1);
        m_vScrollBar->SetLineStep(int(LineHeight(g)));
        m_scrollX = std::clamp(m_scrollX, 0.f, float(m_hScrollBar->GetMaxPosition()));
        m_scrollY = std::clamp(m_scrollY, 0.f, float(m_vScrollBar->GetMaxPosition()));
        m_hScrollBar->SetPosition(int(m_scrollX));
        m_vScrollBar->SetPosition(int(m_scrollY));
    }

    ScrollBar& EditBox::GetHorizontalScrollBar() {
        LayoutScrollBars();
        return *m_hScrollBar;
    }

    ScrollBar& EditBox::GetVerticalScrollBar() {
        LayoutScrollBars();
        return *m_vScrollBar;
    }

    size_t EditBox::HitCharacter(int x, int y) {
        Measure m;
        const auto r = GetTextViewport(m.g);
        const auto lines = m_buffer.Lines();
        const auto text = GetDisplayText();
        const double contentY = std::max(0.0, double(y) - r.Y + m_scrollY);
        const double pitch = LineHeight(m.g);
        size_t row = std::min(lines.size() - 1, size_t(contentY / pitch));
        if (!IsMultiline()) row = 0;
        const auto line = lines[row];
        const auto value = std::wstring_view(text).substr(line.start, line.end - line.start);
        const auto stops = CaretStops(m.g, value);
        // The nearer edge of the character under the pointer.
        const float at = float(x) - r.X + m_scrollX;
        for (size_t i = 0; i < value.size();) {
            const size_t next = std::min(m_buffer.Next(line.start + i) - line.start, value.size());
            if (at < (stops[i] + stops[next]) / 2) return line.start + i;
            i = next;
        }
        return line.end;
    }

    // ---------------------------------------------------------------------------
    // Painting

    void EditBox::OnPaint(Graphics& g) {
        const auto saved = g.Save();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        LayoutScrollBars(g);
        RectF all(0, 0, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()));
        if (m_borderStyle == 1) {
            GraphicsPath clip;
            RoundedRectPath(clip, all, 4);
            g.SetClip(&clip, CombineModeIntersect);
        }
        SolidBrush background{Color(m_background)};
        g.FillRectangle(&background, all.X, all.Y, std::max(0.f, all.Width - 1), std::max(0.f, all.Height - 1));
        if (m_borderStyle == 1)
            DrawRoundedRect(g, all, 4, m_border);
        else if (m_borderStyle != 0)
            DrawBorder(g, all, m_borderStyle == 2 ? 1 : m_border);
        const auto viewport = GetTextViewport(g);
        g.SetClip(viewport, CombineModeIntersect);
        const auto text = GetDisplayText();
        const auto lines = m_buffer.Lines();
        // Like the Windows edit control, the selection is shown only while focused.
        const auto [first, last] = IsFocused() ? GetSelection() : std::pair<size_t, size_t>{0, 0};
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        const float pitch = LineHeight(g);
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto line = lines[i];
            const float y = viewport.Y + static_cast<float>(i) * pitch - m_scrollY;
            if (y + pitch < viewport.Y || y >= viewport.GetBottom()) continue;
            auto value = std::wstring_view(text).substr(line.start, line.end - line.start);
            if (value.empty()) value = L" ";
            RectF rect(viewport.X - m_scrollX, y, std::max(viewport.Width + m_scrollX, TextExtent(g, value) + 4),
                       pitch);
            Text(g, value, *font, rect, m_textColor);
            const auto a = std::max(first, line.start), b = std::min(last, line.end);
            if (a >= b) continue;
            // Measure the selected range inside the whole shaped line.
            StringFormat selectionFormat;
            selectionFormat.SetFormatFlags(EditFormatFlags);
            CharacterRange range(static_cast<INT>(a - line.start), static_cast<INT>(b - a));
            selectionFormat.SetMeasurableCharacterRanges(1, &range);
            Region measured;
            if (g.MeasureCharacterRanges(value.data(), static_cast<INT>(value.size()), font.get(), rect,
                                         &selectionFormat, 1, &measured) != Ok)
                continue;
            Matrix identity;
            INT count = static_cast<INT>(measured.GetRegionScansCount(&identity));
            std::vector<RectF> scans(count);
            if (!count || measured.GetRegionScans(&identity, scans.data(), &count) != Ok) continue;
            Region highlightRegion;
            highlightRegion.MakeEmpty();
            const ARGB highlightColor = ToArgb(0xFF9933);
            SolidBrush highlight(Color{highlightColor});
            for (INT scan = 0; scan < count; ++scan) {
                auto selected = scans[scan];
                selected.Y -= IsMultiline() ? 2.f : 3.f;
                selected.Height += viewport.Height > pitch ? 3.f : 4.f;
                g.FillRectangle(&highlight, selected.X, selected.Y, std::max(0.f, selected.Width - 1),
                                std::max(0.f, selected.Height - 1));
                DrawBorder(g, selected, highlightColor);
                highlightRegion.Union(selected);
            }
            const auto selectionClip = g.Save();
            g.SetClip(&highlightRegion, CombineModeIntersect);
            Text(g, value, *font, rect, ToArgb(0xFFFFFF));
            g.Restore(selectionClip);
        }
        g.Restore(saved); // the caret may reach 1px above the text viewport
        const auto caret = GetCaretRect(g);
        // The caret is drawn whole or not at all: never a stray partial line.
        if (IsFocused() && m_caretVisible && viewport.Contains(float(int(caret.X)), float(int(caret.Y + 2))) &&
            double(caret.Y) + caret.Height - viewport.GetBottom() <= 1e-7) {
            const auto state = g.Save();
            g.SetClip(Rect(1, 1, std::max(0, GetWidth() - (IsMultiline() ? 13 : 2)),
                           std::max(0, GetHeight() - (IsMultiline() ? 13 : 2))),
                      CombineModeIntersect);
            g.SetSmoothingMode(SmoothingModeNone);
            const int x = static_cast<int>(caret.X);
            const int y1 = static_cast<int>(caret.Y + 1), y2 = static_cast<int>(caret.Y + caret.Height - 1);
            Pen pen(Color(0xE6000000), 1);
            g.DrawLine(&pen, x, y1, x, y2);
            g.Restore(state);
        }
        if (IsTabFocused()) DrawBorder(g, viewport, 4);
    }

    void EditBox::ResetBlink() {
        m_caretVisible = true;
        Invalidate();
    }

    // ---------------------------------------------------------------------------
    // Mouse

    void EditBox::OnMouseCancel() {
        m_dragging = false;
        Widget::OnMouseCancel();
    }

    bool EditBox::OnMouseMove(int x, int y, UINT) {
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        if (!m_dragging) return true;
        // Dragging after a double or triple click extends by whole words or lines.
        const size_t hit = HitCharacter(x, y);
        if (m_clickCount >= 2) {
            std::pair<size_t, size_t> unit;
            if (m_clickCount == 3) {
                const auto line = m_buffer.LineOf(hit);
                unit = {line.start, line.end};
            } else {
                unit = m_passwordMode ? std::pair<size_t, size_t>{0, m_caption.size()} : m_buffer.WordAt(hit);
            }
            if (hit < m_dragOrigin)
                m_buffer.Select(m_dragOriginEnd, unit.first);
            else
                m_buffer.Select(m_dragOrigin, std::max(unit.second, m_dragOriginEnd));
        } else {
            m_buffer.MoveCaret(hit, true);
        }
        EnsureCaretVisible();
        Invalidate();
        return true;
    }

    bool EditBox::OnMouseDown(int x, int y, UINT button) {
        if (!IsEffectivelyEnabled()) return false;
        if (button == VK_RBUTTON) return true;
        if (button != VK_LBUTTON) return false;
        Focus();
        const DWORD now = GetTickCount();
        const size_t hit = HitCharacter(x, y);
        const bool triple = m_clickCount == 2 && now - m_lastClickTime <= GetDoubleClickTime() &&
                std::abs(x - m_lastClickPoint.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
                std::abs(y - m_lastClickPoint.y) <= GetSystemMetrics(SM_CYDOUBLECLK);
        if (triple) {
            const auto line = m_buffer.LineOf(hit);
            m_buffer.Select(line.start, line.end);
            m_dragOrigin = line.start;
            m_dragOriginEnd = line.end;
            m_clickCount = 3;
        } else {
            m_clickCount = 1;
            m_buffer.MoveCaret(hit, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        }
        m_lastClickTime = now;
        m_lastClickPoint = {x, y};
        m_dragging = true;
        EnsureCaretVisible();
        ResetBlink();
        return true;
    }

    bool EditBox::OnMouseDoubleClick(int x, int y, UINT button) {
        if (button != VK_LBUTTON || !IsEffectivelyEnabled()) return false;
        Focus();
        // A password reveals no word boundaries: a double click selects everything.
        const auto [first, last] =
                m_passwordMode ? std::pair<size_t, size_t>{0, m_caption.size()} : m_buffer.WordAt(HitCharacter(x, y));
        m_buffer.Select(first, last);
        m_dragOrigin = first;
        m_dragOriginEnd = last;
        m_clickCount = 2;
        m_lastClickTime = GetTickCount();
        m_lastClickPoint = {x, y};
        m_dragging = true;
        EnsureCaretVisible();
        ResetBlink();
        return true;
    }

    bool EditBox::OnMouseUp(int x, int y, UINT button) {
        if (button == VK_LBUTTON) {
            m_dragging = false;
            return true;
        }
        auto* window = GetWindow();
        if (button != VK_RBUTTON || !window) return false;
        const POINT origin = GetWindowOrigin();
        POINT p{window->ToPhysical(float(origin.x + x)), window->ToPhysical(float(origin.y + y))};
        ClientToScreen(window->GetHwnd(), &p);
        return ShowContextMenu(p);
    }

    bool EditBox::OnMouseWheel(int delta, int, int) {
        if (!IsMultiline() || !IsEnabled()) return false;
        LayoutScrollBars();
        if (m_vScrollBar->GetMaxPosition() > 0) {
            Measure m;
            m_vScrollBar->ScrollBy(int(LineHeight(m.g)) * (delta > 0 ? -3 : 3)); // three lines per notch
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Focus and keyboard

    void EditBox::OnTimer() { // caret blink
        m_caretVisible = !m_caretVisible;
        Invalidate();
    }

    void EditBox::UpdateIme(bool focused) {
        const bool off = focused && m_passwordMode && Hwnd();
        if (off == (m_imeWindow != nullptr)) return;
        if (off) {
            m_imeWindow = Hwnd();
            m_savedIme = ImmAssociateContext(m_imeWindow, nullptr);
        } else {
            if (IsWindow(m_imeWindow)) ImmAssociateContext(m_imeWindow, m_savedIme);
            m_imeWindow = nullptr;
            m_savedIme = nullptr;
        }
    }

    void EditBox::OnFocusChanged(bool focused) {
        Widget::OnFocusChanged(focused);
        m_pendingHighSurrogate = 0;
        UpdateIme(focused);
        if (focused) {
            StartTimer(GetCaretBlinkTime());
            ResetBlink();
            PositionIme();
        } else {
            m_dragging = false;
            StopTimer(); // the selection is kept, only hidden
            Invalidate();
        }
    }

    bool EditBox::HandleKey(UINT vk, bool shift, bool control) {
        if (!IsEnabled()) return false;
        if (control) {
            switch (vk) {
                case 'A': SelectAll(); return true;
                case 'C':
                case VK_INSERT: CopyToClipboard(); return true;
                case 'X': CutToClipboard(); return true;
                case 'V': PasteFromClipboard(); return true;
                case 'Z': Undo(); return true;
                case 'Y': Redo(); return true;
            }
        }
        const size_t caret = m_buffer.Caret();
        const auto [first, last] = m_buffer.Selection();
        size_t target = caret;
        switch (vk) {
            case VK_LEFT:
                if (!shift && !control && first != last)
                    target = first; // collapse to the selection start
                else
                    target = control ? m_buffer.PreviousWord(caret) : m_buffer.Previous(caret);
                break;
            case VK_RIGHT:
                if (!shift && !control && first != last)
                    target = last;
                else
                    target = control ? m_buffer.NextWord(caret) : m_buffer.Next(caret);
                break;
            case VK_HOME: target = control ? 0 : m_buffer.LineOf(caret).start; break;
            case VK_END: target = control ? m_caption.size() : m_buffer.LineOf(caret).end; break;
            case VK_UP:
            case VK_DOWN:
            case VK_PRIOR:
            case VK_NEXT: {
                if (!IsMultiline()) return false;
                const auto lines = m_buffer.Lines();
                const size_t row = m_buffer.LineIndexOf(caret);
                size_t step = 1;
                if (vk == VK_PRIOR || vk == VK_NEXT) {
                    Measure m;
                    step = std::max<size_t>(1, size_t(GetTextViewport(m.g).Height / LineHeight(m.g)));
                }
                const bool up = vk == VK_UP || vk == VK_PRIOR;
                const size_t next = up ? (row > step ? row - step : 0) : std::min(row + step, lines.size() - 1);
                target = lines[next].start + std::min(caret - lines[row].start, lines[next].end - lines[next].start);
                break;
            }
            case VK_DELETE:
                if (shift && !control)
                    CutToClipboard();
                else if (CanEdit())
                    Commit(m_buffer.DeleteForward(control));
                return true;
            case VK_BACK:
                if (CanEdit()) Commit(m_buffer.DeleteBackward(control));
                return true;
            case VK_INSERT:
                if (shift) PasteFromClipboard();
                return shift;
            case VK_RETURN:
                if (!IsMultiline()) return false; // left to the window, e.g. a default button
                if (CanEdit()) Commit(m_buffer.Replace(L"\r\n"));
                return true;
            default: return false;
        }
        m_buffer.MoveCaret(target, shift);
        EnsureCaretVisible();
        ResetBlink();
        return true;
    }

    bool EditBox::OnKeyDown(UINT vk, UINT) {
        return HandleKey(vk, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
    }

    bool EditBox::OnChar(wchar_t ch) {
        if (ch < 0x20 || ch == 0x7F) return false; // control characters arrive as keys
        if (!CanEdit()) return true;
        std::wstring value;
        if (ch >= 0xD800 && ch <= 0xDBFF) { // first half of a surrogate pair
            m_pendingHighSurrogate = ch;
            return true;
        }
        if (ch >= 0xDC00 && ch <= 0xDFFF) {
            if (!m_pendingHighSurrogate) return true;
            value = {m_pendingHighSurrogate, ch};
        } else {
            value.assign(1, ch);
        }
        m_pendingHighSurrogate = 0;
        if (m_characterMode == EditInputMode::Digits || m_characterMode == EditInputMode::Decimal) {
            const bool digit = ch >= L'0' && ch <= L'9';
            if (!digit && !(m_characterMode == EditInputMode::Decimal && ch == L'.')) {
                MessageBeep(MB_OK);
                return true;
            }
        }
        Commit(m_buffer.Replace(value));
        return true;
    }

    // ---------------------------------------------------------------------------
    // Clipboard and context menu

    std::optional<std::wstring> EditBox::ReadClipboardText() const {
        if (!OpenClipboard(Hwnd())) return {};
        std::optional<std::wstring> text;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (const auto* data = static_cast<const wchar_t*>(GlobalLock(h))) {
                const size_t limit = GlobalSize(h) / sizeof(wchar_t);
                size_t length = 0;
                while (length < limit && data[length])
                    ++length;
                text = std::wstring(data, length);
                GlobalUnlock(h);
            }
        CloseClipboard();
        return text;
    }

    bool EditBox::WriteClipboardText(std::wstring_view text) const {
        const HWND hwnd = Hwnd();
        if (!IsWindow(hwnd)) return false;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (!h) return false;
        auto* data = static_cast<wchar_t*>(GlobalLock(h));
        if (!data) {
            GlobalFree(h);
            return false;
        }
        std::copy(text.begin(), text.end(), data);
        data[text.size()] = 0;
        GlobalUnlock(h);
        bool ok = false;
        if (OpenClipboard(hwnd)) {
            if (EmptyClipboard()) ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
            CloseClipboard();
        }
        if (!ok) GlobalFree(h);
        return ok;
    }

    void EditBox::CopyToClipboard() const {
        if (!m_passwordMode && m_buffer.HasSelection()) WriteClipboardText(m_buffer.SelectedText());
    }

    void EditBox::CutToClipboard() {
        if (m_passwordMode || !CanEdit() || !m_buffer.HasSelection()) return;
        if (WriteClipboardText(m_buffer.SelectedText())) Commit(m_buffer.Replace({}));
    }

    void EditBox::PasteFromClipboard() {
        if (!CanEdit()) return;
        const auto text = ReadClipboardText();
        if (text && !text->empty()) Commit(m_buffer.Replace(*text));
    }

    bool EditBox::ShowContextMenu(POINT p) {
        if (!IsEffectivelyEnabled() || !Hwnd()) return false;
        const bool selection = m_buffer.HasSelection();
        const auto clip = ReadClipboardText();
        const MenuItem separator{0, L"", true, false, true};
        m_menu.SetItems({{UndoCommand, L"撤销(&U)", CanEdit() && m_buffer.CanUndo()},
                         separator,
                         {CutCommand, L"剪切(&T)", CanEdit() && selection && !m_passwordMode},
                         {CopyCommand, L"复制(&C)", selection && !m_passwordMode},
                         {PasteCommand, L"粘贴(&P)", CanEdit() && clip && !clip->empty()},
                         {DeleteCommand, L"删除(&D)", CanEdit() && selection},
                         separator,
                         {SelectAllCommand, L"全选(&A)", !m_caption.empty()}});
        Focus();
        return m_menu.Show(Hwnd(), p);
    }

    // ---------------------------------------------------------------------------
    // IME

    void EditBox::PositionIme() {
        const HWND hwnd = Hwnd();
        if (!hwnd || !IsFocused() || m_passwordMode) return;
        HIMC context = ImmGetContext(hwnd);
        if (!context) return;
        Measure m;
        const auto p = GetCaretPoint(m.g);
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        // Client coordinates of the window, which are physical pixels.
        const float scale = GetWindow()->GetScale();
        const POINT origin = GetWindowOrigin();
        composition.ptCurrentPos = {Round((origin.x + Round(p.X)) * scale), Round((origin.y + Round(p.Y)) * scale)};
        ImmSetCompositionWindow(context, &composition);
        CANDIDATEFORM candidate{};
        candidate.dwStyle = CFS_CANDIDATEPOS;
        candidate.ptCurrentPos = {composition.ptCurrentPos.x,
                                  Round((origin.y + Round(p.Y) + Round(LineHeight(m.g))) * scale)};
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(hwnd, context);
    }

    bool EditBox::CommitImeResult(std::wstring_view text) {
        return !m_passwordMode && CanEdit() && Commit(m_buffer.Replace(text));
    }

    bool EditBox::OnImeMessage(HWND hwnd, UINT msg, WPARAM, LPARAM lp, LRESULT& result) {
        if (m_passwordMode || !CanEdit()) {
            result = 0;
            return true;
        }
        if (msg == WM_IME_STARTCOMPOSITION) {
            PositionIme();
            return false;
        }
        if (msg != WM_IME_COMPOSITION) return false;
        PositionIme();
        if (!(lp & GCS_RESULTSTR)) return false;
        if (HIMC context = ImmGetContext(hwnd)) {
            const LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
            if (bytes > 0 && bytes % sizeof(wchar_t) == 0) {
                std::wstring text(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
                if (ImmGetCompositionStringW(context, GCS_RESULTSTR, text.data(), bytes) == bytes)
                    CommitImeResult(text);
            }
            ImmReleaseContext(hwnd, context);
        }
        result = 0;
        return true;
    }

} // namespace exui

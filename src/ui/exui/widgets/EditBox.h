#pragma once
#include "ui/exui/Widget.h"
#include "ui/exui/widgets/Menu.h"
#include "ui/exui/widgets/ScrollBar.h"
#include "ui/exui/widgets/TextBuffer.h"
#include <imm.h>
#include <functional>
#include <optional>
#include <string>

namespace exui {

    enum class EditInputMode : int { Text = 0, Password = 1, Multiline = 2, Digits = 3, Decimal = 4 };

    // Self-drawn edit box: translucent white fill, thin border, 2px text inset.
    // Border style 0 = none, 1 = rounded, 2 = sunken bevel, 3 = flat (default).
    // Editing follows the standard Windows edit control; TextBuffer holds the
    // text, selection and undo history.
    class EditBox : public Widget {
    public:
        EditBox();
        EditBox(int x, int y, int width, int height, std::wstring initialText = L"", bool passwordMode = false);
        ~EditBox() override;

        // Replaces the text; the caret goes to the start, or to the end with caretToEnd.
        void SetText(std::wstring text, bool caretToEnd = false);
        const std::wstring& GetText() const noexcept {
            return m_caption;
        }
        void SetPasswordMode(bool password);
        bool IsPasswordMode() const noexcept {
            return m_passwordMode;
        }
        void SetBorderStyle(int style);
        int GetBorderStyle() const noexcept {
            return m_borderStyle;
        }
        void SetReadOnly(bool value) noexcept {
            m_readOnly = value;
        }
        bool IsReadOnly() const noexcept {
            return m_readOnly;
        }
        void SetMultiline(bool value);
        bool IsMultiline() const noexcept {
            return m_buffer.IsMultiline();
        }
        void SetInputMode(EditInputMode mode);
        EditInputMode GetInputMode() const noexcept {
            return m_passwordMode   ? EditInputMode::Password
                    : IsMultiline() ? EditInputMode::Multiline
                                    : m_characterMode;
        }
        void SetBackgroundColor(Gdiplus::ARGB value) {
            m_background = value;
            Invalidate();
        }
        void SetBorderColor(Gdiplus::ARGB value) {
            m_border = value;
            Invalidate();
        }

        void SetSelection(size_t anchor, size_t caret);
        std::pair<size_t, size_t> GetSelection() const noexcept {
            return m_buffer.Selection();
        }
        size_t GetCaretPosition() const noexcept {
            return m_buffer.Caret();
        }
        void SelectAll();
        std::wstring GetDisplayText() const;
        float GetHorizontalOffset() const noexcept {
            return m_scrollX;
        }
        float GetVerticalOffset() const noexcept {
            return m_scrollY;
        }
        // Shown for multiline boxes only.
        ScrollBar& GetHorizontalScrollBar();
        ScrollBar& GetVerticalScrollBar();
        Gdiplus::RectF GetTextViewport(Gdiplus::Graphics& g) const;
        Gdiplus::PointF GetCaretPoint(Gdiplus::Graphics& g) const;
        Gdiplus::RectF GetCaretRect(Gdiplus::Graphics& g) const;

        void CutToClipboard();
        void CopyToClipboard() const;
        void PasteFromClipboard();
        bool Undo();
        bool Redo();
        bool HandleKey(UINT vk, bool shift, bool control);
        bool CommitImeResult(std::wstring_view text);
        bool ShowContextMenu(POINT screenPoint);
        Menu& GetContextMenu() noexcept {
            return m_menu;
        }

        // Called whenever the text changes, including SetText with different text.
        std::function<void(const std::wstring&)> OnTextChanged;

        void OnPaint(Gdiplus::Graphics& g) override;
        void OnMouseCancel() override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseDoubleClick(int x, int y, UINT button) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        bool OnMouseWheel(int delta, int x, int y) override;
        void OnFocusChanged(bool focused) override;
        bool OnKeyDown(UINT vk, UINT flags) override;
        bool OnChar(wchar_t ch) override;
        bool OnImeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) override;

    protected:
        void OnWindowChanged() override;
        void OnTimer() override;

    private:
        bool CanEdit() const noexcept {
            return !m_readOnly && IsEnabled();
        }
        // Applies a buffer edit: syncs the caption, scrolls to the caret, notifies.
        bool Commit(bool changed);
        float TextExtent(Gdiplus::Graphics&, std::wstring_view) const;
        // The font's line spacing in whole pixels: the caret height and the multiline pitch.
        float GlyphHeight(Gdiplus::Graphics&) const;
        float LineHeight(Gdiplus::Graphics&) const;
        // Caret positions in a line: stops[i] is the x before code unit i, stops.back() the end.
        std::vector<float> CaretStops(Gdiplus::Graphics&, std::wstring_view line) const;
        size_t HitCharacter(int x, int y);
        void EnsureCaretVisible();
        void LayoutScrollBars(Gdiplus::Graphics& g);
        void LayoutScrollBars();
        void ResetBlink();
        // A focused password box turns the IME off and restores it on leaving.
        void UpdateIme(bool focused);
        void PositionIme();
        HWND Hwnd() const noexcept;
        std::optional<std::wstring> ReadClipboardText() const;
        bool WriteClipboardText(std::wstring_view text) const;

        TextBuffer m_buffer;
        bool m_passwordMode = false, m_readOnly = false;
        EditInputMode m_characterMode = EditInputMode::Text;
        int m_borderStyle = 3;
        Gdiplus::ARGB m_background = 0xA0FFFFFF, m_border = 0x50000000;
        float m_scrollX = 0, m_scrollY = 0;
        bool m_caretVisible = true;
        bool m_dragging = false;
        size_t m_dragOrigin = 0, m_dragOriginEnd = 0; // word or line picked by a double/triple click
        int m_clickCount = 0;
        DWORD m_lastClickTime = 0;
        POINT m_lastClickPoint{};
        wchar_t m_pendingHighSurrogate = 0;
        HWND m_imeWindow = nullptr; // where the IME is turned off
        HIMC m_savedIme = nullptr;
        Menu m_menu;
        ScrollBar* m_hScrollBar = nullptr;
        ScrollBar* m_vScrollBar = nullptr;
    };

} // namespace exui

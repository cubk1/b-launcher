#pragma once
#include "ui/exui/Resources.h"
#include <memory>
#include <string>
#include <vector>

namespace exui {
    class Window;
    enum class WidgetState : int { Normal = 0, Hover = 1, Pressed = 2 };
    // How the window reports a completed click: OnButtonClicked, OnHyperlinkClicked or nothing.
    enum class ActivationKind { None, Button, Hyperlink };

    // A node of a window's widget tree. Parents own their children. Bounds are
    // relative to the parent, in logical (96-DPI) pixels; a widget paints and
    // gets mouse coordinates in its own space and is clipped to its ancestors.
    class Widget : public std::enable_shared_from_this<Widget> {
    public:
        Widget();
        virtual ~Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;

        // Tree
        Widget* GetParent() const noexcept {
            return m_parent;
        }
        Window* GetWindow() const noexcept {
            return m_window;
        }
        const std::vector<std::shared_ptr<Widget>>& GetChildren() const noexcept {
            return m_children;
        }
        // Takes child from its old parent; its bounds stay relative to the new one.
        template <class T>
        T& Add(std::shared_ptr<T> child) {
            AddChild(child);
            return *child;
        }
        template <class T, class... Args>
        T& Add(Args&&... args) {
            return Add(std::make_shared<T>(std::forward<Args>(args)...));
        }
        void Remove(Widget* child);
        uint32_t GetId() const noexcept {
            return m_id;
        }
        void SetId(uint32_t id) noexcept {
            m_id = id;
        }
        // This widget or the first descendant with the id.
        Widget* Find(uint32_t id) noexcept;

        // Geometry
        void SetBounds(int x, int y, int width, int height);
        RECT GetBounds() const noexcept {
            return m_bounds;
        }
        int GetX() const noexcept {
            return m_bounds.left;
        }
        int GetY() const noexcept {
            return m_bounds.top;
        }
        int GetWidth() const noexcept {
            return m_bounds.right - m_bounds.left;
        }
        int GetHeight() const noexcept {
            return m_bounds.bottom - m_bounds.top;
        }
        bool Contains(int x, int y) const noexcept {
            return x >= 0 && y >= 0 && x < GetWidth() && y < GetHeight();
        }
        // The widget's top left in window coordinates.
        POINT GetWindowOrigin() const noexcept;
        // The area painted, from the top left; labels may paint past their bounds.
        virtual SIZE GetPaintSize() const {
            return {GetWidth(), GetHeight()};
        }

        // State
        void SetVisible(bool visible);
        bool IsVisible() const noexcept {
            return m_visible;
        }
        bool IsEffectivelyVisible() const noexcept;
        void SetEnabled(bool enabled);
        bool IsEnabled() const noexcept {
            return m_enabled;
        }
        bool IsEffectivelyEnabled() const noexcept;
        // A disabled widget is drawn at half its opacity.
        void SetOpacity(uint8_t opacity);
        uint8_t GetOpacity() const noexcept {
            return m_opacity;
        }
        // Tab stops take focus and the mouse; other widgets let a click drag the window.
        void SetTabStop(bool value) noexcept {
            m_tabStop = value;
        }
        bool IsTabStop() const noexcept {
            return m_tabStop;
        }
        virtual bool AcceptsPointerInput() const noexcept {
            return m_tabStop;
        }
        // A container that draws nothing lets the pointer through to what lies below.
        bool IsTransparentToPointer() const noexcept {
            return !m_paints;
        }
        // Whether the pointer at (x, y), in the widget's coordinates and within its bounds, is on it; elsewhere it
        // reaches what lies below.
        virtual bool HitsPoint(int, int) const {
            return true;
        }
        void SetActivationKind(ActivationKind kind) noexcept {
            m_activation = kind;
        }
        ActivationKind GetActivationKind() const noexcept {
            return m_activation;
        }
        WidgetState GetState() const noexcept {
            return m_state;
        }
        bool IsFocused() const noexcept;
        // Focused by the keyboard, so the focus cue is shown.
        bool IsTabFocused() const noexcept;
        void Focus();

        // Appearance
        void SetCaption(std::wstring caption);
        const std::wstring& GetCaption() const noexcept {
            return m_caption;
        }
        // The uppercase character after the first '&', or 0.
        static wchar_t CaptionMnemonic(std::wstring_view caption);
        void SetTooltipText(std::wstring text);
        const std::wstring& GetTooltipText() const noexcept {
            return m_tooltipText;
        }
        void SetFont(std::wstring family, float sizePx, int style = Gdiplus::FontStyleRegular);
        const std::wstring& GetFontFamily() const noexcept {
            return m_fontFamily;
        }
        float GetFontSize() const noexcept {
            return m_fontSizePx;
        }
        int GetFontStyle() const noexcept {
            return m_fontStyle;
        }
        void SetTextColor(Gdiplus::ARGB color);
        Gdiplus::ARGB GetTextColor() const noexcept {
            return m_textColor;
        }
        // A glow color of 0 means no glow, unless the automatic glow is on.
        void SetGlowColor(Gdiplus::ARGB color);
        Gdiplus::ARGB GetGlowColor() const noexcept {
            return m_glowColor;
        }
        void SetAutoGlowColor(bool enabled = true);
        bool HasGlow() const noexcept {
            return m_autoGlowColor || m_glowColor != 0;
        }
        Gdiplus::ARGB ResolveGlowColor(Gdiplus::ARGB text) const noexcept {
            return m_autoGlowColor ? GlowColorFor(text) : m_glowColor;
        }
        // How much the glow is shrunk before it is enlarged again; 1 keeps it sharp.
        void SetGlowScale(float scale);
        float GetGlowScale() const noexcept {
            return m_glowScale;
        }

        // Painting, in the widget's own coordinates.
        void Invalidate() const;
        // Draws the widget and its children as if its parent sat at the target's
        // origin: previews of widgets outside a window.
        void CompositeTo(HDC target, float scale = 1);
        virtual void OnPaint(Gdiplus::Graphics&) {}
        bool IsTransitionActive() const noexcept {
            return m_fadeFrom != nullptr;
        }
        void FinishTransition();

        // Events. Mouse coordinates are local; button is VK_LBUTTON, VK_RBUTTON or VK_MBUTTON.
        virtual void OnMouseEnter();
        virtual void OnMouseLeave();
        // A press ended without a release: capture lost, widget hidden or disabled.
        virtual void OnMouseCancel();
        virtual bool OnMouseMove(int x, int y, UINT flags);
        virtual bool OnMouseDown(int x, int y, UINT button);
        virtual bool OnMouseDoubleClick(int x, int y, UINT button) {
            return OnMouseDown(x, y, button);
        }
        virtual bool OnMouseUp(int x, int y, UINT button);
        virtual bool OnMouseWheel(int delta, int x, int y);
        virtual void OnFocusChanged(bool focused);
        // Keys go to the focused widget first, then to its parents.
        virtual bool OnKeyDown(UINT vk, UINT flags);
        virtual bool OnKeyUp(UINT vk, UINT flags);
        virtual bool OnChar(wchar_t ch);
        virtual bool OnImeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);
        // The widget's action (click, toggle, follow a link): after a click, Enter or
        // Space on the focused widget, and Alt+mnemonic.
        virtual void Activate() {}

    protected:
        // Cross-fades from the current look to the next one, starting at initialAlpha.
        void BeginTransition(int initialAlpha);
        // Square windows get square controls unless a control says otherwise.
        bool WindowIsRounded() const noexcept;
        // The widget joined a window, or left it (window is null).
        virtual void OnWindowChanged() {}
        // A repeating timer, delivered to OnTimer while the widget is in a shown window.
        void StartTimer(UINT intervalMs);
        void StopTimer();
        virtual void OnTimer() {}

        WidgetState m_state = WidgetState::Normal;
        std::wstring m_caption, m_fontFamily = L"宋体";
        float m_fontSizePx = 12;
        int m_fontStyle = Gdiplus::FontStyleRegular;
        Gdiplus::ARGB m_textColor = 0xD2000000, m_glowColor = 0;
        float m_glowScale = 3;
        bool m_autoGlowColor = false;
        bool m_paints = true; // false for containers that draw nothing themselves

    private:
        friend class Window;
        void AddChild(std::shared_ptr<Widget> child);
        void Attach(Window* window);
        uint8_t PaintAlpha() const noexcept {
            return m_enabled ? m_opacity : uint8_t(m_opacity / 2);
        }
        bool PrepareCache(float scale);
        void Composite(HDC target, float scale, POINT origin, RECT clip);
        bool AdvanceTransition(uint64_t now);

        Window* m_window = nullptr;
        Widget* m_parent = nullptr;
        std::vector<std::shared_ptr<Widget>> m_children;
        uint32_t m_id = 0;
        RECT m_bounds{};
        bool m_visible = true, m_enabled = true, m_tabStop = false;
        uint8_t m_opacity = 255;
        ActivationKind m_activation = ActivationKind::None;
        std::wstring m_tooltipText;

        mutable bool m_dirty = true;
        Surface m_cache;
        float m_cacheScale = 0;
        std::unique_ptr<Gdiplus::Bitmap> m_fadeFrom;
        int m_fadeAlpha = 255, m_fadeInitial = 255;
        uint64_t m_fadeStart = 0;
    };

    // An invisible widget that only groups and clips its children.
    class Panel : public Widget {
    public:
        Panel() {
            m_paints = false;
        }
        Panel(int x, int y, int width, int height) : Panel() {
            SetBounds(x, y, width, height);
        }
    };

} // namespace exui

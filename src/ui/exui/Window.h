#pragma once
#include "ui/exui/Tooltip.h"
#include "ui/exui/Widget.h"
#include "ui/exui/WindowAnimator.h"
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace exui {
    class CaptionButton;

    enum class SkinBackgroundMode : int { EdgeFade = 0, Stretch = 1, Tile = 2, NineGrid = 3, BaseColorOnly = 100 };

    struct WindowBackground {
        std::vector<uint8_t> skinBytes;
        COLORREF baseColor = 0xFFFFFF;
        int windowAlpha = 255;
        int cornerRadius = 4;
        SkinBackgroundMode mode = SkinBackgroundMode::EdgeFade;
        RECT margins{5, 5, 5, 5};
        // Unset means: blur behind translucent backgrounds automatically.
        std::optional<bool> aeroEnabled;
        std::optional<bool> controlAnimationsEnabled;
    };

    // What the caption shows; the buttons are laid out right to left in this order.
    namespace CaptionButtonMask {
        inline constexpr uint32_t Icon = 1, Title = 2, Close = 4, Maximize = 8, Minimize = 16;
        inline constexpr uint32_t Menu = 32, Skin = 64, Help = 128, Pin = 256;
        inline constexpr uint32_t FromWindowStyle = 1024; // icon, title, close, maximize and minimize
        inline constexpr uint32_t DefaultLauncherMask = Icon | Title | Close;
    } // namespace CaptionButtonMask

    // A borderless layered top-level window. The skin, frame and title form one
    // cached background; the caption buttons and the widget tree are composited
    // over it.
    //   Window.cpp           creation, appearance, scale and message dispatch
    //   WindowPaint.cpp      background, frame and presentation
    //   WindowInput.cpp      pointer, keyboard, focus and Tab
    //   WindowAnimation.cpp  opening/closing effects and widget fades
    class Window {
    public:
        Window();
        virtual ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        static Window* FromHandle(HWND hwnd);
        // corner: 0 square, 1 a bare popup surface without frame, otherwise rounded.
        bool CreateHostWindow(HWND parent, const std::wstring& title, int width, int height,
                              std::span<const uint8_t> skinBytes, COLORREF base = 0xFFFFFF, int corner = 4,
                              uint32_t mask = CaptionButtonMask::DefaultLauncherMask, bool show = true);
        HWND GetHwnd() const noexcept {
            return m_hwnd;
        }
        // Logical size in 96-DPI pixels; the window itself is GetScale() times larger.
        int GetWidth() const noexcept {
            return m_width;
        }
        int GetHeight() const noexcept {
            return m_height;
        }
        // DPI scale (1 = 96 DPI). Follows the monitor unless set explicitly; 0 follows it again.
        float GetScale() const noexcept {
            return m_scale;
        }
        void SetScale(float scale);
        int ToPhysical(float logical) const noexcept {
            return int(std::lround(logical * m_scale));
        }

        // Appearance
        void SetSkinBytes(std::span<const uint8_t> bytes);
        void SetBackgroundMode(SkinBackgroundMode mode, RECT margins = {5, 5, 5, 5});
        SkinBackgroundMode GetBackgroundMode() const noexcept {
            return m_skinMode;
        }
        void SetTitle(std::wstring title);
        const std::wstring& GetTitle() const noexcept {
            return m_title;
        }
        void SetAeroEnabled(bool enabled);
        bool IsAeroEnabled() const noexcept {
            return m_aeroEnabled;
        }
        // The alpha of the skin layer; translucent windows also get a shadow and blur.
        void SetWindowAlpha(int alpha);
        int GetWindowAlpha() const noexcept {
            return m_windowAlpha;
        }
        int GetCornerRadius() const noexcept {
            return m_cornerRadius;
        }
        COLORREF GetBaseColor() const noexcept {
            return m_baseColor;
        }
        uint32_t GetCaptionMask() const noexcept {
            return m_captionMask;
        }
        WindowBackground GetBackground() const;
        void SetBackground(const WindowBackground& background);
        // White or black text, whichever contrasts with the background under rect.
        Gdiplus::ARGB ContrastTextColor(const Gdiplus::RectF& rect, Gdiplus::ARGB color) const;

        // Showing, closing and modal loops
        void ShowWindowWithAnimation(int showCommand = SW_SHOW);
        // Plays the closing effect first when animate; the window is destroyed at its end.
        void CloseWindow(bool animate = true);
        void SetWindowEnabled(bool enabled);
        // Runs until the window is destroyed. A message box drops input aimed at
        // other windows while their owners stay enabled; the popups it owns, such
        // as an open dropdown, still get theirs.
        void RunModalLoop(bool filterExternalInput = false);
        bool WaitForWindowAnimation();

        // Animation
        void SetWindowAnimationTypes(WindowAnimationKind opening, WindowAnimationKind closing,
                                     bool includeOpeningWidgets = true);
        void SetAnimationsEnabled(bool enabled);
        void SetControlAnimationsEnabled(bool enabled);
        bool AreControlAnimationsEnabled() const noexcept {
            return m_controlAnimationsEnabled;
        }
        bool ShouldAnimateControls() const;
        bool IsWindowAnimating() const noexcept {
            return m_animator.IsActive();
        }
        bool IsClosing() const noexcept {
            return m_animator.IsClosing();
        }
        // Replaces the tick count. With a clock no timers run: the caller steps the
        // animations with AdvanceAnimations, which also works for hidden windows.
        void SetAnimationClock(std::function<uint64_t()> clock) {
            m_clock = std::move(clock);
        }
        uint64_t AnimationTime() const;
        void AdvanceAnimations();

        // Rendering
        // Repaints the skin, frame and title (widgets invalidate themselves).
        void InvalidateBackground();
        std::unique_ptr<Gdiplus::Bitmap> RenderToBitmap();
        // What the window shows right now, an effect frame included.
        std::unique_ptr<Gdiplus::Bitmap> RenderPresentationToBitmap();

        // Widgets
        Widget& GetRoot() noexcept {
            return *m_root;
        }
        template <class T>
        T& Add(std::shared_ptr<T> widget) {
            return m_root->Add(std::move(widget));
        }
        template <class T, class... Args>
        T& Add(Args&&... args) {
            return m_root->Add<T>(std::forward<Args>(args)...);
        }
        const std::vector<std::shared_ptr<Widget>>& GetWidgets() const noexcept {
            return m_root->GetChildren();
        }
        Widget* FindWidget(uint32_t id) noexcept {
            return m_root->Find(id);
        }
        // Only an available tab stop (or null) can take the focus.
        void SetFocusedWidget(Widget* widget, bool fromTab = false);
        Widget* GetFocusedWidget() const noexcept {
            return m_focused;
        }
        Widget* GetTabFocusedWidget() const noexcept {
            return m_tabFocused;
        }
        // The topmost enabled widget at a point in window coordinates.
        Widget* HitTestWidget(int x, int y) const;
        const std::vector<CaptionButton*>& GetCaptionButtons() const noexcept {
            return m_captionButtons;
        }
        HWND GetTooltipHwnd() const noexcept {
            return m_tooltip.Hwnd();
        }

        std::function<void(Widget*)> OnButtonClicked;
        std::function<void(Widget*)> OnHyperlinkClicked;
        std::function<void(uint32_t)> OnCaptionButtonClicked;
        // Sees every message first; returning true consumes it.
        std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> OnWindowMessageFilter;
        // Runs once when CloseWindow is about to destroy the window, after any closing effect. The window
        // is still active then: enable or show the window that should take activation here.
        std::function<void()> OnDestroying;

    protected:
        // A frameless, captionless topmost tool window at screenBounds (physical
        // pixels), drawn at scale: the base of popups.
        bool CreateSurfaceWindow(HWND owner, RECT screenBounds, float scale);
        virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
        // Custom background layers, both part of the cached background and under
        // the widgets: Background before the frame lines, Foreground after the title.
        virtual void PaintClientBackground(Gdiplus::Graphics&) {}
        virtual void PaintClientForeground(Gdiplus::Graphics&) {}

    private:
        friend class Widget;
        friend class CaptionButton;
        friend bool Initialize(const wchar_t*);
        static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        static bool RegisterWindowClass();

        // Window.cpp
        void ApplyScale(float scale, const RECT* suggested = nullptr);
        void ApplyAero(bool enabled);
        void ApplyBlurBehind(bool enabled, const RECT& bounds, int diameter);
        void LayoutCaption();
        // Loads the icons at the current scale and hands them to the window.
        void UpdateIcons();
        POINT ToLogical(LPARAM lp) const noexcept;
        int PhysicalWidth() const noexcept {
            return ToPhysical(float(m_width));
        }
        int PhysicalHeight() const noexcept {
            return ToPhysical(float(m_height));
        }

        // WindowPaint.cpp
        void RequestFrame();
        void RenderFrame(bool includeWidgets = true, bool present = true);
        void PaintScene(Gdiplus::Graphics& g, bool includeForeground);
        void PaintSkin(Gdiplus::Graphics& g);
        void PaintTitle(Gdiplus::Graphics& g);
        void Present();
        void PresentSurface(const Surface& surface);

        // WindowInput.cpp
        LRESULT HandleMouseMove(WPARAM wp, LPARAM lp);
        LRESULT HandleButtonDown(UINT msg, LPARAM lp);
        LRESULT HandleButtonUp(UINT msg, LPARAM lp);
        bool HandleWheel(WPARAM wp, LPARAM lp);
        bool HandleKey(UINT msg, WPARAM wp, LPARAM lp);
        void UpdateHover(Widget* widget);
        void CancelCapture();
        void ActivateWidget(Widget* widget);
        void MoveTabFocus(bool backward);
        void RefreshTooltip(Widget* widget);
        void HideTooltip();
        // Drops hover, press and focus from widgets that are hidden or disabled.
        void ReconcileInputState();
        // Forgets a widget that leaves the window.
        void ForgetWidget(Widget* widget);
        void StartWidgetTimer(Widget* widget, UINT intervalMs);
        void StopWidgetTimer(Widget* widget);

        // WindowAnimation.cpp
        void StartAnimationTimer();
        void StopAnimations();
        void StartWindowEffect(bool closing);
        void HideAnimationChildren();
        void RestoreAnimationChildren();

        HWND m_hwnd = nullptr;
        std::wstring m_title;
        int m_width = 0, m_height = 0;
        float m_scale = 1, m_requestedScale = 0; // 0 = follow the monitor DPI

        // Appearance
        COLORREF m_baseColor = 0xFFFFFF;
        int m_cornerRadius = 4, m_windowAlpha = 255;
        uint32_t m_captionMask = CaptionButtonMask::DefaultLauncherMask;
        SkinBackgroundMode m_skinMode = SkinBackgroundMode::EdgeFade;
        RECT m_skinMargins{5, 5, 5, 5};
        std::vector<uint8_t> m_skinBytes;
        std::unique_ptr<Gdiplus::Bitmap> m_skin;
        HICON m_icon = nullptr;
        bool m_aeroEnabled = false;

        // Widgets. The caption buttons are painted under the content but take the mouse first.
        std::shared_ptr<Panel> m_root, m_chrome;
        std::vector<CaptionButton*> m_captionButtons;
        Widget* m_hovered = nullptr;
        Widget* m_pressed = nullptr;
        Widget* m_focused = nullptr;
        Widget* m_tabFocused = nullptr;
        std::vector<Widget*> m_widgetTimers; // timer ids are the widget addresses
        Tooltip m_tooltip;

        // Rendering
        Surface m_canvas, m_scene;
        bool m_sceneDirty = true, m_frameDirty = true, m_rendering = false;
        bool m_sceneHasForeground = false;
        std::vector<uint32_t> m_backgroundPixels; // the skin and frame, sampled for contrast

        // Animation
        WindowAnimator m_animator;
        std::function<uint64_t()> m_clock;
        bool m_animationsEnabled = true, m_controlAnimationsEnabled = true, m_timerRunning = false;
        std::vector<HWND> m_animationChildren;
        bool m_destroying = false;
        std::shared_ptr<char> m_lifetime = std::make_shared<char>();
    };

} // namespace exui

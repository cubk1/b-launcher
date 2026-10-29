#include "ui/exui/Window.h"

#include <utility>

namespace exui {

    namespace {
        template <class F>
        void ForEach(Widget& widget, F&& f) {
            for (const auto& child : widget.GetChildren()) {
                f(*child);
                ForEach(*child, f);
            }
        }
    } // namespace

    uint64_t Window::AnimationTime() const {
        return m_clock ? m_clock() : GetTickCount64();
    }

    bool Window::ShouldAnimateControls() const {
        return m_animationsEnabled && m_controlAnimationsEnabled && !m_destroying && !IsClosing() && m_hwnd &&
                (m_clock || IsWindowVisible(m_hwnd));
    }

    void Window::SetWindowAnimationTypes(WindowAnimationKind opening, WindowAnimationKind closing,
                                         bool includeOpeningWidgets) {
        m_animator.SetKinds(opening, closing, includeOpeningWidgets);
    }

    void Window::SetAnimationsEnabled(bool enabled) {
        m_animationsEnabled = enabled;
        if (enabled) return;
        if (IsClosing()) {
            CloseWindow(false);
            return;
        }
        StopAnimations();
        RequestFrame();
    }

    void Window::SetControlAnimationsEnabled(bool enabled) {
        m_controlAnimationsEnabled = enabled;
        if (enabled) return;
        ForEach(*m_root, [](Widget& w) { w.FinishTransition(); });
        ForEach(*m_chrome, [](Widget& w) { w.FinishTransition(); });
    }

    // One timer drives the window effect and every widget fade while any runs.
    void Window::StartAnimationTimer() {
        if (m_timerRunning || m_clock || !m_hwnd) return;
        m_timerRunning = SetTimer(m_hwnd, reinterpret_cast<UINT_PTR>(this), 10, nullptr) != 0;
    }

    void Window::AdvanceAnimations() {
        const auto now = AnimationTime();
        bool repaint = false, fading = false;
        auto step = [&](Widget& w) {
            repaint = w.AdvanceTransition(now) || repaint;
            fading = fading || w.IsTransitionActive();
        };
        ForEach(*m_chrome, step);
        ForEach(*m_root, step);
        if (m_animator.IsActive()) {
            const bool closing = IsClosing();
            switch (m_animator.Advance(now, PhysicalWidth(), PhysicalHeight())) {
                case WindowAnimator::Step::Finished:
                    if (closing) {
                        CloseWindow(false);
                        return;
                    }
                    RestoreAnimationChildren();
                    ApplyAero(m_aeroEnabled);
                    repaint = true;
                    break;
                case WindowAnimator::Step::Advanced: Present(); break;
                case WindowAnimator::Step::None: break;
            }
            // The next frame waits its delay after this one was shown.
            m_animator.ScheduleAdvanced(AnimationTime());
        }
        if (repaint) RenderFrame();
        if (!fading && !m_animator.IsActive() && m_timerRunning) {
            KillTimer(m_hwnd, reinterpret_cast<UINT_PTR>(this));
            m_timerRunning = false;
        }
    }

    void Window::StopAnimations() {
        if (m_timerRunning && m_hwnd) KillTimer(m_hwnd, reinterpret_cast<UINT_PTR>(this));
        m_timerRunning = false;
        m_animator.Stop();
        ForEach(*m_root, [](Widget& w) { w.FinishTransition(); });
        ForEach(*m_chrome, [](Widget& w) { w.FinishTransition(); });
        if (m_destroying) {
            m_animationChildren.clear();
            return;
        }
        RestoreAnimationChildren();
        ApplyAero(m_aeroEnabled);
    }

    // Shows the window through its opening effect: a cleared surface is presented
    // first, so the finished window never flashes ahead of the effect.
    void Window::ShowWindowWithAnimation(int showCommand) {
        if (!m_hwnd) return;
        const bool animate = m_animationsEnabled && m_animator.OpeningKind() != WindowAnimationKind::None &&
                !IsWindowVisible(m_hwnd) && !m_animator.IsActive();
        if (animate) {
            RenderFrame(m_animator.IncludesOpeningWidgets(), false);
            if (m_animator.PrepareOpening(m_canvas, PhysicalWidth(), PhysicalHeight())) {
                ApplyAero(false);
                PresentSurface(m_animator.Presentation());
            }
        }
        const std::weak_ptr<char> lifetime = m_lifetime;
        ShowWindow(m_hwnd, showCommand);
        if (lifetime.expired() || !m_hwnd) return;
        if (m_animator.IsOpeningPrepared()) StartWindowEffect(false);
        UpdateWindow(m_hwnd);
    }

    void Window::CloseWindow(bool animate) {
        if (!m_hwnd || !IsWindow(m_hwnd)) return;
        HideTooltip();
        if (animate && m_animationsEnabled && (m_clock || IsWindowVisible(m_hwnd)) &&
            m_animator.ClosingKind() != WindowAnimationKind::None) {
            if (!IsClosing()) StartWindowEffect(true);
            return;
        }
        m_destroying = true;
        StopAnimations();
        if (const auto destroying = std::exchange(OnDestroying, nullptr)) destroying();
        if (m_hwnd) DestroyWindow(m_hwnd);
    }

    bool Window::WaitForWindowAnimation() {
        MSG message{};
        while (m_hwnd && m_animator.IsActive() && !m_clock) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                if (!result) PostQuitMessage(int(message.wParam));
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return m_hwnd && !m_animator.IsActive();
    }

    void Window::StartWindowEffect(bool closing) {
        m_animator.Interrupt();
        const bool capture = closing || !m_animator.IsOpeningPrepared();
        if (capture) RenderFrame(closing || m_animator.IncludesOpeningWidgets(), false);
        if (!m_animator.Begin(closing, capture ? &m_canvas : nullptr)) {
            if (closing) CloseWindow(false);
            return;
        }
        // A reveal-closing effect moves its blur region from the first frame;
        // every other effect starts without blur.
        if (!(closing && WindowAnimator::IsReveal(m_animator.ClosingKind()))) ApplyAero(false);
        HideTooltip();
        HideAnimationChildren();
        const int delay = m_animator.CurrentFrame(PhysicalWidth(), PhysicalHeight()).delayMs;
        Present();
        m_animator.SetNextFrameTime(AnimationTime() + delay);
        StartAnimationTimer();
    }

    // Native child windows (a web browser) are hidden while the captured image plays.
    void Window::HideAnimationChildren() {
        if (!m_animationChildren.empty()) return;
        EnumChildWindows(
                m_hwnd,
                [](HWND child, LPARAM data) -> BOOL {
                    auto* self = reinterpret_cast<Window*>(data);
                    if (GetParent(child) == self->m_hwnd && (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
                        self->m_animationChildren.push_back(child);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(this));
        for (HWND child : m_animationChildren)
            ShowWindow(child, SW_HIDE);
    }

    void Window::RestoreAnimationChildren() {
        const auto children = std::move(m_animationChildren);
        m_animationChildren.clear();
        for (HWND child : children)
            if (IsWindow(child) && GetParent(child) == m_hwnd) ShowWindow(child, SW_SHOWNOACTIVATE);
    }

} // namespace exui

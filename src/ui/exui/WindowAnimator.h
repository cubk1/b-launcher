#pragma once
#include "ui/exui/Resources.h"
#include <optional>

namespace exui {

    enum class WindowAnimationKind : int {
        None = 0,
        Fade = 1,
        ExpandFade = 2,
        RevealLeft = 3,
        RevealRight = 4,
        RevealTop = 5,
        RevealBottom = 6,
        ZoomFade = 7
    };

    struct WindowAnimationFrame {
        Gdiplus::Rect destination;
        int alpha = 255, delayMs = 0, revealWidth = -1;
        int revealDirection = 3;
        bool revealClosing = false;
        bool finished = true;
    };

    // The open/close effects of the 2014 windows. The finished window is
    // captured once; each frame blends that capture onto a cleared surface.
    // Timing and message pumping stay with the window.
    class WindowAnimator {
    public:
        static WindowAnimationFrame Frame(WindowAnimationKind kind, bool closing, int index, int width, int height);
        static bool IsReveal(WindowAnimationKind kind) noexcept {
            return int(kind) >= 3 && int(kind) <= 6;
        }

        void SetKinds(WindowAnimationKind opening, WindowAnimationKind closing, bool includeOpeningWidgets) noexcept;
        WindowAnimationKind OpeningKind() const noexcept {
            return m_opening;
        }
        WindowAnimationKind ClosingKind() const noexcept {
            return m_closing;
        }
        bool IncludesOpeningWidgets() const noexcept {
            return m_includeOpeningWidgets;
        }

        bool IsActive() const noexcept {
            return m_phase != Phase::Idle;
        }
        bool IsOpening() const noexcept {
            return m_phase == Phase::Opening;
        }
        bool IsClosing() const noexcept {
            return m_phase == Phase::Closing;
        }
        int FrameIndex() const noexcept {
            return m_frame;
        }
        WindowAnimationFrame CurrentFrame(int width, int height) const;

        // Before an opening window is shown, a cleared surface is presented so the
        // fully drawn window never flashes ahead of the effect.
        bool PrepareOpening(const Surface& captured, int width, int height);
        bool IsOpeningPrepared() const noexcept {
            return m_openingPrepared;
        }
        const Surface& Presentation() const noexcept {
            return m_presentation;
        }

        // Leaves the current effect without discarding the capture.
        void Interrupt() noexcept {
            m_phase = Phase::Idle;
        }
        // captured may be null when the opening capture was already prepared.
        bool Begin(bool closing, const Surface* captured);
        void Stop();

        uint64_t NextFrameTime() const noexcept {
            return m_nextFrame;
        }
        void SetNextFrameTime(uint64_t time) noexcept {
            m_nextFrame = time;
        }
        enum class Step { None, Advanced, Finished };
        // Moves to the next frame once its delay has passed; never catches up.
        Step Advance(uint64_t now, int width, int height);
        // Schedules the frame produced by the last Advance, unless something reset it.
        void ScheduleAdvanced(uint64_t now);

        // Blur region for the next reveal-closing frame, if one is due.
        std::optional<RECT> PendingBlurRegion(int width, int height) const;
        // Composes the current frame. Returns null when there is nothing to compose.
        const Surface* Compose(int width, int height);

    private:
        enum class Phase { Idle, Opening, Closing };
        WindowAnimationKind m_opening = WindowAnimationKind::None, m_closing = WindowAnimationKind::None;
        bool m_includeOpeningWidgets = true;
        Phase m_phase = Phase::Idle;
        int m_frame = 0, m_lastRenderedFrame = -1;
        int m_scheduledFrame = -1, m_scheduledDelay = 0;
        uint64_t m_nextFrame = 0;
        bool m_openingPrepared = false;
        Surface m_source, m_presentation;
    };

} // namespace exui

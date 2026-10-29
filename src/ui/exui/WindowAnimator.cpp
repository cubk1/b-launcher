#include "ui/exui/WindowAnimator.h"

namespace exui {

    WindowAnimationFrame WindowAnimator::Frame(WindowAnimationKind kind, bool closing, int index, int w, int h) {
        WindowAnimationFrame frame{{0, 0, w, h}};
        const int count = closing ? kind == WindowAnimationKind::ExpandFade ? 20 : 15 : 10;
        if (kind == WindowAnimationKind::None || index >= count) {
            frame.alpha = closing ? 0 : 255;
            return frame;
        }
        index = std::max(0, index);
        frame.finished = false;
        frame.alpha = closing ? 255 - 17 * (index + 1) : 25 * index;
        frame.delayMs = closing ? 10 : 20;
        if (kind == WindowAnimationKind::ExpandFade && closing) {
            // Grows for three frames (1.5px each), then shrinks by two pixels per
            // frame. Every cast truncates toward zero, including negatives.
            const int step = index + 1;
            const double change = step <= 3 ? step * 1.5 : 10.0 - (step - 3) * 2;
            const double origin = step <= 3 ? -step * 1.5 / 2 : (step - 3) - 1.0;
            frame.destination = {int(origin), int(origin), int(w + change), int(h + change)};
            frame.alpha = 255 - 12 * step;
            frame.delayMs = std::max(16 - step, 10);
        } else if (kind == WindowAnimationKind::ZoomFade) {
            const int factor = closing ? index : 9 - index;
            const int dx = int(w * 0.1 / count) * factor, dy = int(h * 0.1 / count) * factor;
            frame.destination = {dx, dy, std::max(0, w - 2 * dx), std::max(0, h - 2 * dy)};
            frame.delayMs = closing ? std::max(12 - index, 1) : 10 - index;
        } else if (int(kind) >= 2 && int(kind) <= 6) {
            frame.revealDirection = kind == WindowAnimationKind::ExpandFade ? 6 : int(kind);
            const int length = frame.revealDirection <= 4 ? w : h;
            frame.revealClosing = closing;
            frame.revealWidth = (length / count) * (closing ? index + 1 : index);
            if (closing) frame.delayMs = 8;
        }
        return frame;
    }

    void WindowAnimator::SetKinds(WindowAnimationKind opening, WindowAnimationKind closing,
                                  bool includeOpeningWidgets) noexcept {
        m_opening = opening;
        m_closing = closing;
        m_includeOpeningWidgets = includeOpeningWidgets;
    }

    WindowAnimationFrame WindowAnimator::CurrentFrame(int width, int height) const {
        return Frame(IsClosing() ? m_closing : m_opening, IsClosing(), m_frame, width, height);
    }

    bool WindowAnimator::PrepareOpening(const Surface& captured, int width, int height) {
        if (!m_source.CopyFrom(captured) || !m_presentation.Resize(width, height)) return false;
        GdiFlush();
        std::fill(m_presentation.Pixels().begin(), m_presentation.Pixels().end(), 0U);
        m_openingPrepared = true;
        return true;
    }

    bool WindowAnimator::Begin(bool closing, const Surface* captured) {
        if (captured && !m_source.CopyFrom(*captured)) return false;
        m_openingPrepared = false;
        m_phase = closing ? Phase::Closing : Phase::Opening;
        m_frame = 0;
        m_lastRenderedFrame = -1;
        m_scheduledFrame = -1;
        m_nextFrame = 0; // no frame may advance before the first one is presented
        return true;
    }

    void WindowAnimator::Stop() {
        m_phase = Phase::Idle;
        m_nextFrame = 0;
        m_scheduledFrame = -1;
        m_openingPrepared = false;
        m_source.Reset();
    }

    WindowAnimator::Step WindowAnimator::Advance(uint64_t now, int width, int height) {
        if (!IsActive() || !m_nextFrame || now < m_nextFrame) return Step::None;
        const bool closing = IsClosing();
        const auto frame = Frame(closing ? m_closing : m_opening, closing, ++m_frame, width, height);
        m_nextFrame = 0;
        if (frame.finished) {
            m_phase = Phase::Idle;
            m_source.Reset();
            return Step::Finished;
        }
        m_scheduledFrame = m_frame;
        m_scheduledDelay = frame.delayMs;
        return Step::Advanced;
    }

    void WindowAnimator::ScheduleAdvanced(uint64_t now) {
        if (m_scheduledFrame >= 0 && IsActive() && m_frame == m_scheduledFrame && !m_nextFrame)
            m_nextFrame = now + m_scheduledDelay;
        m_scheduledFrame = -1;
    }

    std::optional<RECT> WindowAnimator::PendingBlurRegion(int width, int height) const {
        if (!IsActive() || !m_source.GetBitmap() || m_lastRenderedFrame == m_frame) return std::nullopt;
        const auto frame = CurrentFrame(width, height);
        if (!frame.revealClosing) return std::nullopt;
        // Only the bottom edge is clamped; a right reveal may invert the rectangle.
        RECT bounds{6, 6, width - 5, height - 5};
        switch (frame.revealDirection) {
            case 3: bounds.left = std::min(frame.revealWidth + 100, width - 5); break;
            case 4: bounds.right = width - frame.revealWidth - 100; break;
            case 5: bounds.top = std::min(frame.revealWidth + 100, height - 5); break;
            default: bounds.bottom = std::max(height - frame.revealWidth - 100, 6); break;
        }
        return bounds;
    }

    const Surface* WindowAnimator::Compose(int width, int height) {
        if (!IsActive() || !m_source.GetBitmap() || !m_presentation.Resize(width, height)) return nullptr;
        GdiFlush();
        std::fill(m_presentation.Pixels().begin(), m_presentation.Pixels().end(), 0U);
        const auto frame = CurrentFrame(width, height);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, BYTE(frame.alpha), AC_SRC_ALPHA};
        if (frame.revealWidth >= 0) {
            const bool horizontal = frame.revealDirection <= 4;
            const int length = horizontal ? width : height;
            const int extent = std::min(frame.revealWidth, length);
            auto strip = [&](int at, int alpha) {
                if (at < 0 || at >= length) return;
                blend.SourceConstantAlpha = BYTE(alpha);
                const int x = horizontal ? at : 0, y = horizontal ? 0 : at;
                const int w = horizontal ? 1 : width, h = horizontal ? height : 1;
                GdiAlphaBlend(m_presentation.GetDC(), x, y, w, h, m_source.GetDC(), x, y, w, h, blend);
            };
            const bool fromNearEdge = frame.revealDirection == 3 || frame.revealDirection == 5;
            if (frame.revealClosing) {
                // Starts at pixel 1; the last 100 pixels fade out linearly.
                const int remaining = length - extent;
                for (int i = 1; i <= remaining; ++i)
                    strip(fromNearEdge ? length - i : i,
                          i >= remaining - 100 ? int((frame.alpha / 100.0) * (remaining - i)) : frame.alpha);
            } else {
                for (int i = 0; i < extent; ++i)
                    strip(fromNearEdge ? i : length - 1 - i, frame.alpha);
                // A 100px soft edge follows the revealed part.
                for (int i = 0; i < 100; ++i)
                    strip(fromNearEdge ? extent + i : length - extent - i - 1, frame.alpha * (100 - i) / 100);
            }
        } else if (frame.destination.Width > 0 && frame.destination.Height > 0) {
            const auto& d = frame.destination;
            GdiAlphaBlend(m_presentation.GetDC(), d.X, d.Y, d.Width, d.Height, m_source.GetDC(), 0, 0, width, height,
                          blend);
        }
        m_lastRenderedFrame = m_frame;
        return &m_presentation;
    }

} // namespace exui

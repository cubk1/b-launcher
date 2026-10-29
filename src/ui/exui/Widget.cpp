#include "ui/exui/Widget.h"
#include "ui/exui/Window.h"
#include <climits>
#include <cmath>
#include <cwctype>

namespace exui {
    using namespace Gdiplus;

    Widget::Widget() = default;

    Widget::~Widget() {
        for (const auto& child : m_children)
            child->m_parent = nullptr;
    }

    // ---------------------------------------------------------------------------
    // Tree

    void Widget::AddChild(std::shared_ptr<Widget> child) {
        if (!child || child.get() == this) return;
        for (auto* p = m_parent; p; p = p->m_parent)
            if (p == child.get()) return; // no cycles
        if (child->m_parent) child->m_parent->Remove(child.get());
        child->m_parent = this;
        m_children.push_back(child);
        child->Attach(m_window);
        child->Invalidate();
    }

    void Widget::Remove(Widget* child) {
        const auto at = std::find_if(m_children.begin(), m_children.end(),
                                     [child](const auto& item) { return item.get() == child; });
        if (at == m_children.end()) return;
        const auto keep = *at; // alive until it has left the window
        if (m_window) m_window->RequestFrame();
        m_children.erase(at);
        keep->m_parent = nullptr;
        keep->Attach(nullptr);
    }

    void Widget::Attach(Window* window) {
        if (m_window != window) {
            if (m_window) m_window->ForgetWidget(this);
            FinishTransition();
            m_window = window;
            m_dirty = true;
            OnWindowChanged();
        }
        for (const auto& child : m_children)
            child->Attach(window);
    }

    Widget* Widget::Find(uint32_t id) noexcept {
        if (m_id == id) return this;
        for (const auto& child : m_children)
            if (auto* found = child->Find(id)) return found;
        return nullptr;
    }

    // ---------------------------------------------------------------------------
    // Geometry and state

    void Widget::SetBounds(int x, int y, int width, int height) {
        const RECT bounds{x, y, x + std::max(0, width), y + std::max(0, height)};
        if (EqualRect(&bounds, &m_bounds)) return;
        FinishTransition();
        m_bounds = bounds;
        Invalidate();
    }

    POINT Widget::GetWindowOrigin() const noexcept {
        POINT origin{0, 0};
        for (auto* p = this; p; p = p->m_parent) {
            origin.x += p->m_bounds.left;
            origin.y += p->m_bounds.top;
        }
        return origin;
    }

    void Widget::SetVisible(bool visible) {
        if (m_visible == visible) return;
        if (!visible) FinishTransition();
        m_visible = visible;
        Invalidate();
        if (m_window) m_window->ReconcileInputState();
    }

    bool Widget::IsEffectivelyVisible() const noexcept {
        for (auto* p = this; p; p = p->m_parent)
            if (!p->m_visible) return false;
        return true;
    }

    void Widget::SetEnabled(bool enabled) {
        if (m_enabled == enabled) return;
        const auto keep = weak_from_this().lock(); // callbacks below may remove the widget
        OnMouseLeave();
        FinishTransition();
        m_enabled = enabled;
        Invalidate();
        if (m_window) m_window->ReconcileInputState();
    }

    bool Widget::IsEffectivelyEnabled() const noexcept {
        for (auto* p = this; p; p = p->m_parent)
            if (!p->m_enabled) return false;
        return true;
    }

    void Widget::SetOpacity(uint8_t opacity) {
        FinishTransition();
        m_opacity = opacity;
        Invalidate();
    }

    bool Widget::WindowIsRounded() const noexcept {
        return !m_window || m_window->GetCornerRadius() != 0;
    }
    bool Widget::IsFocused() const noexcept {
        return m_window && m_window->GetFocusedWidget() == this;
    }
    bool Widget::IsTabFocused() const noexcept {
        return m_window && m_window->GetTabFocusedWidget() == this;
    }
    void Widget::Focus() {
        if (m_window) m_window->SetFocusedWidget(this);
    }

    // ---------------------------------------------------------------------------
    // Appearance

    void Widget::SetCaption(std::wstring caption) {
        FinishTransition();
        m_caption = std::move(caption);
        Invalidate();
    }

    wchar_t Widget::CaptionMnemonic(std::wstring_view caption) {
        const auto amp = caption.find(L'&');
        return amp != std::wstring_view::npos && amp + 1 < caption.size()
                ? static_cast<wchar_t>(std::towupper(caption[amp + 1]))
                : 0;
    }

    void Widget::SetTooltipText(std::wstring text) {
        if (m_tooltipText == text) return;
        m_tooltipText = std::move(text);
        if (m_window) m_window->RefreshTooltip(this);
    }

    void Widget::SetFont(std::wstring family, float size, int style) {
        FinishTransition();
        m_fontFamily = std::move(family);
        m_fontSizePx = size;
        m_fontStyle = style;
        Invalidate();
    }

    void Widget::SetTextColor(ARGB color) {
        m_textColor = color;
        Invalidate();
    }

    void Widget::SetGlowColor(ARGB color) {
        m_glowColor = color;
        m_autoGlowColor = false;
        Invalidate();
    }

    void Widget::SetAutoGlowColor(bool enabled) {
        m_autoGlowColor = enabled;
        Invalidate();
    }

    void Widget::SetGlowScale(float scale) {
        m_glowScale = std::max(1.f, scale);
        Invalidate();
    }

    // ---------------------------------------------------------------------------
    // Painting

    void Widget::Invalidate() const {
        m_dirty = true;
        if (m_window) m_window->RequestFrame();
    }

    // The cache holds physical pixels: the paint size times the window's scale.
    bool Widget::PrepareCache(float scale) {
        if (scale != m_cacheScale) {
            m_cacheScale = scale;
            m_dirty = true;
            m_fadeFrom.reset();
        }
        if (!m_dirty && m_cache.GetBitmap()) return true;
        const auto paint = GetPaintSize();
        if (!m_cache.Resize(int(std::ceil(paint.cx * scale)), int(std::ceil(paint.cy * scale)))) return false;
        m_dirty = false; // OnPaint may invalidate again for the next frame
        Graphics graphics(m_cache.GetBitmap());
        graphics.Clear(Color(0));
        if (scale != 1) {
            graphics.ScaleTransform(scale, scale);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        }
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        OnPaint(graphics);
        graphics.Flush(FlushIntentionSync);
        return true;
    }

    // origin is the parent's top left in window coordinates; clip is the parent's
    // visible area in physical pixels.
    void Widget::Composite(HDC target, float scale, POINT origin, RECT clip) {
        if (!m_visible) return;
        origin = {origin.x + m_bounds.left, origin.y + m_bounds.top};
        auto physical = [scale](LONG value) { return LONG(std::lround(value * scale)); };
        const int x = physical(origin.x), y = physical(origin.y);
        if (m_paints && GetWidth() > 0 && GetHeight() > 0 && PrepareCache(scale)) {
            const int saved = SaveDC(target);
            IntersectClipRect(target, clip.left, clip.top, clip.right, clip.bottom);
            const BYTE alpha = PaintAlpha();
            if (m_fadeFrom) {
                // The old look at target-current, then the new one at current.
                Graphics graphics(target);
                const int current = std::min(int(alpha), m_fadeAlpha);
                DrawWithOpacity(graphics, *m_fadeFrom,
                                Rect(x, y, int(m_fadeFrom->GetWidth()), int(m_fadeFrom->GetHeight())),
                                BYTE(alpha - current));
                DrawWithOpacity(graphics, *m_cache.GetBitmap(), Rect(x, y, m_cache.Width(), m_cache.Height()),
                                BYTE(current));
                graphics.Flush(FlushIntentionSync);
            } else {
                BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
                GdiAlphaBlend(target, x, y, m_cache.Width(), m_cache.Height(), m_cache.GetDC(), 0, 0, m_cache.Width(),
                              m_cache.Height(), blend);
            }
            if (saved) RestoreDC(target, saved);
        }
        if (m_children.empty()) return;
        RECT own{x, y, x + physical(GetWidth()), y + physical(GetHeight())};
        if (!IntersectRect(&own, &own, &clip)) return;
        const auto children = m_children; // painting may not change the tree, but stay safe
        for (const auto& child : children)
            child->Composite(target, scale, origin, own);
    }

    void Widget::CompositeTo(HDC target, float scale) {
        Composite(target, scale, {0, 0}, {LONG_MIN / 2, LONG_MIN / 2, LONG_MAX / 2, LONG_MAX / 2});
    }

    void Widget::BeginTransition(int initialAlpha) {
        if (!m_window || !m_window->ShouldAnimateControls() || !m_enabled || !IsEffectivelyVisible() ||
            !m_cache.GetBitmap() || initialAlpha >= m_opacity) {
            FinishTransition();
            return;
        }
        m_fadeFrom = CopyBitmap(*m_cache.GetBitmap());
        m_fadeInitial = m_fadeAlpha = std::max(0, initialAlpha);
        m_fadeStart = m_window->AnimationTime();
        m_window->StartAnimationTimer();
    }

    // +51 alpha every 60 ms, up to the opacity.
    bool Widget::AdvanceTransition(uint64_t now) {
        if (!m_fadeFrom) return false;
        const int alpha = int(std::min<uint64_t>(m_opacity, m_fadeInitial + 51 * ((now - m_fadeStart) / 60)));
        if (alpha == m_fadeAlpha) return false;
        m_fadeAlpha = alpha;
        if (alpha >= m_opacity) m_fadeFrom.reset();
        return true;
    }

    void Widget::StartTimer(UINT intervalMs) {
        if (m_window) m_window->StartWidgetTimer(this, intervalMs);
    }

    void Widget::StopTimer() {
        if (m_window) m_window->StopWidgetTimer(this);
    }

    void Widget::FinishTransition() {
        if (!m_fadeFrom) return;
        m_fadeFrom.reset();
        if (m_window) m_window->RequestFrame();
    }

    // ---------------------------------------------------------------------------
    // Default event handling

    void Widget::OnMouseEnter() {
        m_state = WidgetState::Hover;
        Invalidate();
    }
    void Widget::OnMouseLeave() {
        m_state = WidgetState::Normal;
        Invalidate();
    }
    void Widget::OnMouseCancel() {
        m_state = WidgetState::Normal;
        Invalidate();
    }
    bool Widget::OnMouseMove(int, int, UINT) {
        return false;
    }
    bool Widget::OnMouseDown(int, int, UINT) {
        return false;
    }
    bool Widget::OnMouseUp(int, int, UINT) {
        return false;
    }
    bool Widget::OnMouseWheel(int, int, int) {
        return false;
    }
    void Widget::OnFocusChanged(bool) {
        Invalidate();
    }
    bool Widget::OnKeyDown(UINT, UINT) {
        return false;
    }
    bool Widget::OnKeyUp(UINT, UINT) {
        return false;
    }
    bool Widget::OnChar(wchar_t) {
        return false;
    }
    bool Widget::OnImeMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&) {
        return false;
    }

} // namespace exui

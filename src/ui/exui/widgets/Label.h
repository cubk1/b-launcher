#pragma once
#include "ui/exui/Widget.h"
#include <functional>

namespace exui {

    // Text helpers shared by the widgets. Measurements are rounded to whole pixels.
    namespace widget_detail {
        std::unique_ptr<Gdiplus::Font> MakeFont(const Widget& widget, int style);
        Gdiplus::SizeF MeasureText(Gdiplus::Graphics& g, std::wstring_view text, const Gdiplus::Font& font,
                                   const Gdiplus::RectF& bounds, const Gdiplus::StringFormat& format);
        void DrawText(Gdiplus::Graphics& g, const Widget& widget, const Gdiplus::RectF& bounds,
                      const Gdiplus::StringFormat& format, int style, Gdiplus::ARGB color);
        Gdiplus::ARGB ContrastTextColor(const Widget& widget, Gdiplus::ARGB color);
        float UnboundedTextWidth(const Widget& widget);
    }

    // Text, optionally on a background and with a border; transparent by default.
    class Label : public Widget {
    public:
        Label() = default;
        Label(int x, int y, int width, int height, std::wstring caption, Gdiplus::ARGB textColor = 0xD2000000);
        void SetAlignment(Gdiplus::StringAlignment horizontal, Gdiplus::StringAlignment vertical);
        void SetMultiline(bool multiline);
        void SetBackgroundColor(Gdiplus::ARGB color);
        void SetBackgroundGradient(Gdiplus::ARGB start, Gdiplus::ARGB end,
                                   Gdiplus::LinearGradientMode mode = Gdiplus::LinearGradientModeHorizontal);
        void SetBorderColor(Gdiplus::ARGB color);
        void SetTrimming(Gdiplus::StringTrimming trimming);
        void SetAutoTextColor(bool automatic) {
            m_autoTextColor = automatic;
            Invalidate();
        }
        bool IsAutoTextColor() const noexcept {
            return m_autoTextColor;
        }
        Gdiplus::RectF GetTextBounds(Gdiplus::Graphics& g) const;
        void OnPaint(Gdiplus::Graphics& g) override;

    protected:
        virtual Gdiplus::ARGB ResolveTextColor() const;
        Gdiplus::StringAlignment m_hAlign = Gdiplus::StringAlignmentNear;
        Gdiplus::StringAlignment m_vAlign = Gdiplus::StringAlignmentCenter;
        bool m_multiline = false, m_autoTextColor = false;
        Gdiplus::ARGB m_backgroundStart = 0, m_backgroundEnd = 0, m_borderColor = 0;
        Gdiplus::LinearGradientMode m_gradientMode = Gdiplus::LinearGradientModeHorizontal;
        Gdiplus::StringTrimming m_trimming = Gdiplus::StringTrimmingNone;
    };

    // A label that opens its target when clicked: underlined and blue under the
    // pointer, dark red once visited.
    class Hyperlink : public Label {
    public:
        Hyperlink();
        Hyperlink(int x, int y, int width, int height, std::wstring caption, std::wstring targetUrl = L"",
                  Gdiplus::ARGB normalColor = 0xD2000000);
        void SetTargetUrl(std::wstring url) {
            m_targetUrl = std::move(url);
        }
        const std::wstring& GetTargetUrl() const noexcept {
            return m_targetUrl;
        }
        void SetColors(Gdiplus::ARGB normal, Gdiplus::ARGB hover, Gdiplus::ARGB visited);
        bool IsVisited() const noexcept {
            return m_visited;
        }
        void SetMailMode(bool mail) noexcept {
            m_mailMode = mail;
        }
        // Opens the target instead of ShellExecuteW; called only for a nonempty target.
        std::function<void(std::wstring_view)> OnOpenTarget;
        std::function<void()> OnClick;
        void OnMouseEnter() override;
        void OnMouseLeave() override;
        void OnMouseCancel() override;
        bool OnMouseMove(int localX, int localY, UINT flags) override;
        bool OnMouseDown(int localX, int localY, UINT button) override;
        bool OnMouseUp(int localX, int localY, UINT button) override;
        void Activate() override;
        // Only the text is the link; the rest of the bounds lets the pointer through.
        bool HitsPoint(int localX, int localY) const override;

    private:
        std::wstring m_targetUrl;
        bool m_visited = false, m_mailMode = false, m_armed = false;
        Gdiplus::ARGB m_normalColor = 0xD2000000, m_hoverColor = 0xD20060C0, m_visitedColor = 0xD2800000;
    };
} // namespace exui

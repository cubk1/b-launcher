#include "ui/exui/widgets/Menu.h"
#include <cmath>
#include <cwctype>
#include <utility>

namespace exui {
    using namespace Gdiplus;

    namespace {
        // White body, three shadow rings and the icon rail.
        void PaintMenuBackground(Graphics& g, int width, int height) {
            g.Clear(Color(0));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            RectF r(6, 6, float(width - 12), float(height - 12));
            FillRoundedRect(g, r, 4, 0xEAFFFFFF);
            DrawRoundedRect(g, r, 4, 0x7D000000);
            constexpr int diameters[]{7, 10, 13};
            constexpr ARGB shadows[]{0x23000000, 0x0F000000, 0x05000000};
            for (int i = 0; i < 3; ++i) {
                r = Adjust(r, -1, -1, 2, 2);
                DrawRoundedRect(g, r, float(diameters[i]), shadows[i]);
            }
            const RectF rail(8, 8, 24, float(height - 16));
            FillRoundedRect(g, rail, 3, ToArgb(0xECE4D1));
            DrawRoundedRect(g, rail, 3, ToArgb(0xECE4D1));
        }

        void DrawItemText(Graphics& g, std::wstring_view value, const Font& font, RectF rect, ARGB color) {
            StringFormat format;
            format.SetFormatFlags(StringFormatFlagsNoFitBlackBox | StringFormatFlagsMeasureTrailingSpaces |
                                  StringFormatFlagsNoClip | StringFormatFlagsNoWrap);
            format.SetHotkeyPrefix(HotkeyPrefixShow);
            SolidBrush brush{Color(color)};
            g.DrawString(value.data(), INT(value.size()), &font, rect, &format, &brush);
        }

        bool Selectable(const MenuItem& item) {
            return item.enabled && !item.separator;
        }
    } // namespace

    // The popup's content. The highlight follows the pointer, unless the keyboard
    // moved it since the pointer last entered an item.
    class MenuList : public Widget {
    public:
        explicit MenuList(Menu& menu) : m_menu(menu) {
            SetTabStop(true);
        }
        void Reset() {
            m_hover = m_pressed = m_keyboard = -1;
        }
        bool AcceptsPointerInput() const noexcept override {
            return true;
        }
        void OnPaint(Graphics& g) override {
            m_menu.Paint(g, GetWidth(), m_keyboard >= 0 ? m_keyboard : m_hover);
        }

        bool OnMouseMove(int x, int y, UINT) override {
            if (m_pressed >= 0) return true;
            Hover(m_menu.HitTest(x, y));
            return true;
        }
        void OnMouseLeave() override {
            if (m_pressed < 0) Hover(-1);
        }
        void OnMouseCancel() override {
            m_pressed = -1;
        }
        bool OnMouseDown(int x, int y, UINT button) override {
            if (button != VK_LBUTTON) return false;
            if (const int hit = m_menu.HitTest(x, y); hit >= 0) {
                m_hover = m_keyboard = m_pressed = hit;
                Invalidate();
            }
            return true;
        }
        bool OnMouseUp(int x, int y, UINT button) override {
            if (button != VK_LBUTTON) return false;
            const int pressed = std::exchange(m_pressed, -1), hit = m_menu.HitTest(x, y);
            if (pressed >= 0 && hit == pressed)
                m_menu.Activate(hit); // may close the menu
            else
                Hover(hit);
            return true;
        }

        bool OnKeyDown(UINT vk, UINT) override {
            const auto key = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(vk)));
            const auto& items = m_menu.GetItems();
            for (int i = 0; i < int(items.size()); ++i)
                if (key && Selectable(items[i]) && Widget::CaptionMnemonic(items[i].text) == key) {
                    m_menu.Activate(i);
                    return true;
                }
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (vk == VK_DOWN || (vk == VK_TAB && !shift))
                MoveFocus(1);
            else if (vk == VK_UP || vk == VK_TAB)
                MoveFocus(-1);
            else if (vk == VK_RETURN || vk == VK_SPACE)
                m_menu.Activate(m_keyboard >= 0 ? m_keyboard : m_hover);
            else
                return false;
            return true;
        }

    private:
        void Hover(int item) {
            if (item >= 0 && item != m_hover) m_keyboard = -1;
            m_hover = item;
            Invalidate();
        }
        // The next selectable item, wrapping around.
        void MoveFocus(int direction) {
            const auto& items = m_menu.GetItems();
            const int count = int(items.size());
            int start = m_keyboard >= 0 ? m_keyboard : m_hover;
            if (start < 0) start = direction > 0 ? count - 1 : 0;
            for (int step = 1; step <= count; ++step) {
                const int at = ((start + direction * step) % count + count) % count;
                if (Selectable(items[at])) {
                    m_keyboard = at;
                    Invalidate();
                    return;
                }
            }
        }

        Menu& m_menu;
        int m_hover = -1, m_pressed = -1, m_keyboard = -1;
    };

    Menu::Menu() {
        auto list = std::make_shared<MenuList>(*this);
        m_list = list.get();
        m_popup.Add(std::move(list));
    }

    Menu::~Menu() {
        Close();
    }

    void Menu::SetItems(std::vector<MenuItem> items) {
        Close();
        m_items = std::move(items);
    }

    // Text width plus 50, measured in 12px 宋体.
    Size Menu::Measure() const {
        HDC dc = GetDC(nullptr);
        HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"宋体");
        const auto previous = SelectObject(dc, font);
        int w = 50, h = 16;
        for (const auto& item : m_items) {
            SIZE s{};
            GetTextExtentPoint32W(dc, item.text.data(), int(item.text.size()), &s);
            w = std::max(w, int(s.cx) + 50);
            h += item.separator ? 3 : 22;
        }
        SelectObject(dc, previous);
        DeleteObject(font);
        ReleaseDC(nullptr, dc);
        return {w, h};
    }

    void Menu::Paint(Graphics& g, int width, int highlighted) const {
        const auto saved = g.Save();
        const auto size = Measure();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        Bitmap background(width, size.Height, PixelFormat32bppPARGB);
        {
            Graphics frame(&background);
            PaintMenuBackground(frame, width, size.Height);
        }
        g.DrawImage(&background, 0, 0);
        Font font(L"宋体", 12, FontStyleRegular, UnitPixel);
        auto* check = SkinBitmap(Skin::MenuCheckmark);
        int y = 8;
        for (int i = 0; i < int(m_items.size()); ++i) {
            const auto& item = m_items[i];
            const int h = item.separator ? 3 : 22;
            const RectF r(8.f, float(y), float(width - 16), float(h));
            if (item.separator) {
                Pen pen(Color(ToArgb(13419447)));
                g.DrawLine(&pen, 33.f, r.Y + 1, r.GetRight() - 3, r.Y + 1);
            } else {
                const bool hot = i == highlighted && item.enabled;
                if (hot) {
                    SolidBrush fill(Color(0xE63086C6));
                    g.FillRectangle(&fill, r.X, r.Y, r.Width - 1, r.Height - 1);
                    DrawBorder(g, r, 0xC33086C6);
                }
                // The whole item is drawn first so that a disabled one fades evenly.
                Bitmap ink(width, h, PixelFormat32bppPARGB);
                {
                    Graphics ig(&ink);
                    ig.Clear(Color(0));
                    ig.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                    DrawItemText(ig, item.text, font,
                                 {r.X + 30, float(int((22 - font.GetHeight(&g)) / 2)), r.Width - 30, r.Height},
                                 hot ? 0xF0FFFFFF : 0xD2000000);
                    if (item.checked && check) ig.DrawImage(check, RectF(r.X + 4, 1, 20, 20), 0, 0, 20, 20, UnitPixel);
                }
                DrawWithOpacity(g, ink, Rect(0, y, width, h), item.enabled ? 255 : 127);
            }
            y += h;
        }
        g.Restore(saved);
    }

    int Menu::HitTest(int x, int y) const {
        if (x < 8 || x >= Measure().Width - 8) return -1;
        int top = 8;
        for (int i = 0; i < int(m_items.size()); ++i) {
            const int h = m_items[i].separator ? 3 : 22;
            if (y >= top && y < top + h) return m_items[i].enabled ? i : -1;
            top += h;
        }
        return -1;
    }

    POINT Menu::PositionAtCursor(POINT cursor, Size size, const RECT& workArea) {
        if (cursor.x + size.Width - 5 > workArea.right) cursor.x -= size.Width;
        if (cursor.y + size.Height > workArea.bottom) cursor.y -= size.Height - 5;
        cursor.x = std::max<LONG>(workArea.left, std::min<LONG>(cursor.x, workArea.right - size.Width));
        cursor.y = std::max<LONG>(workArea.top, std::min<LONG>(cursor.y, workArea.bottom - size.Height));
        return cursor;
    }

    bool Menu::Show(HWND owner, POINT point) {
        Close();
        const auto* host = Window::FromHandle(owner);
        const float scale = host ? host->GetScale() : 1.f;
        const auto logical = Measure();
        const Size size(int(std::lround(logical.Width * scale)), int(std::lround(logical.Height * scale)));
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor))
            point = PositionAtCursor(point, size, monitor.rcWork);
        m_list->Reset();
        m_list->SetBounds(0, 0, logical.Width, logical.Height);
        return m_popup.Open(owner, {point.x, point.y, point.x + size.Width, point.y + size.Height}, m_list);
    }

    void Menu::Close() {
        m_popup.Close();
    }

    void Menu::Activate(int index) {
        if (index < 0 || index >= int(m_items.size()) || !Selectable(m_items[index])) return;
        const int command = m_items[index].command;
        const auto callback = OnCommand;
        Close();
        if (callback) callback(command);
    }

} // namespace exui

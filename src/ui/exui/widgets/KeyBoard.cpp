#include "ui/exui/widgets/KeyBoard.h"
#include "ui/exui/widgets/Label.h"
#include <algorithm>
#include <cmath>

namespace exui {
    namespace {
        constexpr int MAIN = 0, NAV = 1, PAD = 2;
        struct Cap { const char* name; const wchar_t* label; float x, y, w, h; UINT key; };
        struct Item { const char* name; const wchar_t* label; float w; UINT key; };
        const Item SPACER{nullptr, nullptr, 1.0f, 0};
        const Item HALF{nullptr, nullptr, 0.5f, 0};

        void addRow(std::vector<Cap>& caps, float y, std::initializer_list<Item> items, float h = 1.0f) {
            float x = 0.0f;
            for (const Item& item : items) {
                if (item.name != nullptr) {
                    caps.push_back(Cap{item.name, item.label, x, y, item.w, h, item.key});
                }
                x += item.w;
            }
        }

        std::vector<Cap> build(int section) {
            std::vector<Cap> caps;
            if (section == MAIN) {
                addRow(caps, 0.0f,
                       {{"key.keyboard.escape", L"Esc", 1, VK_ESCAPE},
                        SPACER,
                        {"key.keyboard.f1", L"F1", 1, VK_F1},
                        {"key.keyboard.f2", L"F2", 1, VK_F2},
                        {"key.keyboard.f3", L"F3", 1, VK_F3},
                        {"key.keyboard.f4", L"F4", 1, VK_F4},
                        HALF,
                        {"key.keyboard.f5", L"F5", 1, VK_F5},
                        {"key.keyboard.f6", L"F6", 1, VK_F6},
                        {"key.keyboard.f7", L"F7", 1, VK_F7},
                        {"key.keyboard.f8", L"F8", 1, VK_F8},
                        HALF,
                        {"key.keyboard.f9", L"F9", 1, VK_F9},
                        {"key.keyboard.f10", L"F10", 1, VK_F10},
                        {"key.keyboard.f11", L"F11", 1, VK_F11},
                        {"key.keyboard.f12", L"F12", 1, VK_F12}});
                addRow(caps, 1.25f,
                       {{"key.keyboard.grave.accent", L"`", 1, VK_OEM_3},
                        {"key.keyboard.1", L"1", 1, '1'},
                        {"key.keyboard.2", L"2", 1, '2'},
                        {"key.keyboard.3", L"3", 1, '3'},
                        {"key.keyboard.4", L"4", 1, '4'},
                        {"key.keyboard.5", L"5", 1, '5'},
                        {"key.keyboard.6", L"6", 1, '6'},
                        {"key.keyboard.7", L"7", 1, '7'},
                        {"key.keyboard.8", L"8", 1, '8'},
                        {"key.keyboard.9", L"9", 1, '9'},
                        {"key.keyboard.0", L"0", 1, '0'},
                        {"key.keyboard.minus", L"-", 1, VK_OEM_MINUS},
                        {"key.keyboard.equal", L"=", 1, VK_OEM_PLUS},
                        {"key.keyboard.backspace", L"Back", 2, VK_BACK}});
                addRow(caps, 2.25f,
                       {{"key.keyboard.tab", L"Tab", 1.5f, VK_TAB},
                        {"key.keyboard.q", L"Q", 1, 'Q'},
                        {"key.keyboard.w", L"W", 1, 'W'},
                        {"key.keyboard.e", L"E", 1, 'E'},
                        {"key.keyboard.r", L"R", 1, 'R'},
                        {"key.keyboard.t", L"T", 1, 'T'},
                        {"key.keyboard.y", L"Y", 1, 'Y'},
                        {"key.keyboard.u", L"U", 1, 'U'},
                        {"key.keyboard.i", L"I", 1, 'I'},
                        {"key.keyboard.o", L"O", 1, 'O'},
                        {"key.keyboard.p", L"P", 1, 'P'},
                        {"key.keyboard.left.bracket", L"[", 1, VK_OEM_4},
                        {"key.keyboard.right.bracket", L"]", 1, VK_OEM_6},
                        {"key.keyboard.backslash", L"\\", 1.5f, VK_OEM_5}});
                addRow(caps, 3.25f,
                       {{"key.keyboard.caps.lock", L"Caps", 1.75f, VK_CAPITAL},
                        {"key.keyboard.a", L"A", 1, 'A'},
                        {"key.keyboard.s", L"S", 1, 'S'},
                        {"key.keyboard.d", L"D", 1, 'D'},
                        {"key.keyboard.f", L"F", 1, 'F'},
                        {"key.keyboard.g", L"G", 1, 'G'},
                        {"key.keyboard.h", L"H", 1, 'H'},
                        {"key.keyboard.j", L"J", 1, 'J'},
                        {"key.keyboard.k", L"K", 1, 'K'},
                        {"key.keyboard.l", L"L", 1, 'L'},
                        {"key.keyboard.semicolon", L";", 1, VK_OEM_1},
                        {"key.keyboard.apostrophe", L"'", 1, VK_OEM_7},
                        {"key.keyboard.enter", L"Enter", 2.25f, VK_RETURN}});
                addRow(caps, 4.25f,
                       {{"key.keyboard.left.shift", L"Shift", 2.25f, VK_LSHIFT},
                        {"key.keyboard.z", L"Z", 1, 'Z'},
                        {"key.keyboard.x", L"X", 1, 'X'},
                        {"key.keyboard.c", L"C", 1, 'C'},
                        {"key.keyboard.v", L"V", 1, 'V'},
                        {"key.keyboard.b", L"B", 1, 'B'},
                        {"key.keyboard.n", L"N", 1, 'N'},
                        {"key.keyboard.m", L"M", 1, 'M'},
                        {"key.keyboard.comma", L",", 1, VK_OEM_COMMA},
                        {"key.keyboard.period", L".", 1, VK_OEM_PERIOD},
                        {"key.keyboard.slash", L"/", 1, VK_OEM_2},
                        {"key.keyboard.right.shift", L"Shift", 2.75f, VK_RSHIFT}});
                addRow(caps, 5.25f,
                       {{"key.keyboard.left.control", L"Ctrl", 1.25f, VK_LCONTROL},
                        {"key.keyboard.left.win", L"Win", 1.25f, VK_LWIN},
                        {"key.keyboard.left.alt", L"Alt", 1.25f, VK_LMENU},
                        {"key.keyboard.space", L"", 6.25f, VK_SPACE},
                        {"key.keyboard.right.alt", L"Alt", 1.25f, VK_RMENU},
                        {"key.keyboard.right.win", L"Win", 1.25f, VK_RWIN},
                        {"key.keyboard.menu", L"Menu", 1.25f, VK_APPS},
                        {"key.keyboard.right.control", L"Ctrl", 1.25f, VK_RCONTROL}});
            } else if (section == NAV) {
                addRow(caps, 0.0f,
                       {{"key.keyboard.print.screen", L"Prt", 1, VK_SNAPSHOT},
                        {"key.keyboard.scroll.lock", L"Scr", 1, VK_SCROLL},
                        {"key.keyboard.pause", L"Pau", 1, VK_PAUSE}});
                addRow(caps, 1.25f,
                       {{"key.keyboard.insert", L"Ins", 1, VK_INSERT},
                        {"key.keyboard.home", L"Home", 1, VK_HOME},
                        {"key.keyboard.page.up", L"PgUp", 1, VK_PRIOR}});
                addRow(caps, 2.25f,
                       {{"key.keyboard.delete", L"Del", 1, VK_DELETE},
                        {"key.keyboard.end", L"End", 1, VK_END},
                        {"key.keyboard.page.down", L"PgDn", 1, VK_NEXT}});
                addRow(caps, 4.25f, {SPACER, {"key.keyboard.up", L"↑", 1, VK_UP}, SPACER});
                addRow(caps, 5.25f,
                       {{"key.keyboard.left", L"←", 1, VK_LEFT},
                        {"key.keyboard.down", L"↓", 1, VK_DOWN},
                        {"key.keyboard.right", L"→", 1, VK_RIGHT}});
            } else if (section == PAD) {
                addRow(caps, 1.25f,
                       {{"key.keyboard.num.lock", L"Num", 1, VK_NUMLOCK},
                        {"key.keyboard.keypad.divide", L"/", 1, VK_DIVIDE},
                        {"key.keyboard.keypad.multiply", L"*", 1, VK_MULTIPLY},
                        {"key.keyboard.keypad.subtract", L"-", 1, VK_SUBTRACT}});
                addRow(caps, 2.25f,
                       {{"key.keyboard.keypad.7", L"7", 1, VK_NUMPAD7},
                        {"key.keyboard.keypad.8", L"8", 1, VK_NUMPAD8},
                        {"key.keyboard.keypad.9", L"9", 1, VK_NUMPAD9}});
                caps.push_back(Cap{"key.keyboard.keypad.add", L"+", 3.0f, 2.25f, 1.0f, 2.0f, VK_ADD});
                addRow(caps, 3.25f,
                       {{"key.keyboard.keypad.4", L"4", 1, VK_NUMPAD4},
                        {"key.keyboard.keypad.5", L"5", 1, VK_NUMPAD5},
                        {"key.keyboard.keypad.6", L"6", 1, VK_NUMPAD6}});
                addRow(caps, 4.25f,
                       {{"key.keyboard.keypad.1", L"1", 1, VK_NUMPAD1},
                        {"key.keyboard.keypad.2", L"2", 1, VK_NUMPAD2},
                        {"key.keyboard.keypad.3", L"3", 1, VK_NUMPAD3}});
                caps.push_back(
                        Cap{"key.keyboard.keypad.enter", L"Ent", 3.0f, 4.25f, 1.0f, 2.0f, 0});
                addRow(caps, 5.25f,
                       {{"key.keyboard.keypad.0", L"0", 2, VK_NUMPAD0},
                        {"key.keyboard.keypad.decimal", L".", 1, VK_DECIMAL}});
            }
            return caps;
        }

        const std::vector<Cap>& keyboard() {
            static const std::vector<Cap> all = [] {
                std::vector<Cap> out = build(MAIN);
                for (Cap cap : build(NAV)) {
                    cap.x += 15.5f;
                    out.push_back(cap);
                }
                for (Cap cap : build(PAD)) {
                    cap.x += 19.0f;
                    out.push_back(cap);
                }
                return out;
            }();
            return all;
        }

        const std::vector<Cap>& caps(bool mouse) {
            static const std::vector<Cap> buttons = {
                    {"key.mouse.left", L"左键", 1, 0, 2, 2.5f, VK_LBUTTON},
                    {"key.mouse.right", L"右键", 3.5f, 0, 2, 2.5f, VK_RBUTTON},
                    {"key.mouse.middle", L"中键", 2.7f, 0.5f, 1.1f, 1.4f, VK_MBUTTON},
                    {"key.mouse.4", L"侧键 4", 0, 2.8f, 1.6f, 1, VK_XBUTTON1},
                    {"key.mouse.5", L"侧键 5", 0, 4, 1.6f, 1, VK_XBUTTON2},
            };
            return mouse ? buttons : keyboard();
        }

        Gdiplus::RectF bounds(const Widget& board, const Cap& cap, bool mouse) {
            float units = mouse ? 6.5f : 23.0f;
            float unit = std::max(1.0f, std::min((board.GetWidth() - 12) / units, (board.GetHeight() - 8) / 6.25f));
            float x = (board.GetWidth() - units * unit) / 2;
            return {x + cap.x * unit + 1.5f, 4 + cap.y * unit + 1.5f, cap.w * unit - 3, cap.h * unit - 3};
        }
    }

    KeyBoard::KeyBoard() { SetTabStop(true); }

    void KeyBoard::SetMouse(bool value) {
        if (mouse == value) return;
        mouse = value;
        hovered = pressed = -1;
        SetTooltipText(L"");
        Invalidate();
    }

    void KeyBoard::SetKeys(std::map<std::string, int> value, std::string key) {
        if (counts == value && selected == key) return;
        counts = std::move(value);
        selected = std::move(key);
        Invalidate();
    }

    std::string KeyBoard::KeyOf(UINT vk, LPARAM flags) {
        bool extended = (flags & (1 << 24)) != 0;
        if (vk == VK_SHIFT) vk = ((flags >> 16) & 0xff) == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        if (vk == VK_CONTROL) vk = extended ? VK_RCONTROL : VK_LCONTROL;
        if (vk == VK_MENU) vk = extended ? VK_RMENU : VK_LMENU;
        if (vk == VK_RETURN && extended) return "key.keyboard.keypad.enter";
        if (!extended) {
            switch (vk) {
                case VK_INSERT: vk = VK_NUMPAD0; break;
                case VK_END: vk = VK_NUMPAD1; break;
                case VK_DOWN: vk = VK_NUMPAD2; break;
                case VK_NEXT: vk = VK_NUMPAD3; break;
                case VK_LEFT: vk = VK_NUMPAD4; break;
                case VK_CLEAR: vk = VK_NUMPAD5; break;
                case VK_RIGHT: vk = VK_NUMPAD6; break;
                case VK_HOME: vk = VK_NUMPAD7; break;
                case VK_UP: vk = VK_NUMPAD8; break;
                case VK_PRIOR: vk = VK_NUMPAD9; break;
                case VK_DELETE: vk = VK_DECIMAL; break;
            }
        }
        for (const auto& cap : keyboard()) if (cap.key && cap.key == vk) return cap.name;
        if (vk >= VK_F13 && vk <= VK_F24) return "key.keyboard.f" + std::to_string(vk - VK_F1 + 1);
        return {};
    }

    void KeyBoard::OnPaint(Gdiplus::Graphics& g) {
        using namespace Gdiplus;
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        auto font = widget_detail::MakeFont(*this, FontStyleRegular);
        if (!font) return;
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(format.GetFormatFlags() | StringFormatFlagsNoWrap);
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        const auto& keys = caps(mouse);
        if (mouse) {
            auto body = bounds(*this, {"", L"", 1, 0, 4.5f, 6, 0}, true);
            FillRoundedRect(g, body, 25, 0xA0DFEAF0);
            DrawRoundedRect(g, body, 25, 0x70005070);
        }
        for (size_t i = 0; i < keys.size(); ++i) {
            const auto& key = keys[i];
            auto box = bounds(*this, key, mouse);
            auto found = counts.find(key.name);
            int count = found == counts.end() ? 0 : found->second;
            bool chosen = selected == key.name, hover = hovered == (int) i;
            ARGB fill = chosen ? 0xED9CD7F4 : count < 0 ? 0xE8F1BBBB : count > 0 ? 0xC4C3E8F7 : 0xB8F2F5F7;
            FillRoundedRect(g, {box.X, box.Y + 2, box.Width, box.Height}, 3, 0x55003350);
            FillRoundedRect(g, box, 3, fill);
            if (hover || pressed == (int) i) FillRoundedRect(g, box, 3, pressed == (int) i ? 0x500078BA : 0x70FFFFFF);
            DrawRoundedRect(g, box, 3, chosen ? 0xFF0078B0 : hover ? 0xC00078B0 : 0x70003048);
            SolidBrush ink(Color(0xE6203542));
            g.DrawString(key.label, -1, font.get(), box, &format, &ink);
            if (count != 0) {
                SolidBrush dot(Color(count < 0 ? 0xFFB52C28 : 0xFF1680AA));
                g.FillEllipse(&dot, box.GetRight() - 5, box.GetBottom() - 5, 2.5f, 2.5f);
            }
        }
    }

    int KeyBoard::Hit(int x, int y) const {
        const auto& keys = caps(mouse);
        for (int i = (int) keys.size() - 1; i >= 0; --i)
            if (bounds(*this, keys[i], mouse).Contains((float) x, (float) y)) return i;
        return -1;
    }
    bool KeyBoard::OnMouseMove(int x, int y, UINT) {
        int key = Hit(x, y);
        if (hovered != key) {
            hovered = key;
            SetTooltipText(key >= 0 ? caps(mouse)[key].label : L"");
            Invalidate();
        }
        return true;
    }
    bool KeyBoard::OnMouseDown(int x, int y, UINT button) {
        if (button != VK_LBUTTON) return false;
        pressed = Hit(x, y);
        Invalidate();
        return true;
    }
    bool KeyBoard::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON) return false;
        int key = pressed;
        pressed = -1;
        Invalidate();
        if (key >= 0 && key == Hit(x, y) && OnPick) {
            auto pick = OnPick;
            std::string name = caps(mouse)[key].name;
            pick(name);
        }
        return true;
    }
    void KeyBoard::OnMouseLeave() { hovered = -1; Invalidate(); }
    void KeyBoard::OnMouseCancel() { pressed = -1; Invalidate(); }
}

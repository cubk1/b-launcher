#include "UiKeys.h"

#include "imgui.h"

namespace UiKeys {

    bool isMouseKey(int key) {
        return key <= -100;
    }

    bool isBindableMouse(int button) {
        return button != 0;
    }

    int mouseKey(int button) {
        return -100 - button;
    }

    int mouseButtonOf(int key) {
        return -100 - key;
    }

    std::string mouseKeyName(int key) {
        return "M" + std::to_string(mouseButtonOf(key) + 1);
    }

    bool isValidBind(int key) {
        return key > 0 || isMouseKey(key);
    }

    bool isBindKey(int key) {
        return key != ImGuiKey_Escape && key != ImGuiKey_Delete;
    }

    bool modifierKey(int key) {
        return key == ImGuiKey_LeftCtrl || key == ImGuiKey_LeftShift || key == ImGuiKey_LeftAlt ||
                key == ImGuiKey_LeftSuper || key == ImGuiKey_RightCtrl || key == ImGuiKey_RightShift ||
                key == ImGuiKey_RightAlt || key == ImGuiKey_RightSuper;
    }

    int combo(int key, bool ctrl, bool shift, bool alt) {
        return key | (ctrl ? 1 << 16 : 0) | (shift ? 1 << 17 : 0) | (alt ? 1 << 18 : 0);
    }

    std::string comboKeyName(int key) {
        if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) {
            return "F" + std::to_string(key - ImGuiKey_F1 + 1);
        }
        switch (key) {
            case ImGuiKey_Space: return "Space";
            case ImGuiKey_Enter: return "Enter";
            case ImGuiKey_KeypadEnter: return "Num Enter";
            case ImGuiKey_Tab: return "Tab";
            case ImGuiKey_Backspace: return "Backspace";
            case ImGuiKey_Delete: return "Del";
            case ImGuiKey_Insert: return "Insert";
            case ImGuiKey_Home: return "Home";
            case ImGuiKey_End: return "End";
            case ImGuiKey_PageUp: return "PageUp";
            case ImGuiKey_PageDown: return "PageDown";
            case ImGuiKey_UpArrow: return "Up";
            case ImGuiKey_DownArrow: return "Down";
            case ImGuiKey_LeftArrow: return "Left";
            case ImGuiKey_RightArrow: return "Right";
            case ImGuiKey_Escape: return "Esc";
            default: break;
        }
        if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
            return std::string(1, (char) ('A' + (key - ImGuiKey_A)));
        }
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
            return std::string(1, (char) ('0' + (key - ImGuiKey_0)));
        }
        switch (key) {
            case ImGuiKey_Slash: return "/";
            case ImGuiKey_Backslash: return "\\";
            case ImGuiKey_Comma: return ",";
            case ImGuiKey_Period: return ".";
            case ImGuiKey_Semicolon: return ";";
            case ImGuiKey_Apostrophe: return "'";
            case ImGuiKey_Minus: return "-";
            case ImGuiKey_Equal: return "=";
            case ImGuiKey_LeftBracket: return "[";
            case ImGuiKey_RightBracket: return "]";
            case ImGuiKey_GraveAccent: return "`";
            default: break;
        }
        return "Key " + std::to_string(key);
    }

    std::string comboLabel(int combo) {
        if (combo == 0) {
            return "";
        }
        std::string text;
        if ((combo & (1 << 16)) != 0) {
            text += "Ctrl-";
        }
        if ((combo & (1 << 18)) != 0) {
            text += "Alt-";
        }
        if ((combo & (1 << 17)) != 0) {
            text += "Shift-";
        }
        return text + comboKeyName(combo & 0xFFFF);
    }
}

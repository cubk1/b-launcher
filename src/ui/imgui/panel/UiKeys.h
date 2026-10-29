#pragma once

#include <string>

namespace UiKeys {

    bool isMouseKey(int key);
    bool isBindableMouse(int button);
    int mouseKey(int button);
    int mouseButtonOf(int key);
    std::string mouseKeyName(int key);

    bool isValidBind(int key);
    bool isBindKey(int key);

    bool modifierKey(int key);
    int combo(int key, bool ctrl, bool shift, bool alt);
    std::string comboKeyName(int key);
    std::string comboLabel(int combo);
}

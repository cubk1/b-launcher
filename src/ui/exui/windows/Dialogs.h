#pragma once

#include "ui/exui/MsgBox.h"
#include "ui/exui/Window.h"

#include <string>
#include <vector>

namespace ForgottenMelody {

    // The background of the 2014 launcher's message boxes: the dialog skin's base color, square and opaque.
    exui::WindowBackground messageBackground();
    // A message box in that look. Returns the result code of the pressed button: 0 for 确认 or the leftmost
    // custom label, and the code of 取消 when the box is closed.
    int message(HWND parent, const std::wstring& text, const std::wstring& title,
                exui::MsgBoxButtons buttons = exui::MsgBoxButtons::Ok, const std::vector<std::wstring>& labels = {});
}

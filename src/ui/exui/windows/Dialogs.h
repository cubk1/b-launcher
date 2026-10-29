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

    // Creates window in the look of the 2014 dialogs: the dialog skin at 150 alpha, fading into gray at its edges.
    bool createDialog(exui::Window& window, HWND parent, const std::wstring& title, int width, int height,
                      uint32_t captionMask = exui::CaptionButtonMask::Close);
    // Shows window, focused on focus if given, and returns once it is closed. Other windows stay enabled, but their
    // input is dropped meanwhile.
    void runModal(exui::Window& window, exui::Widget* focus = nullptr);
}

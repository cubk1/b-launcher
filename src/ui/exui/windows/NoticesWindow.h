#pragma once

#include <windows.h>
#include <string>

namespace ForgottenMelody {
    class NoticeHistory;

    bool showNotices(HWND parent, NoticeHistory& history, const std::string& selected = {});
}

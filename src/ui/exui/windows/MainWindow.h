#pragma once

#include "app/system/Notices.h"
#include "ui/exui/Form.h"

#include <string>

namespace ForgottenMelody {

    // The main window of 忘却的旋律, a placeholder for now.
    class MainWindow : public exui::Form {
    public:
        bool create();
        // A duration of 0 keeps the notice until the next one.
        void showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs);
    };
}

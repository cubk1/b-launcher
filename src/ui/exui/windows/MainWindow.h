#pragma once

#include "app/system/Notices.h"
#include "ui/exui/Form.h"

#include <string>
#include <vector>

namespace ForgottenMelody {

    // The main window of 忘却的旋律, laid out like the 2014 one.
    class MainWindow : public exui::Form {
    public:
        bool create();
        // Follows the instances, the current account and the game.
        void refresh();
        // Failures get a message box; other notices show above the credit line, a duration of 0 keeping them
        // until the next one.
        void showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs);

    protected:
        LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;
        void OnAction(uint32_t id) override;
        void OnSelection(uint32_t id, int index) override;

    private:
        // The keys of the instance list, in its order.
        std::vector<std::string> instanceKeys;
        std::vector<std::wstring> failures;
        bool showingFailures = false;
    };
}

#pragma once

#include "app/system/Notices.h"
#include "ui/exui/Form.h"

#include <optional>
#include <string>
#include <utility>
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
        // Opens the account manager for a game that has no account to log in with, after the failures that say so.
        void askForAccount(const std::string& login, const std::string& authServer);
        // Opens the game log once the current message is done, as a game asks for it from a poll.
        void openLog();

    protected:
        LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;
        void OnAction(uint32_t id) override;
        void OnSelection(uint32_t id, int index) override;

    private:
        // The keys of the instance list, in its order.
        std::vector<std::string> instanceKeys;
        std::vector<std::wstring> failures;
        bool showingFailures = false;
        // How the game wants to log in while the account manager is due to open.
        std::optional<std::pair<std::string, std::string>> accountWanted;

        void showFailures();
    };
}

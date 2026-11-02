#pragma once

#include "app/system/Notices.h"
#include "ui/exui/Form.h"
#include "ui/exui/windows/NoticeHistory.h"

#include <core/modpack/modpack.hpp>

#include <filesystem>
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
        // The single-line hint opens the session's message history. Failures
        // also keep the original message-box presentation.
        void showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs);
        std::string startNotice(Notices::Kind kind, const std::wstring& text);
        void finishNotice(const std::string& id, Notices::Kind kind, const std::wstring& text, long long durationMs);
        // Opens the account manager for a game that has no account to log in with, after the failures that say so.
        void askForAccount(const std::string& login, const std::string& authServer);
        // Opens the game log once the current message is done, as a game asks for it from a poll.
        void openLog();
        // Whether files dropped on the window are taken: not while a dialog or a message box is open over it.
        bool takesDrops() const;
        // Opens 下载中心 on 新建实例 for a dropped modpack once the current message is done, as drops are acted on
        // from a poll.
        void openModpack(const std::filesystem::path& file, const b_launcher_base::modpack::pack_manifest& manifest);

    protected:
        LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;
        void OnAction(uint32_t id) override;
        void OnSelection(uint32_t id, int index) override;

    private:
        // The keys of the instance list, in its order, and whether it says the instances are still being read.
        std::vector<std::string> instanceKeys;
        bool listLoading = false;
        struct Failure {
            std::string id;
            std::wstring text;
        };
        std::vector<Failure> failures;
        bool showingFailures = false;
        NoticeHistory noticeHistory;
        bool showingNotices = false;
        // How the game wants to log in while the account manager is due to open.
        std::optional<std::pair<std::string, std::string>> accountWanted;
        // A dropped modpack while 下载中心 is due to open for it.
        std::optional<std::pair<std::filesystem::path, b_launcher_base::modpack::pack_manifest>> droppedPack;

        void showFailures();
        void refreshNotice();
        void clearReadNotices();
    };
}

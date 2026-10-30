#pragma once

#include "app/system/Notices.h"
#include "ui/exui/Form.h"

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
        // Failures get a message box; other notices show above the credit line, a duration of 0 keeping them
        // until the next one.
        void showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs);
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
        std::vector<std::wstring> failures;
        bool showingFailures = false;
        // How the game wants to log in while the account manager is due to open.
        std::optional<std::pair<std::string, std::string>> accountWanted;
        // A dropped modpack while 下载中心 is due to open for it.
        std::optional<std::pair<std::filesystem::path, b_launcher_base::modpack::pack_manifest>> droppedPack;

        void showFailures();
    };
}

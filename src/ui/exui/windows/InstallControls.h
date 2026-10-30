#pragma once

#include "app/instances/Installer.h"
#include "ui/exui/Form.h"

#include <core/installer/loader.hpp>

#include <map>
#include <string>
#include <vector>

namespace ForgottenMelody {

    namespace blb = b_launcher_base;

    // The loaders of an instance chosen as in the default interface: a row for each loader the game version takes,
    // with its versions in a combo box and, beside it, why it cannot be chosen.
    class LoaderRows {
    public:
        static constexpr int MAX_ROWS = 8;
        static constexpr int ROW_H = 34;
        // Where the combo boxes start, from the names.
        static constexpr int LABEL_W = 120;
        // A name, a combo box and a note for each row.
        static constexpr uint32_t ID_COUNT = 3 * MAX_ROWS;

        // The controls of the rows from firstId on, the first row at (x, y), all of them width wide.
        static std::vector<exui::ControlSpec> controls(uint32_t firstId, int x, int y, int width);

        LoaderRows(exui::Form& form, uint32_t firstId) : form(form), first(firstId) {}

        // Once the controls are made.
        void create();
        void reset(const std::string& minecraft, std::map<blb::install::loader_type, std::string> chosen = {});
        // Shows the loaders the game version takes and their versions; the combo boxes are disabled unless enabled.
        void refresh(bool enabled);
        void hide();
        // Takes what was picked in one of the combo boxes; false when id is none of theirs.
        bool select(uint32_t id, int index);
        // Fetches the lists of versions again.
        void reload();

        int rowCount() const {
            return (int) types.size();
        }
        const std::map<blb::install::loader_type, std::string>& chosen() const {
            return picked;
        }
        std::vector<blb::install::loader_request> requests() const;
        std::vector<blb::install::loader_info> infos() const;

    private:
        exui::Form& form;
        uint32_t first;
        std::string minecraft;
        std::map<blb::install::loader_type, std::string> picked;
        // The loader of each row shown, and the versions behind the options of its combo box after 未选择.
        std::vector<blb::install::loader_type> types;
        std::vector<std::vector<std::string>> versions;
        std::map<uint32_t, std::vector<std::wstring>> offered;

        void sanitize();
        void setOptions(uint32_t id, const std::vector<std::wstring>& options, int selected);
    };

    // An install of the installer as it goes: what it does and for how long, and how far each of its parts is.
    // Once the install ends it stays as it ended until it is dismissed.
    class InstallProgress {
    public:
        // The activity, the time taken, the bar and the list of the parts.
        static constexpr uint32_t ID_COUNT = 4;

        static std::vector<exui::ControlSpec> controls(uint32_t firstId, int x, int y, int width, int height);

        InstallProgress(exui::Form& form, uint32_t firstId) : form(form), first(firstId) {}

        // The columns of the list, once the controls are made.
        void create();
        void refresh();
        void cancel();
        // Forgets an install that ended.
        void dismiss();

        // An install goes on, or one ended and was not dismissed yet.
        bool active() const {
            return Installer::running() || ended;
        }
        bool hasEnded() const {
            return ended;
        }
        bool succeeded() const;

    private:
        exui::Form& form;
        uint32_t first;
        std::vector<Installer::Part> parts;
        std::string activity;
        double elapsed = 0.0;
        bool watching = false, ended = false, cancelled = false;
        std::string error;
        std::vector<std::vector<std::wstring>> shown;
    };
}

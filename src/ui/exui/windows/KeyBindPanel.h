#pragma once

#include "ui/exui/Widget.h"
#include <filesystem>
#include <set>
#include <tuple>

namespace exui { class Button; class CheckBox; class ComboBox; class EditBox; class Label; class ListView; class KeyBoard; }
namespace KeyBinds { struct Table; }

namespace ForgottenMelody {
    class KeyBindPanel : public exui::Panel {
    public:
        enum : uint32_t { DEVICE = 1, GROUP, SEARCH, ALL_KEYS, BOARD, BINDINGS, CHANGE, UNBIND, CANCEL, STATUS, CONFLICTS, COPY };
        KeyBindPanel();
        void SetFile(std::filesystem::path file, bool legacy = false);
        void Refresh();
        bool HandleInput(UINT msg, WPARAM wp, LPARAM lp);
        void OnPaint(Gdiplus::Graphics&) override;

    private:
        exui::ComboBox *device, *group;
        exui::EditBox* search;
        exui::KeyBoard* board;
        exui::ListView* bindings;
        exui::Button *allKeys, *change, *unbind, *cancel;
        exui::Button* copy;
        exui::CheckBox* onlyConflicts;
        exui::Label *status, *searchLabel;
        std::filesystem::path file;
        bool legacy = false;
        std::string groupKey, keyFilter, capturing, notice;
        std::vector<std::string> groupKeys, rowKeys;
        std::vector<std::vector<std::wstring>> shown;
        std::vector<std::tuple<std::string, std::string, std::string, std::string>> conflictInput;
        std::set<std::string> conflicts;
        std::string Selected() const;
        void Assign(const std::string& key);
        void Capture();
        void CancelCapture();
        void Arrange();
        void FindConflicts(const KeyBinds::Table& table);
    };

    void showKeyBinds(HWND parent, const std::string& instance);
}

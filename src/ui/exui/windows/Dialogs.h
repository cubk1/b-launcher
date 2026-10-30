#pragma once

#include "ui/exui/Form.h"
#include "ui/exui/MsgBox.h"
#include "ui/exui/Skins.h"
#include "ui/exui/Window.h"

#include <functional>
#include <string>
#include <vector>

namespace exui {
    class ListView;
}

namespace ForgottenMelody {

    // The average color the 2014 launcher works out for a skin and uses as the base color of its window.
    COLORREF skinBaseColor(exui::Skin skin);

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

    // W06's strip without a caption: a prompt, the name and 保存 / 取消, Enter saving and Esc closing it. save returns
    // a problem to show, or nothing to close the strip.
    void askName(HWND parent, const std::wstring& title, const std::wstring& prompt, const std::wstring& name,
                 const std::function<std::wstring(const std::wstring&)>& save);

    // The UTF-8 text of an edit box without the spaces around it.
    std::string trimmed(const std::wstring& text);

    // A window that follows the services while it is open, refreshing ten times a second.
    class LiveForm : public exui::Form {
    public:
        using Rows = std::vector<std::vector<std::wstring>>;

    protected:
        virtual void refresh() = 0;
        void startRefreshing();
        LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;
        // Shows rows in the list and returns whether they changed. As many rows as before are only rewritten, so
        // that the list keeps its selection and scroll while, say, a progress goes on.
        static bool fill(exui::ListView& list, Rows& shown, Rows rows);
        // The same for rows with a key each: the keys selected before stay selected wherever their rows went, and
        // those that are gone are no longer.
        static void fill(exui::ListView& list, Rows& shown, std::vector<std::string>& shownKeys, Rows rows,
                         std::vector<std::string> keys);
        // The key of the current row, or nothing.
        static std::string selectedKey(const exui::ListView& list, const std::vector<std::string>& shownKeys);
        // The keys of all selected rows, the current one first.
        static std::vector<std::string> selectedKeys(const exui::ListView& list,
                                                     const std::vector<std::string>& shownKeys);
    };

    // The frame of the 2014 下载中心 window, for windows with sections: the sections listed on the left, the shown one
    // on a backing beside them and named on the banner, and 完成 below. The frame's ids are below FIRST_CONTROL.
    class SectionedForm : public LiveForm {
    public:
        static constexpr uint32_t FIRST_CONTROL = 100;

        struct Section {
            std::wstring name;
            uint32_t panel;
        };

        // 下载中心's own measures by default: the banner above top, the sections and the backing down to bottom, the
        // list listWidth wide and 完成 below them at the right. Without a list, a single section takes the width.
        struct Frame {
            int width = 900, height = 600;
            int top = 75, bottom = 525;
            int listWidth = 194;
            int doneWidth = 110, doneHeight = 55;
        };

    protected:
        // Creates the window and an empty panel for each section, which spans the backing; title goes on the banner
        // before the section's name, if given.
        bool createFrame(HWND parent, const std::wstring& caption, const std::wstring& title, std::vector<Section> list,
                         const Frame& frame = {});
        void showSection(int index);
        virtual void sectionShown(int) {}
        // Fills a combo box with options and selects one.
        void choose(uint32_t id, const std::vector<std::wstring>& options, int selected);
        void OnSelection(uint32_t id, int index) override;
        void OnAction(uint32_t id) override;

    private:
        enum : uint32_t { SECTIONS = 1, DONE, TITLE, BACKING };

        std::vector<Section> sections;
        std::wstring subject;
    };
}

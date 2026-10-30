#pragma once
#include "ui/exui/Window.h"
#include <initializer_list>
#include <map>
#include <span>
#include <stdexcept>

namespace exui {

    enum class ControlKind {
        None, // reserves an id without creating a widget
        Button,
        ImageButton,
        Label,
        Hyperlink,
        Edit,
        Combo,
        Radio,
        Check,
        Progress,
        Group,
        List,
    };

    // One control of a window built from a table.
    struct ControlSpec {
        uint32_t id;
        ControlKind kind;
        Gdiplus::Rect bounds;
        std::wstring caption;
        std::wstring font = L"宋体";
        float fontSize = 12;
        int fontStyle = 0;
        Gdiplus::ARGB textColor = 0xD2000000;
        Gdiplus::ARGB glowColor = 0;
        bool visible = true, enabled = true;
        float glowScale = 3;
        bool autoTextColor = false;
    };

    // A window whose controls are addressed by id. Button and hyperlink clicks
    // arrive in OnAction, combo and list selections in OnSelection; neither is
    // called once the window is being destroyed.
    class Form : public Window {
    public:
        Form();

        Widget* FindControl(uint32_t id) const;
        template <class T>
        T& Control(uint32_t id) const {
            auto* result = dynamic_cast<T*>(FindControl(id));
            if (!result) throw std::out_of_range("exui::Form control id or type");
            return *result;
        }
        // Caption of any control; SetText edits an EditBox's text.
        std::wstring Text(uint32_t id) const;
        void SetText(uint32_t id, std::wstring text);
        bool Checked(uint32_t id) const;
        void SetChecked(uint32_t id, bool checked);
        // Clicks an enabled, visible control through the same path as the mouse.
        void Click(uint32_t id);

    protected:
        // Creates the widget for spec, or returns null for ControlKind::None.
        Widget* AddControl(const ControlSpec& spec);
        void AddControls(std::span<const ControlSpec> specs);
        Widget& AddControl(uint32_t id, std::shared_ptr<Widget> widget);
        // Makes the controls children of parent; they keep their place in the window.
        void MoveInto(uint32_t parent, std::initializer_list<uint32_t> ids);
        // The same for a parent that is no control, such as the content of a ScrollPanel.
        void MoveInto(Widget& parent, std::initializer_list<uint32_t> ids);
        // Radio buttons uncheck each other only within their group.
        void GroupRadios(std::initializer_list<uint32_t> ids);

        virtual void OnAction(uint32_t) {}
        virtual void OnSelection(uint32_t, int) {}
        LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override;

    private:
        void Dispatch(Widget* widget);
        void MoveInto(Widget& parent, Widget* child);
        std::map<uint32_t, std::shared_ptr<Widget>> m_controls;
        bool m_destroyed = false;
    };

} // namespace exui

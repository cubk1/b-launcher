#include "ui/exui/Form.h"
#include "ui/exui/widgets/Button.h"
#include "ui/exui/widgets/CheckRadio.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/GroupBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/ProgressBar.h"

namespace exui {
    namespace {
        std::shared_ptr<Widget> MakeWidget(ControlKind kind) {
            switch (kind) {
                case ControlKind::Button: return std::make_shared<Button>();
                case ControlKind::ImageButton: return std::make_shared<ImageButton>();
                case ControlKind::Label: return std::make_shared<Label>();
                case ControlKind::Hyperlink: return std::make_shared<Hyperlink>();
                case ControlKind::Edit: return std::make_shared<EditBox>();
                case ControlKind::Combo: return std::make_shared<ComboBox>();
                case ControlKind::Radio: return std::make_shared<RadioButton>();
                case ControlKind::Check: return std::make_shared<CheckBox>();
                case ControlKind::Progress: return std::make_shared<ProgressBar>();
                case ControlKind::Group: return std::make_shared<GroupBox>();
                case ControlKind::List: return std::make_shared<ListView>();
                case ControlKind::None: break;
            }
            return nullptr;
        }
    } // namespace

    Form::Form() {
        OnButtonClicked = [this](Widget* widget) { Dispatch(widget); };
        OnHyperlinkClicked = [this](Widget* widget) { Dispatch(widget); };
    }

    Widget* Form::FindControl(uint32_t id) const {
        const auto at = m_controls.find(id);
        return at == m_controls.end() ? nullptr : at->second.get();
    }

    std::wstring Form::Text(uint32_t id) const {
        auto* widget = FindControl(id);
        return widget ? widget->GetCaption() : L"";
    }

    void Form::SetText(uint32_t id, std::wstring text) {
        if (auto* edit = dynamic_cast<EditBox*>(FindControl(id)))
            edit->SetText(std::move(text));
        else if (auto* widget = FindControl(id))
            widget->SetCaption(std::move(text));
    }

    bool Form::Checked(uint32_t id) const {
        if (auto* radio = dynamic_cast<RadioButton*>(FindControl(id))) return radio->IsChecked();
        if (auto* check = dynamic_cast<CheckBox*>(FindControl(id))) return check->IsChecked();
        return false;
    }

    void Form::SetChecked(uint32_t id, bool checked) {
        if (auto* radio = dynamic_cast<RadioButton*>(FindControl(id)))
            radio->SetChecked(checked);
        else if (auto* check = dynamic_cast<CheckBox*>(FindControl(id)))
            check->SetChecked(checked);
    }

    void Form::Click(uint32_t id) {
        auto* widget = FindControl(id);
        if (!IsWindow(GetHwnd()) || !widget || !widget->IsEffectivelyEnabled() || !widget->IsEffectivelyVisible())
            return;
        widget->OnMouseDown(1, 1, VK_LBUTTON);
        widget->OnMouseUp(1, 1, VK_LBUTTON);
        widget->Activate();
        // The window notifies after the widget's own action, as for a mouse click.
        if (widget->GetActivationKind() == ActivationKind::Button ||
            widget->GetActivationKind() == ActivationKind::Hyperlink)
            Dispatch(widget);
    }

    Widget* Form::AddControl(const ControlSpec& spec) {
        auto widget = MakeWidget(spec.kind);
        if (!widget) return nullptr;
        widget->SetBounds(spec.bounds.X, spec.bounds.Y, spec.bounds.Width, spec.bounds.Height);
        widget->SetCaption(spec.caption);
        widget->SetFont(spec.font, spec.fontSize, spec.fontStyle);
        widget->SetTextColor(spec.textColor);
        widget->SetGlowColor(spec.glowColor);
        widget->SetGlowScale(spec.glowScale);
        widget->SetVisible(spec.visible);
        widget->SetEnabled(spec.enabled);
        if (auto* label = dynamic_cast<Label*>(widget.get())) label->SetAutoTextColor(spec.autoTextColor);
        if (auto* link = dynamic_cast<Hyperlink*>(widget.get()))
            link->SetColors(spec.textColor, 0xD20060C0, 0xD2800000);
        return &AddControl(spec.id, std::move(widget));
    }

    void Form::AddControls(std::span<const ControlSpec> specs) {
        for (const auto& spec : specs)
            AddControl(spec);
    }

    Widget& Form::AddControl(uint32_t id, std::shared_ptr<Widget> widget) {
        auto notify = [this, id](int index) {
            if (!m_destroyed) OnSelection(id, index);
        };
        if (auto* combo = dynamic_cast<ComboBox*>(widget.get())) combo->OnSelectionChanged = notify;
        if (auto* list = dynamic_cast<ListView*>(widget.get())) {
            list->SetThemeColor(GetBaseColor());
            list->OnSelectionChanged = notify;
        }
        widget->SetId(id);
        m_controls.insert_or_assign(id, widget);
        return Add(std::move(widget));
    }

    void Form::MoveInto(Widget& parent, Widget* child) {
        const auto keep = child ? child->weak_from_this().lock() : nullptr;
        if (!keep || keep.get() == &parent) return;
        const POINT from = keep->GetWindowOrigin(), to = parent.GetWindowOrigin();
        keep->SetBounds(from.x - to.x, from.y - to.y, keep->GetWidth(), keep->GetHeight());
        parent.Add(keep);
    }

    void Form::MoveInto(uint32_t parent, std::initializer_list<uint32_t> ids) {
        if (auto* target = FindControl(parent))
            for (const auto id : ids)
                MoveInto(*target, FindControl(id));
    }

    void Form::GroupRadios(std::initializer_list<uint32_t> ids) {
        auto& group = Add<Panel>(0, 0, GetWidth(), GetHeight());
        for (const auto id : ids)
            MoveInto(group, FindControl(id));
    }

    LRESULT Form::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_CREATE || msg == WM_DESTROY) m_destroyed = msg == WM_DESTROY;
        return Window::HandleMessage(msg, wp, lp);
    }

    void Form::Dispatch(Widget* widget) {
        if (!m_destroyed && widget && FindControl(widget->GetId()) == widget) OnAction(widget->GetId());
    }

} // namespace exui

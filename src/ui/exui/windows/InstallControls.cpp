#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/InstallControls.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/ProgressBar.h"

#include <core/instance/loader_editor.hpp>

#include <algorithm>
#include <cstdio>

namespace ForgottenMelody {

    namespace {
        using blb::install::loader_type;
        using exui::ControlKind;

        constexpr Gdiplus::ARGB MUTED = 0x96000000;

        std::wstring labelOf(const std::string& coreName) {
            if (std::optional<loader_type> type = blb::install::parse_loader_type(coreName)) {
                return Platform::widen(Installer::loaderName(*type));
            }
            return Platform::widen(coreName);
        }

        std::wstring day(const std::string& time) {
            return Platform::widen(time.size() >= 10 ? time.substr(0, 10) : time);
        }

        std::wstring clock(double seconds) {
            int whole = (int) seconds;
            wchar_t text[16];
            std::swprintf(text, 16, L"%d:%02d", whole / 60, whole % 60);
            return text;
        }
    }

    std::vector<exui::ControlSpec> LoaderRows::controls(uint32_t firstId, int x, int y, int width) {
        std::vector<exui::ControlSpec> out;
        for (int i = 0; i < MAX_ROWS; i++) {
            int top = y + i * ROW_H;
            uint32_t id = firstId + 3 * i;
            // Wide enough for Legacy Fabric API.
            out.push_back(
                    {.id = id, .kind = ControlKind::Label, .bounds = {x, top, LABEL_W - 5, 26}, .visible = false});
            out.push_back({.id = id + 1,
                           .kind = ControlKind::Combo,
                           .bounds = {x + LABEL_W, top - 1, 300, 28},
                           .visible = false});
            out.push_back({.id = id + 2,
                           .kind = ControlKind::Label,
                           .bounds = {x + LABEL_W + 306, top, width - LABEL_W - 306, 26},
                           .textColor = MUTED,
                           .visible = false});
        }
        return out;
    }

    void LoaderRows::create() {
        for (int i = 0; i < MAX_ROWS; i++) {
            form.Control<exui::Label>(first + 3 * i + 2).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        }
    }

    void LoaderRows::reset(const std::string& game, std::map<loader_type, std::string> chosen) {
        minecraft = game;
        picked = std::move(chosen);
    }

    std::vector<blb::install::loader_request> LoaderRows::requests() const {
        std::vector<blb::install::loader_request> out;
        for (const auto& [type, version] : picked) {
            out.push_back(blb::install::loader_request{type, version});
        }
        return out;
    }

    std::vector<blb::install::loader_info> LoaderRows::infos() const {
        std::vector<blb::install::loader_info> out;
        for (const auto& [type, version] : picked) {
            out.push_back(blb::install::loader_info{type, version});
        }
        return out;
    }

    // Drops the loaders that the others no longer allow, as the default interface does after each change.
    void LoaderRows::sanitize() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& [type, version] : picked) {
                std::vector<blb::install::loader_info> others;
                for (const auto& [otherType, otherVersion] : picked) {
                    if (otherType != type) {
                        others.push_back(blb::install::loader_info{otherType, otherVersion});
                    }
                }
                for (const blb::instance::loader_choice& choice : blb::instance::loader_choices(minecraft, others)) {
                    if (choice.type == type && choice.state != blb::instance::choice_state::available) {
                        changed = true;
                    }
                }
                if (changed) {
                    picked.erase(type);
                    break;
                }
            }
        }
    }

    void LoaderRows::setOptions(uint32_t id, const std::vector<std::wstring>& options, int selected) {
        auto& combo = form.Control<exui::ComboBox>(id);
        if (offered[id] != options) {
            offered[id] = options;
            combo.ClearItems();
            for (const std::wstring& option : options) {
                combo.AddItem(option);
            }
        }
        if (combo.GetSelectedIndex() != selected) {
            combo.SetSelectedIndex(selected);
        }
    }

    void LoaderRows::refresh(bool enabled) {
        std::vector<blb::instance::loader_choice> choices = blb::instance::loader_choices(minecraft, infos());
        if (choices.size() > MAX_ROWS) {
            choices.resize(MAX_ROWS);
        }
        types.clear();
        versions.assign(choices.size(), {});
        for (int i = 0; i < MAX_ROWS; i++) {
            uint32_t id = first + 3 * i;
            bool shown = i < (int) choices.size();
            for (uint32_t part : {id, id + 1, id + 2}) {
                form.FindControl(part)->SetVisible(shown);
            }
            if (!shown) {
                continue;
            }
            const blb::instance::loader_choice& choice = choices[i];
            types.push_back(choice.type);
            auto chosen = picked.find(choice.type);
            bool isPicked = chosen != picked.end();
            bool available = choice.state == blb::instance::choice_state::available;
            std::wstring name = Platform::widen(Installer::loaderName(choice.type));
            if (form.Text(id) != name) {
                form.SetText(id, name);
            }

            std::vector<std::wstring> options = {L"未选择"};
            std::wstring note;
            int selected = 0;
            if (isPicked || available) {
                // Only the lists that can be chosen from are fetched.
                const Catalog::LoaderVersions& list = Catalog::loader(choice.type, minecraft);
                for (const blb::install::loader_version& version : list.list) {
                    versions[i].push_back(version.version);
                    options.push_back(Platform::widen(version.version) + (version.stable ? L"" : L"（测试版）"));
                    if (isPicked && version.version == chosen->second) {
                        selected = (int) versions[i].size();
                        note = day(version.release_time);
                    }
                }
                if (isPicked && selected == 0) {
                    versions[i].insert(versions[i].begin(), chosen->second);
                    options.insert(options.begin() + 1, Platform::widen(chosen->second));
                    selected = 1;
                }
                if (!isPicked && list.list.empty()) {
                    note = list.status == Catalog::Status::Loading ? L"正在获取版本…"
                            : list.status == Catalog::Status::Failed
                            ? L"获取版本失败：" + Platform::widen(list.error)
                            : name + L" 没有适用于 " + Platform::widen(minecraft) + L" 的版本";
                }
            } else if (choice.state == blb::instance::choice_state::incompatible) {
                note = L"与 " + labelOf(choice.related) + L" 不兼容";
            } else if (choice.state == blb::instance::choice_state::requires_loader) {
                note = L"需要先选择 " + labelOf(choice.related);
            }
            setOptions(id + 1, options, selected);
            form.FindControl(id + 1)->SetEnabled(enabled && options.size() > 1);
            if (form.Text(id + 2) != note) {
                form.SetText(id + 2, note);
            }
        }
    }

    void LoaderRows::hide() {
        types.clear();
        versions.clear();
        for (uint32_t id = first; id < first + ID_COUNT; id++) {
            form.FindControl(id)->SetVisible(false);
        }
    }

    bool LoaderRows::select(uint32_t id, int index) {
        if (id < first || id >= first + ID_COUNT || (id - first) % 3 != 1) {
            return false;
        }
        size_t row = (id - first) / 3;
        if (row >= types.size() || index < 0) {
            return true;
        }
        if (index == 0 || index > (int) versions[row].size()) {
            picked.erase(types[row]);
        } else {
            picked[types[row]] = versions[row][index - 1];
        }
        sanitize();
        return true;
    }

    void LoaderRows::reload() {
        for (loader_type type : types) {
            Catalog::refreshLoader(type, minecraft);
        }
    }

    std::vector<exui::ControlSpec> InstallProgress::controls(uint32_t firstId, int x, int y, int width, int height) {
        return {
                {.id = firstId, .kind = ControlKind::Label, .bounds = {x, y, width - 106, 26}},
                {.id = firstId + 1,
                 .kind = ControlKind::Label,
                 .bounds = {x + width - 100, y, 100, 26},
                 .textColor = MUTED},
                {.id = firstId + 2, .kind = ControlKind::Progress, .bounds = {x, y + 34, width, 18}},
                {.id = firstId + 3, .kind = ControlKind::List, .bounds = {x, y + 60, width, height - 60}},
        };
    }

    void InstallProgress::create() {
        form.Control<exui::Label>(first).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        form.Control<exui::Label>(first + 1).SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
        auto& list = form.Control<exui::ListView>(first + 3);
        list.InsertColumn(L"项目", 180);
        list.InsertColumn(L"进度", 70);
        list.InsertColumn(L"详情", 370);
    }

    bool InstallProgress::succeeded() const {
        return ended && !parts.empty() && std::all_of(parts.begin(), parts.end(), [](const Installer::Part& part) {
                   return part.status == Installer::Part::Status::Done;
               });
    }

    void InstallProgress::cancel() {
        if (Installer::running()) {
            cancelled = true;
            Installer::cancel();
        }
    }

    void InstallProgress::dismiss() {
        ended = cancelled = false;
        parts.clear();
        error.clear();
    }

    void InstallProgress::refresh() {
        if (Installer::running()) {
            if (!watching) {
                dismiss();
            }
            watching = true;
            if (std::vector<Installer::Part> now = Installer::parts(); !now.empty()) {
                parts = std::move(now);
            }
            activity = Installer::activity();
            elapsed = Installer::elapsed();
        } else if (watching) {
            watching = false;
            ended = true;
            error = Installer::lastError();
        }
        if (!active()) {
            return;
        }

        // The installer says 安装完成 for a while before it stops, whatever it did.
        bool done = !parts.empty() && std::all_of(parts.begin(), parts.end(), [](const Installer::Part& part) {
            return part.status == Installer::Part::Status::Done;
        });
        std::wstring status = done ? what + L"完成"
                : !ended           ? Platform::widen(activity)
                : !error.empty()   ? what + L"失败：" + Platform::widen(error)
                                   : L"已取消" + what;
        if (form.Text(first) != status) {
            form.SetText(first, status);
        }
        std::wstring taken = L"已用 " + clock(elapsed);
        if (form.Text(first + 1) != taken) {
            form.SetText(first + 1, taken);
        }
        float fraction = done ? 1.0f : Installer::running() ? Installer::progress() : -1.0f;
        auto& bar = form.Control<exui::ProgressBar>(first + 2);
        if (fraction >= 0.0f || !ended) {
            bar.SetPosition((int) (std::max(0.0f, fraction) * 100));
        }

        std::vector<std::vector<std::wstring>> rows;
        for (const Installer::Part& part : parts) {
            std::wstring amount, detail = Platform::widen(part.detail);
            switch (part.status) {
                // A part with nothing to do has no share to show.
                case Installer::Part::Status::Done: amount = part.fraction >= 0.0f ? L"100%" : L""; break;
                case Installer::Part::Status::Failed:
                    // Cancelling fails the part that was going on; the reason of a failure is on top.
                    amount = ended && error.empty() ? L"已取消" : L"失败";
                    detail.clear();
                    break;
                case Installer::Part::Status::Running:
                    if (ended) {
                        // The installer leaves the parts as they were when it fails or is cancelled.
                        amount = error.empty() ? L"已取消" : L"失败";
                        detail.clear();
                    } else {
                        amount = part.fraction >= 0.0f ? std::to_wstring((int) (part.fraction * 100)) + L"%" : L"…";
                    }
                    break;
                case Installer::Part::Status::Waiting:
                    if (ended) {
                        detail = L"未进行";
                    }
                    break;
            }
            rows.push_back({Platform::widen(part.label), amount, detail});
        }
        auto& list = form.Control<exui::ListView>(first + 3);
        if (rows.size() != shown.size() || list.GetRowCount() != (int) rows.size()) {
            list.ClearRows();
            for (const auto& row : rows) {
                list.InsertRow(row);
            }
        } else {
            for (size_t i = 0; i < rows.size(); i++) {
                for (size_t j = 0; j < rows[i].size(); j++) {
                    if (rows[i][j] != shown[i][j]) {
                        list.SetCellText((int) i, (int) j, rows[i][j]);
                    }
                }
            }
        }
        shown = std::move(rows);
    }
}

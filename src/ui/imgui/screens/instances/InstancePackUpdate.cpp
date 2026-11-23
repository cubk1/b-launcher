#include "ui/imgui/screens/instances/InstancePackUpdate.h"
#include "app/instances/PackUpdates.h"
#include "app/launch/Game.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/instances/InstanceScreenshots.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace InstancePackUpdate {

    using PackUpdates::Phase;

    namespace {
        bool keepModified = false;

        bool release(const Addons::Version& version) {
            return version.type.empty() || version.type == "release";
        }

        const Addons::Version* newest(const Addons::Versions& versions, const PackUpdates::Origin& origin) {
            bool stable = true;
            for (const Addons::Version& version : versions.list) {
                if (version.id == origin.version) {
                    stable = release(version);
                }
            }
            const Addons::Version* best = nullptr;
            for (const Addons::Version& version : versions.list) {
                if ((!stable || release(version)) && (best == nullptr || version.published > best->published)) {
                    best = &version;
                }
            }
            return best;
        }

        bool mine(const std::string& key) {
            return PackUpdates::state().instanceKey == key;
        }

        void valueRow(const std::string& label, const std::string& value, float y, uint32_t color) {
            Page::label(label, y);
            float available = Page::controlRight() - Page::left() - 140.0f;
            std::string shown = UiFont::fitEnd(value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, available);
            float w = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, Page::controlRight() - w, y, Page::ROW_H, color, UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        std::string joined(const std::vector<std::string>& parts) {
            std::string out;
            for (const std::string& part : parts) {
                out += (out.empty() ? "" : "、") + part;
            }
            return out;
        }

        bool pending(const PackUpdates::Summary& summary) {
            return summary.added + summary.changed + summary.removed > 0 || summary.loadersChanged ||
                    (!keepModified && !summary.modified.empty());
        }

        int summaryRows(const std::string& key) {
            const PackUpdates::State& state = PackUpdates::state();
            if (!mine(key) || state.phase == Phase::Idle) {
                return 0;
            }
            if (state.phase != Phase::Planned && state.phase != Phase::Applying) {
                return 1;
            }
            return 4 + (state.summary.modified.empty() ? 0 : 1);
        }

        void summaryCard(const std::string& key, const PackUpdates::Origin& origin, float y, bool blocked) {
            const PackUpdates::State& state = PackUpdates::state();
            int rows = summaryRows(key);
            Page::caption("更新内容", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, rows);
            if (state.phase == Phase::Planning) {
                Page::spinner(Page::left() + 24.0f, rowY + Page::ROW_H * 0.5f, 6.0f, UiTheme::accent());
                UiRender::textY(PackUpdates::progress(), Page::left() + 40.0f, rowY, Page::ROW_H,
                                UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                return;
            }
            if (state.phase == Phase::Failed) {
                std::string text =
                        UiFont::fitEnd(state.error, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::width() - 32.0f);
                UiRender::textY(text, Page::left() + 16.0f, rowY, Page::ROW_H, UiTheme::danger(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return;
            }
            const PackUpdates::Summary& summary = state.summary;
            uint32_t ink = UiTheme::textSecondary();
            std::string from = origin.versionName.empty() ? std::string("当前版本") : origin.versionName;
            valueRow("版本", from + " → " + summary.version, rowY, ink);
            std::string loaders = "Minecraft " + summary.minecraft;
            if (!summary.loaders.empty()) {
                loaders += "、" + joined(summary.loaders);
            }
            valueRow("游戏和加载器", summary.loadersChanged ? "更新为 " + loaders : loaders, rowY + Page::ROW_H, ink);
            std::vector<std::string> changes;
            if (summary.added > 0) {
                changes.push_back("新增 " + std::to_string(summary.added) + " 个");
            }
            if (summary.changed > 0) {
                changes.push_back("更新 " + std::to_string(summary.changed) + " 个");
            }
            if (summary.removed > 0) {
                changes.push_back("删除 " + std::to_string(summary.removed) + " 个");
            }
            valueRow("文件", changes.empty() ? std::string("没有变化") : joined(changes), rowY + 2 * Page::ROW_H, ink);
            uint64_t bytes = summary.bytes + (keepModified ? 0 : summary.modifiedBytes);
            valueRow("需要下载", InstanceScreenshots::sizeText(bytes), rowY + 3 * Page::ROW_H, ink);
            if (!summary.modified.empty()) {
                float toggleY = rowY + 4 * Page::ROW_H;
                Page::label("保留本地改过的 " + std::to_string(summary.modified.size()) + " 个文件", toggleY);
                static const std::string id = "packupdate#keep";
                static const auto flag = std::make_shared<FlagBinding>(
                        id, [] { return keepModified; }, [](bool on) { keepModified = on; });
                ToggleWidget widget(id, flag, true);
                widget.bounds(Page::controlRight() - 30.0f, toggleY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                        .hitArea(Page::left(), toggleY, Page::width(), Page::ROW_H)
                        .block(blocked || state.phase == Phase::Applying);
                widget.paint();
            }
        }

        void versionRows(const std::string& key, const PackUpdates::Origin& origin, const Addons::Versions& versions,
                         float y, bool blocked) {
            float x = Page::left();
            float w = Page::width();
            int count = (int) versions.list.size();
            Page::listCard(x, y, w, Page::ROW_H, count);
            const PackUpdates::State& state = PackUpdates::state();
            std::string chosen = mine(key) && state.phase != Phase::Idle ? state.versionId : std::string();
            for (int i = 0; i < count; i++) {
                float rowY = y + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const Addons::Version& version = versions.list[i];
                bool selected = version.id == chosen;
                bool hovered = !blocked && NewUi::hit(x, rowY, w, Page::ROW_H);
                float lit = UiState::motion("packupdate#row#" + version.id, hovered ? 1.0f : 0.0f, 20.0f);
                if (lit > 0.001f) {
                    UiRender::roundRect(x, rowY + 2.0f, w, Page::ROW_H - 4.0f, 8.0f,
                                        UiTheme::overlay(Ui::round(14 * lit)));
                }
                if (selected) {
                    UiRender::outline(x, rowY + 2.0f, w, Page::ROW_H - 4.0f, 8.0f, UiTheme::accent(), 0xF, 1.4f);
                }
                float right = x + w - 16.0f;
                if (selected) {
                    UiWidgets::icon(Ui::glyph(0xf00c), right - 6.0f, rowY + Page::ROW_H * 0.5f, UiTheme::accentText(),
                                    11.0f);
                    right -= 22.0f;
                }
                std::string date = DownloadsParts::shortDate(version.published);
                float dateW = UiFont::width(date, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(date, right - dateW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                float textX = x + 16.0f;
                bool current = version.id == origin.version;
                bool preview = !release(version);
                float tagsW = (current ? 48.0f : 0.0f) + (preview ? 60.0f : 0.0f);
                std::string name = version.number.empty() ? version.name : version.number;
                name = UiFont::fitEnd(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                      std::max(40.0f, right - dateW - 12.0f - textX - tagsW));
                UiRender::textY(name, textX, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                float tagX = textX + UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                if (preview) {
                    tagX += Page::tag(version.type == "alpha" ? "预览版" : "测试版", tagX, rowY,
                                      UiTheme::surfaceRaised(), UiTheme::textSecondary()) +
                            4.0f;
                }
                if (current) {
                    Page::tag("当前", tagX, rowY, UiTheme::accentSoft(255), UiTheme::accentText());
                }
                if (!blocked && !selected && NewUi::click(x, rowY, w, Page::ROW_H)) {
                    keepModified = false;
                    PackUpdates::plan(key, version);
                }
            }
        }

        void updateButton(const Instances::Entry& entry, bool gameBusy, bool blocked) {
            const PackUpdates::State& state = PackUpdates::state();
            float y = MainWindow::height() - 28.0f - Page::FIELD_H;
            bool applying = state.phase == Phase::Applying;
            bool here = applying && mine(entry.key);
            std::string label = here ? "取消" : "更新";
            float w = std::max(88.0f, Page::buttonWidth(label, here ? 0 : 0xf2f1));
            float x = Page::right() - w;
            std::string note;
            uint32_t noteInk = UiTheme::textMuted();
            if (here) {
                note = PackUpdates::progress();
                noteInk = UiTheme::textSecondary();
            } else if (applying) {
                note = "另一个实例正在更新";
            } else if (gameBusy) {
                note = "游戏运行中";
            }
            if (!note.empty()) {
                float noteW = UiFont::width(note, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(note, x - 14.0f - noteW, y, Page::FIELD_H, noteInk, UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (here) {
                    Page::spinner(x - 14.0f - noteW - 14.0f, y + Page::FIELD_H * 0.5f, 6.0f, UiTheme::accent());
                }
            }
            bool ready = state.phase == Phase::Planned && mine(entry.key) && pending(state.summary);
            if (Page::button("packupdate#go", label, x, y, w, !here, blocked || (!here && (!ready || gameBusy)),
                             here ? 0 : 0xf2f1)) {
                if (here) {
                    PackUpdates::cancel();
                } else {
                    PackUpdates::apply(keepModified);
                }
            }
        }
    }

    std::string rowValue(const std::string& instanceKey) {
        const PackUpdates::Origin* origin = PackUpdates::origin(instanceKey);
        if (origin == nullptr) {
            return std::string();
        }
        const Addons::Versions& versions = Addons::versions(origin->source, origin->project);
        if (versions.status == Addons::Status::Ready) {
            const Addons::Version* latest = newest(versions, *origin);
            if (latest != nullptr && latest->id != origin->version) {
                return "可更新到 " + (latest->number.empty() ? latest->name : latest->number);
            }
        }
        return origin->versionName;
    }

    void reset() {
        keepModified = false;
        PackUpdates::reset();
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        bool back = Page::back("packupdate#back", "返回", Page::left() - 4.0f, Page::top() - 2.0f, covered);
        const char* title = "整合包更新";
        Page::title(title, Page::left() + 30.0f, Page::top());
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::right() - nameX),
                        nameX, Page::top(), 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);

        const PackUpdates::Origin* origin = PackUpdates::origin(key);
        if (origin == nullptr) {
            return true;
        }
        const Addons::Versions& versions = Addons::versions(origin->source, origin->project);
        bool gameBusy = Game::state(key) != Game::State::Idle;
        bool applying = PackUpdates::state().phase == Phase::Applying;

        int rows = summaryRows(key);
        float summaryH = rows > 0 ? Page::CAPTION_H + rows * Page::ROW_H + Page::GAP : 0.0f;
        int versionCount = versions.status == Addons::Status::Ready ? std::max(1, (int) versions.list.size()) : 1;
        float contentH = summaryH + Page::CAPTION_H + versionCount * Page::ROW_H;
        float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
        Page::Scroll body("packupdate#body#" + key, Page::top() + 40.0f, buttonTop - 14.0f, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered || applying;
        float y = body.top - body.offset;

        if (rows > 0) {
            summaryCard(key, *origin, y, blocked);
            y += summaryH;
        }
        Page::caption("版本", y);
        y += Page::CAPTION_H;
        if (versions.status == Addons::Status::Loading) {
            Page::card(y, 1);
            Page::spinner(Page::left() + 24.0f, y + Page::ROW_H * 0.5f, 6.0f, UiTheme::accent());
            UiRender::textY("正在获取版本列表", Page::left() + 40.0f, y, Page::ROW_H, UiTheme::textSecondary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        } else if (versions.status == Addons::Status::Failed || versions.list.empty()) {
            Page::card(y, 1);
            std::string text = versions.list.empty() && versions.status != Addons::Status::Failed
                    ? std::string("没有可用的版本")
                    : UiFont::fitEnd("无法获取版本列表：" + versions.error, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                     Page::width() - 140.0f);
            UiRender::textY(text, Page::left() + 16.0f, y, Page::ROW_H, UiTheme::danger(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (versions.status == Addons::Status::Failed) {
                std::string retry = "重试";
                float w = Page::buttonWidth(retry);
                if (Page::button("packupdate#retry", retry, Page::controlRight() - w, y + Page::FIELD_Y, w, false,
                                 blocked)) {
                    Addons::refreshVersions(origin->source, origin->project);
                }
            }
        } else {
            versionRows(key, *origin, versions, y, blocked);
        }
        body.end();

        updateButton(entry, gameBusy, covered);
        return back;
    }
}

#include "ui/imgui/screens/instances/DeleteDialogs.h"

#include "app/system/Deletion.h"
#include "app/system/Notices.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace DeleteDialogs {

    namespace {
        const char* const DIALOG = "delete#confirm";
        constexpr float WIDTH = 520.0f;
        constexpr float EDGE = 16.0f;
        constexpr float PAD = 24.0f;
        constexpr float LINE = 20.0f;
        constexpr float TITLE_H = 24.0f;
        constexpr float SECTION_GAP = 16.0f;
        constexpr float NOTE_PAD = 12.0f;
        // The crash dialog sizes its buttons with Page::button, so these follow the same rules.
        constexpr float BUTTON_H = Page::FIELD_H;
        constexpr float BUTTON_GAP = 10.0f;
        constexpr float CANCEL_MIN_W = 72.0f;
        constexpr float HOLD_SECONDS = 1.5f;
        const char* const HOLD_TEXT = "长按删除";

        struct Request {
            std::string title;
            std::string lead;
            std::string detail;
            std::vector<std::string> notes;
            std::function<void()> confirmed;
            bool hold = false;
        };

        Request request;
        float held = 0.0f;
        bool holding = false;

        std::vector<std::string> wrapped(const std::string& text, float width) {
            return UiFont::wrap(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, width);
        }

        std::string bytes(unsigned long long value) {
            const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
            double shown = (double) value;
            int unit = 0;
            while (shown >= 1024.0 && unit < 4) {
                shown /= 1024.0;
                unit += 1;
            }
            char text[32];
            std::snprintf(text, sizeof(text), unit == 0 ? "%.0f %s" : "%.1f %s", shown, units[unit]);
            return text;
        }

        struct Layout {
            std::vector<std::string> lead;
            std::vector<std::string> detail;
            std::vector<std::string> note;
            float noteTop = 0.0f;
            float noteHeight = 0.0f;
            float height = 0.0f;
        };

        Layout measure(float width) {
            Layout out;
            float inner = width - 2.0f * PAD;
            out.lead = wrapped(request.lead, inner);
            if (!request.detail.empty()) {
                out.detail = wrapped(request.detail, inner);
            }
            float noteInner = inner - 2.0f * NOTE_PAD;
            for (const std::string& text : request.notes) {
                for (const std::string& line : wrapped(text, noteInner)) {
                    out.note.push_back(line);
                }
            }
            float top = PAD + TITLE_H + SECTION_GAP;
            float body = (float) (out.lead.size() + out.detail.size()) * LINE;
            out.noteTop = top + body + (request.detail.empty() ? 0.0f : 6.0f) + 14.0f;
            out.noteHeight = out.note.empty() ? 0.0f : (float) out.note.size() * LINE + 2.0f * NOTE_PAD;
            float buttons = out.noteHeight > 0.0f ? out.noteTop + out.noteHeight + 18.0f : out.noteTop + 2.0f;
            out.height = buttons + BUTTON_H + PAD;
            return out;
        }

        void reset() {
            request = Request{};
            held = 0.0f;
            holding = false;
        }

        bool dangerButton(const std::string& id, const std::string& text, float x, float y, float w) {
            bool hovered = NewUi::hit(x, y, w, BUTTON_H);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
            uint32_t red = UiTheme::danger() & 0x00FFFFFFu;
            UiRender::roundRect(x, y, w, BUTTON_H, 7.0f, red | ((uint32_t) Ui::round(72.0f + 40.0f * lit) << 24));
            UiRender::outline(x, y, w, BUTTON_H, 7.0f, red | ((uint32_t) Ui::round(140.0f + 70.0f * lit) << 24));
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(text, x + (w - textW) * 0.5f, y, BUTTON_H, UiTheme::danger(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (NewUi::click(x, y, w, BUTTON_H)) {
                UiSound::request(UiSound::forLabel(text));
                return true;
            }
            return false;
        }

        // Releasing the button or moving off it resets the count, so a stray click cannot delete.
        bool holdButton(float x, float y, float w) {
            bool inside = NewUi::hit(x, y, w, BUTTON_H);
            if (!holding) {
                if (NewUi::press(x, y, w, BUTTON_H)) {
                    holding = true;
                    held = 0.0f;
                }
            } else if (!NewUi::mouseDown || !inside) {
                holding = false;
                held = 0.0f;
            }

            bool done = false;
            if (holding) {
                held += ImGui::GetIO().DeltaTime;
                UiState::keepAnimating();
                if (held >= HOLD_SECONDS) {
                    holding = false;
                    held = 0.0f;
                    done = true;
                }
            }

            uint32_t red = UiTheme::danger() & 0x00FFFFFFu;
            UiRender::roundRect(x, y, w, BUTTON_H, 7.0f, red | 0x22000000u);
            // Fills from the left edge, keeping the left corners round and the right ones square.
            float fill = w * std::min(1.0f, held / HOLD_SECONDS);
            if (fill > 0.5f) {
                UiRender::roundRect(x, y, fill, BUTTON_H, 7.0f, red | 0x66000000u, fill >= w - 0.5f ? 0xF : (1 | 4));
            }
            UiRender::outline(x, y, w, BUTTON_H, 7.0f, red | (held > 0.0f ? 0xD0000000u : 0x9A000000u));
            float textW = UiFont::width(HOLD_TEXT, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(HOLD_TEXT, x + (w - textW) * 0.5f, y, BUTTON_H, UiTheme::danger(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            return done;
        }

        void body(float x, float y, float w, float h) {
            Layout layout = measure(w);
            float inner = w - 2.0f * PAD;
            float left = x + PAD;
            float right = left + inner;

            float titleY = y + PAD;
            UiRender::textY(request.title, left, titleY, TITLE_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            float cursor = titleY + TITLE_H + SECTION_GAP;
            for (const std::string& line : layout.lead) {
                UiRender::textY(line, left, cursor, LINE, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                cursor += LINE;
            }
            if (!layout.detail.empty()) {
                cursor += 6.0f;
                for (const std::string& line : layout.detail) {
                    UiRender::textY(line, left, cursor, LINE, UiTheme::textSecondary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    cursor += LINE;
                }
            }

            if (!layout.note.empty()) {
                float noteY = y + layout.noteTop;
                uint32_t shade = (UiTheme::danger() & 0x00FFFFFFu) | 0x1A000000u;
                UiRender::roundRect(left, noteY, inner, layout.noteHeight, 8.0f, shade);
                UiRender::roundRect(left, noteY + 1.0f, 3.0f, layout.noteHeight - 2.0f, 1.5f,
                                    (UiTheme::danger() & 0x00FFFFFFu) | 0xB4000000u);
                float lineY = noteY + NOTE_PAD;
                for (const std::string& line : layout.note) {
                    UiRender::textY(line, left + NOTE_PAD, lineY, LINE, UiTheme::textPrimary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    lineY += LINE;
                }
            }

            float buttonY = y + h - PAD - BUTTON_H;
            float cancelW = std::max(CANCEL_MIN_W, Page::buttonWidth("取消"));
            if (request.hold) {
                float holdW = Page::buttonWidth(HOLD_TEXT);
                if (Page::button("delete#cancel", "取消", right - holdW - BUTTON_GAP - cancelW, buttonY, cancelW,
                                 false, false, 0, BUTTON_H)) {
                    Dialog::close();
                    return;
                }
                if (holdButton(right - holdW, buttonY, holdW)) {
                    std::function<void()> confirmed = request.confirmed;
                    Dialog::close();
                    if (confirmed) {
                        confirmed();
                    }
                }
                return;
            }

            float permanentW = std::max(CANCEL_MIN_W, Page::buttonWidth("永久删除"));
            if (dangerButton("delete#permanent", "永久删除", right - permanentW, buttonY, permanentW)) {
                std::function<void()> confirmed = request.confirmed;
                Dialog::close();
                if (confirmed) {
                    confirmed();
                }
                return;
            }
            float next = right - permanentW - BUTTON_GAP;
            if (Page::button("delete#cancel", "取消", next - cancelW, buttonY, cancelW, false, false, 0, BUTTON_H)) {
                Dialog::close();
            }
        }

        void present() {
            float width = std::min(WIDTH, MainWindow::width() - 2.0f * EDGE);
            Layout layout = measure(width);
            Dialog::open(DIALOG, width, layout.height, body, reset);
        }
    }

    void openInstance(const std::string& name, const std::vector<std::string>& notes,
                      std::function<void()> confirmed) {
        reset();
        request.title = "永久删除实例";
        request.lead = "“" + name + "”及其模组、存档、日志和设置都会被永久删除。";
        request.detail = "文件将被直接删除，无法从系统回收站恢复。";
        request.notes = notes;
        request.confirmed = std::move(confirmed);
        request.hold = true;
        present();
    }

    namespace {
        std::vector<std::string> recycleNotes(const std::string& reason, bool tooLarge,
                                              const std::filesystem::path& file) {
            std::vector<std::string> notes;
            notes.push_back(reason + "。");
            if (tooLarge) {
                Deletion::Space space = Deletion::space(file);
                if (space.limitBytes > 0) {
                    notes.push_back("这个磁盘的回收站容量为 " + bytes(space.limitBytes) + "。");
                }
            }
            return notes;
        }
    }

    void openInstanceRecycleFailed(const std::string& name, const std::string& reason, bool tooLarge,
                                   const std::filesystem::path& folder, std::function<void()> confirmed) {
        openInstance(name, recycleNotes(reason, tooLarge, folder), std::move(confirmed));
    }

    void openResource(const std::string& name, std::function<void()> confirmed) {
        reset();
        request.title = "永久删除文件";
        request.lead = "“" + name + "”将被直接永久删除。";
        request.detail = "文件将被直接删除，无法从系统回收站恢复。";
        request.confirmed = std::move(confirmed);
        present();
    }

    void removeResourceFile(const std::string& name, const std::filesystem::path& file, Erase erase) {
        auto failed = [name](const Deletion::Outcome& outcome) {
            if (!outcome.ok && !outcome.needsPermanent) {
                Notices::add(Notices::Kind::Failed, "无法删除 " + name + "：" + outcome.reason, 4000);
            }
        };
        if (Deletion::mode() == Deletion::Mode::Permanent && Deletion::confirmPermanent()) {
            openResource(name, [erase, failed] { failed(erase(true)); });
            return;
        }
        Deletion::Outcome outcome = erase(false);
        if (outcome.ok) {
            return;
        }
        if (outcome.needsPermanent) {
            openRecycleFailed(name, outcome.reason, outcome.tooLarge, file,
                              [erase, failed] { failed(erase(true)); });
            return;
        }
        failed(outcome);
    }

    void openRecycleFailed(const std::string& name, const std::string& reason, bool tooLarge,
                           const std::filesystem::path& file, std::function<void()> confirmed) {
        reset();
        request.title = "无法移到系统回收站";
        request.lead = "“" + name + "”删除失败。";
        request.detail = "可以改为永久删除，删除后无法恢复。";
        request.notes = recycleNotes(reason, tooLarge, file);
        request.confirmed = std::move(confirmed);
        present();
    }
}

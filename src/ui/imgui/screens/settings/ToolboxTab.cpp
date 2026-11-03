#include "ui/imgui/screens/settings/ToolboxTab.h"
#include "app/system/CustomDownload.h"
#include "app/system/Notices.h"
#if defined(_WIN32)
#include "app/system/PclThemes.h"
#endif
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ToolboxTab {

    namespace blb = b_launcher_base;

    namespace {
        const char* const URL_OWNER = "settings#tools#url";
        const char* const FOLDER_OWNER = "settings#tools#folder";
        const char* const NAME_OWNER = "settings#tools#name";
        constexpr float LABEL_OFFSET = 100.0f;
        constexpr float BAR_H = 6.0f;
        constexpr float TEXT_LINE = 18.0f;
        constexpr float LINE_GAP = 5.0f;
        constexpr double SWEEP_SECONDS = 1.4;

        std::string urlText;
        std::string folderText;
        std::string nameText;
        bool folderInit = false;

        std::shared_ptr<TextBinding> urlValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        URL_OWNER, [] { return urlText; }, [](const std::string& text) { urlText = Ui::trim(text); },
                        "https://");
                binding->limit = 2048;
                return binding;
            }();
            return value;
        }

        std::shared_ptr<TextBinding> folderValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        FOLDER_OWNER, [] { return folderText; },
                        [](const std::string& text) { folderText = Ui::trim(text); }, "保存文件夹");
                binding->limit = 1024;
                return binding;
            }();
            return value;
        }

        std::shared_ptr<TextBinding> nameValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        NAME_OWNER, [] { return nameText; }, [](const std::string& text) { nameText = Ui::trim(text); },
                        "自动根据地址生成");
                binding->limit = 255;
                return binding;
            }();
            return value;
        }

        std::string liveUrl() {
            return Ui::trim(NewUi::textEditing == URL_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : urlText);
        }

        std::string liveFolder() {
            return Ui::trim(NewUi::textEditing == FOLDER_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : folderText);
        }

        std::string liveName() {
            return Ui::trim(NewUi::textEditing == NAME_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : nameText);
        }

        std::string amount(uint64_t bytes) {
            char text[32];
            if (bytes >= (1ull << 30)) {
                std::snprintf(text, sizeof(text), "%.2f GB", bytes / 1073741824.0);
            } else if (bytes >= (1ull << 20)) {
                std::snprintf(text, sizeof(text), "%.1f MB", bytes / 1048576.0);
            } else if (bytes >= 1024) {
                std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
            } else {
                std::snprintf(text, sizeof(text), "%llu B", (unsigned long long) bytes);
            }
            return text;
        }

        std::string timeOf(int seconds) {
            if (seconds <= 0) {
                return "0 秒";
            }
            if (seconds < 60) {
                return std::to_string(seconds) + " 秒";
            }
            if (seconds < 3600) {
                return std::to_string(seconds / 60) + " 分 " + std::to_string(seconds % 60) + " 秒";
            }
            return std::to_string(seconds / 3600) + " 小时 " + std::to_string(seconds / 60 % 60) + " 分";
        }

        std::string fit(const std::string& text, float room, float size = UiWidgets::TEXT_CONTROL) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, size, std::max(0.0f, room));
        }

        void ensureFolderDefault() {
            if (!folderInit) {
                folderInit = true;
                folderText = Platform::display(Platform::exeDirectory());
            }
        }

        void begin() {
            std::string url = liveUrl();
            std::string folder = liveFolder();
            std::string name = liveName();
            if (NewUi::textEditing == URL_OWNER || NewUi::textEditing == FOLDER_OWNER ||
                NewUi::textEditing == NAME_OWNER) {
                NewUi::cancelTextEdit();
            }
            urlText = url;
            folderText = folder;
            nameText = name;
            if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
                Notices::add(Notices::Kind::Failed, "下载地址需要以 http:// 或 https:// 开头", 4000);
                return;
            }
            if (folder.empty()) {
                Notices::add(Notices::Kind::Failed, "请选择保存文件夹", 4000);
                return;
            }
            if (name.empty()) {
                name = CustomDownload::nameOf(url);
            }
            if (!blb::fs::safe_filename(name)) {
                Notices::add(Notices::Kind::Failed, "文件名无效，含有不能用的字符", 4000);
                return;
            }
            std::filesystem::path target = blb::path_from_utf8(folder) / blb::path_from_utf8(name);
            std::error_code error;
            bool exists = std::filesystem::exists(target, error);
            if (error) {
                Notices::add(Notices::Kind::Failed, "无法检查目标文件：" + error.message(), 4000);
                return;
            }
            if (exists) {
                Dialog::open("settings#tools#overwrite", 480.0f, 164.0f,
                             [url, target](float x, float y, float w, float h) {
                                 UiRender::textY("覆盖已有文件？", x + 20.0f, y + 20.0f, 24.0f,
                                                 UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
                                 std::string path = UiFont::fitStart(Platform::display(target), UiFont::MEDIUM,
                                                                     UiWidgets::TEXT_CONTROL, w - 40.0f);
                                 UiRender::textY(path, x + 20.0f, y + 52.0f, 22.0f, UiTheme::textMuted(),
                                                 UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                                 UiRender::textY("原文件将被替换。", x + 20.0f, y + 78.0f, 22.0f,
                                                 UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                                 float buttonY = y + h - 20.0f - Page::FIELD_H;
                                 if (Page::button("settings#tools#overwrite#cancel", "取消", x + w - 204.0f,
                                                  buttonY, 72.0f, false, false)) {
                                     Dialog::close();
                                     return;
                                 }
                                 if (Page::button("settings#tools#overwrite#confirm", "覆盖下载", x + w - 120.0f,
                                                  buttonY, 100.0f, true, false)) {
                                     Dialog::close();
                                     CustomDownload::start(url, target);
                                 }
                             });
                return;
            }
            CustomDownload::start(url, target);
        }

        void pickFolder() {
            std::string folder = liveFolder();
            std::filesystem::path initial = folder.empty() ? Platform::exeDirectory() : blb::path_from_utf8(folder);
            std::optional<std::filesystem::path> picked = Platform::pickFolder(initial);
            if (picked) {
                if (NewUi::textEditing == FOLDER_OWNER) {
                    NewUi::cancelTextEdit();
                }
                folderText = Platform::display(*picked);
            }
        }

        void urlRow(float rowY, bool blocked) {
            Page::label("下载地址", rowY);
            float fieldX = Page::columnLeft() + LABEL_OFFSET;
            float fieldW = Page::controlRight() - fieldX;
            TextFieldWidget field(urlValue());
            field.box(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .bounds(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .block(blocked || CustomDownload::busy());
            field.paint();
        }

        void folderRow(float rowY, bool blocked) {
            Page::label("保存到", rowY);
            std::string pickLabel = "选择";
            float pickW = std::max(60.0f, Page::buttonWidth(pickLabel));
            float pickX = Page::controlRight() - pickW;
            bool pickPressed = Page::button("settings#tools#pick", pickLabel, pickX, rowY + Page::FIELD_Y, pickW, false,
                                            blocked || CustomDownload::busy());
            float fieldX = Page::columnLeft() + LABEL_OFFSET;
            float fieldW = pickX - 8.0f - fieldX;
            TextFieldWidget field(folderValue());
            field.box(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .bounds(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .block(blocked || CustomDownload::busy());
            field.paint();
            if (pickPressed) {
                pickFolder();
            }
        }

        void nameRow(float rowY, bool blocked) {
            Page::label("文件名", rowY);
            float fieldX = Page::columnLeft() + LABEL_OFFSET;
            float fieldW = Page::controlRight() - fieldX;
            TextFieldWidget field(nameValue());
            field.box(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .bounds(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .block(blocked || CustomDownload::busy());
            field.paint();
        }

        uint32_t amber() {
            return UiTheme::isLight() ? 0xFFB7791Fu : 0xFFE8A33Du;
        }

        float fractionOf(const CustomDownload::Status& status) {
            if (status.state == CustomDownload::State::Done) {
                return 1.0f;
            }
            return status.total > 0 ? std::min(1.0f, (float) ((double) status.done / (double) status.total)) : 0.0f;
        }

        void progressBar(const CustomDownload::Status& status, float x, float y, float w) {
            UiRender::roundRect(x, y, w, BAR_H, BAR_H * 0.5f, UiTheme::outline());
            bool indeterminate = status.state == CustomDownload::State::Probing ||
                    (status.state == CustomDownload::State::Running && status.total == 0);
            if (indeterminate) {
                UiState::keepAnimating();
                double seconds =
                        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                float t = (float) (std::fmod(seconds, SWEEP_SECONDS) / SWEEP_SECONDS);
                float segment = w * 0.3f;
                float head = x - segment + (w + segment) * t;
                float from = std::max(x, head);
                float to = std::min(x + w, head + segment);
                if (to - from > 0.5f) {
                    float width = std::max(BAR_H, to - from);
                    UiRender::roundRect(std::min(from, x + w - width), y, width, BAR_H, BAR_H * 0.5f,
                                        UiTheme::accent());
                }
                return;
            }
            uint32_t color = status.state == CustomDownload::State::Failed ? UiTheme::danger() : UiTheme::accent();
            float shown = UiState::motion("settings#tools#bar", fractionOf(status), 10.0f);
            if (shown > 0.001f) {
                UiRender::roundRect(x, y, std::max(BAR_H, w * shown), BAR_H, BAR_H * 0.5f, color);
            }
        }

        std::vector<std::string> detailsOf(const CustomDownload::Status& status) {
            std::vector<std::string> parts;
            if (status.state == CustomDownload::State::Done) {
                parts.push_back(amount(std::max(status.done, status.total)));
                return parts;
            }
            if (status.state != CustomDownload::State::Running) {
                return parts;
            }
            parts.push_back(status.total > 0 ? amount(status.done) + " / " + amount(status.total)
                                             : amount(status.done));
            if (status.speed > 0.0) {
                parts.push_back(amount((uint64_t) status.speed) + "/s");
            }
            if (status.total > status.done && status.speed > 1.0) {
                parts.push_back("剩余 " + timeOf((int) ((double) (status.total - status.done) / status.speed)));
            }
            return parts;
        }

        void progressBlock(const CustomDownload::Status& status, float x, float rowY, float w) {
            std::string label;
            uint32_t color = UiTheme::textPrimary();
            switch (status.state) {
                case CustomDownload::State::Probing:
                    label = "正在连接…";
                    color = amber();
                    break;
                case CustomDownload::State::Running: label = "下载中"; break;
                case CustomDownload::State::Done:
                    label = "已完成";
                    color = UiTheme::accent();
                    break;
                case CustomDownload::State::Failed:
                    label = "下载失败：" + (status.error.empty() ? std::string("未知错误") : status.error);
                    color = UiTheme::danger();
                    break;
                default: break;
            }
            std::vector<std::string> parts = detailsOf(status);
            std::string detail;
            float labelW = UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            while (!parts.empty()) {
                detail.clear();
                for (const std::string& part : parts) {
                    detail += (detail.empty() ? "" : "，") + part;
                }
                if (parts.size() == 1 ||
                    labelW + 16.0f + UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) <= w) {
                    break;
                }
                parts.pop_back();
            }
            float top = rowY + (Page::ROW_H - TEXT_LINE - LINE_GAP - BAR_H) * 0.5f;
            float detailW = detail.empty() ? 0.0f : UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!detail.empty()) {
                UiRender::textY(detail, x + w - detailW, top, TEXT_LINE, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            float labelRoom = w - (detail.empty() ? 0.0f : detailW + 16.0f);
            UiRender::textY(fit(label, labelRoom, UiWidgets::TEXT_SMALL), x, top, TEXT_LINE, color, UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            progressBar(status, x, top + TEXT_LINE + LINE_GAP, w);
        }

        void buttonsRow(float rowY, bool blocked) {
            const CustomDownload::Status& status = CustomDownload::status();
            bool downloading =
                    status.state == CustomDownload::State::Probing || status.state == CustomDownload::State::Running;
            std::string actionText = downloading ? "取消下载" : "开始下载";
            int actionGlyph = downloading ? 0xf00d : 0xf019;
            std::string openText = "打开文件夹";
            float actionW = std::max(100.0f, Page::buttonWidth(actionText, actionGlyph));
            float openW = std::max(100.0f, Page::buttonWidth(openText, 0xf07c));
            float gap = 12.0f;
            float openX = Page::controlRight() - openW;
            float actionX = openX - gap - actionW;

            if (status.state != CustomDownload::State::Idle) {
                float blockX = Page::columnLeft() + 16.0f;
                float blockW = actionX - 20.0f - blockX;
                if (blockW > 60.0f) {
                    progressBlock(status, blockX, rowY, blockW);
                }
            }

            bool canAction = !blocked && (downloading || !liveUrl().empty());
            if (Page::button("settings#tools#action", actionText, actionX, rowY + Page::FIELD_Y, actionW, !downloading,
                             !canAction, actionGlyph)) {
                if (downloading) {
                    CustomDownload::cancel();
                } else {
                    begin();
                }
            }
            if (Page::button("settings#tools#open", openText, openX, rowY + Page::FIELD_Y, openW, false,
                             blocked || liveFolder().empty(), 0xf07c)) {
                std::filesystem::path folder = blb::path_from_utf8(liveFolder());
                if (std::filesystem::exists(folder)) {
                    Platform::open(folder);
                } else {
                    Notices::add(Notices::Kind::Failed, "文件夹不存在", 3000);
                }
            }
        }

        void drawHome(bool covered) {
            ensureFolderDefault();
            const int downloadRows = 3;
            const float buttonBlockH = Page::ROW_H + 4.0f;
#if defined(_WIN32)
            float contentH =
                    2.0f * Page::CAPTION_H + downloadRows * Page::ROW_H + buttonBlockH + Page::ROW_H + Page::GAP;
#else
            float contentH = Page::CAPTION_H + downloadRows * Page::ROW_H + buttonBlockH;
#endif
            Page::Scroll body("settings#tools#home", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;

            Page::caption("自定义下载", y);
            y += Page::CAPTION_H;
            Page::card(y, 3);
            urlRow(y, blocked);
            folderRow(y + Page::ROW_H, blocked);
            nameRow(y + 2 * Page::ROW_H, blocked);
            y += 3 * Page::ROW_H + 4.0f;
            buttonsRow(y, blocked);
            y += Page::ROW_H + Page::GAP;

#if defined(_WIN32)
            Page::caption("PCL2", y);
            y += Page::CAPTION_H;
            Page::card(y, 1);
            float textX = Page::columnLeft() + 16.0f;
            UiRender::textY("解锁隐藏主题", textX, y, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            std::string unlockLabel = "一键解锁";
            float unlockW = std::max(90.0f, Page::buttonWidth(unlockLabel, 0xf09c));
            float unlockX = Page::controlRight() - unlockW;
            if (Page::button("settings#tools#pcl", unlockLabel, unlockX, y + Page::FIELD_Y, unlockW, false, blocked,
                             0xf09c)) {
                PclThemes::unlock();
                const PclThemes::Status& result = PclThemes::status();
                if (result.state == PclThemes::State::Done) {
                    Notices::add(Notices::Kind::Done, "PCL 主题已解锁，重启 PCL 后生效", 4000);
                } else if (result.state == PclThemes::State::Failed) {
                    Notices::add(Notices::Kind::Failed,
                                 "解锁失败：" + (result.error.empty() ? std::string("未知错误") : result.error), 5000);
                }
            }
#endif

            body.end();
        }
    }

    void draw(bool covered) {
        drawHome(covered);
    }
}

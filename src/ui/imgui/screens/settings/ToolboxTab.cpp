#include "ui/imgui/screens/settings/ToolboxTab.h"
#include "app/system/CustomDownload.h"
#include "app/system/PclThemes.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/base/strings.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace ToolboxTab {

    namespace blb = b_launcher_base;

    namespace {
        const char* const URL_OWNER = "settings#tools#url";
        constexpr float URL_FIELD_MAX = 360.0f;
        constexpr float BAR_H = 3.0f;

        std::string urlText;
        std::filesystem::path lastFolder;

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

        std::string liveUrl() {
            return Ui::trim(NewUi::textEditing == URL_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : urlText);
        }

        std::string amount(uint64_t bytes) {
            char text[32];
            if (bytes >= (1ull << 30)) {
                std::snprintf(text, sizeof(text), "%.2f GB", bytes / 1073741824.0);
            } else if (bytes >= (1ull << 20)) {
                std::snprintf(text, sizeof(text), "%.1f MB", bytes / 1048576.0);
            } else {
                std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
            }
            return text;
        }

        std::string fit(const std::string& text, float room) {
            return UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, std::max(0.0f, room));
        }

        void begin() {
            std::string url = liveUrl();
            if (NewUi::textEditing == URL_OWNER) {
                NewUi::cancelTextEdit();
            }
            urlText = url;
            if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
                Notices::add(Notices::Kind::Failed, "下载地址需要以 http:// 或 https:// 开头", 4000);
                return;
            }
            std::filesystem::path folder = lastFolder.empty() ? Platform::exeDirectory() : lastFolder;
            std::optional<std::filesystem::path> target = Platform::pickSaveFile(
                    folder / blb::path_from_utf8(CustomDownload::nameOf(url)), L"保存下载的文件");
            if (!target) {
                return;
            }
            lastFolder = target->parent_path();
            CustomDownload::start(url, *target);
        }

        float rightButton(const std::string& id, const std::string& text, int glyph, float rowY, bool blocked,
                          bool& pressed) {
            float w = std::max(72.0f, Page::buttonWidth(text, glyph));
            float x = Page::controlRight() - w;
            pressed = Page::button(id, text, x, rowY + Page::FIELD_Y, w, false, blocked, glyph);
            return x;
        }

        void addressRow(float rowY, bool blocked) {
            Page::label("下载地址", rowY);
            bool pressed = false;
            float buttonX = rightButton("settings#tools#download", "下载", 0xf019, rowY,
                                        blocked || CustomDownload::busy() || liveUrl().empty(), pressed);
            float labelRight =
                    Page::columnLeft() + 16.0f + UiFont::width("下载地址", UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float fieldW = std::min(URL_FIELD_MAX, buttonX - 8.0f - labelRight - 16.0f);
            float fieldX = buttonX - 8.0f - fieldW;
            TextFieldWidget field(urlValue());
            field.box(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .bounds(fieldX, rowY + Page::FIELD_Y, fieldW, Page::FIELD_H)
                    .block(blocked || CustomDownload::busy());
            field.paint();
            if (pressed) {
                begin();
            }
        }

        void statusRow(const CustomDownload::Status& status, float rowY, bool blocked) {
            float textX = Page::columnLeft() + 16.0f;
            float centreY = rowY + Page::ROW_H * 0.5f;
            std::string name = blb::path_to_utf8(status.target.filename());
            bool pressed = false;
            switch (status.state) {
                case CustomDownload::State::Probing:
                case CustomDownload::State::Running: {
                    float buttonX = rightButton("settings#tools#cancel", "取消", 0, rowY, blocked, pressed);
                    std::string detail = status.state == CustomDownload::State::Probing ? std::string("正在连接…")
                            : status.total > 0 ? amount(status.done) + " / " + amount(status.total)
                                               : amount(status.done);
                    if (status.state == CustomDownload::State::Running && status.speed > 0.0) {
                        detail += "，" + amount((uint64_t) status.speed) + "/s";
                    }
                    float detailW = UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    float detailX = buttonX - 14.0f - detailW;
                    UiRender::textY(detail, detailX, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    UiRender::textY(fit(name, detailX - 12.0f - textX), textX, rowY, Page::ROW_H,
                                    UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    float barX = textX;
                    float barW = Page::controlRight() - textX;
                    float barY = rowY + Page::ROW_H - 6.0f;
                    UiRender::roundRect(barX, barY, barW, BAR_H, BAR_H * 0.5f, UiTheme::outline());
                    if (status.total > 0) {
                        float fraction = std::min(1.0f, (float) ((double) status.done / (double) status.total));
                        if (fraction > 0.0f) {
                            UiRender::roundRect(barX, barY, std::max(BAR_H, barW * fraction), BAR_H, BAR_H * 0.5f,
                                                UiTheme::accent());
                        }
                    } else {
                        Page::spinner(detailX - 14.0f, centreY, 6.0f, UiTheme::accent());
                    }
                    if (pressed) {
                        CustomDownload::cancel();
                    }
                    break;
                }
                case CustomDownload::State::Done: {
                    float buttonX =
                            rightButton("settings#tools#reveal", "打开所在文件夹", 0xf07c, rowY, blocked, pressed);
                    std::string text = "已下载 " + name;
                    UiRender::textY(fit(text, buttonX - 12.0f - textX), textX, rowY, Page::ROW_H,
                                    UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    if (pressed) {
                        Platform::reveal(status.target);
                    }
                    break;
                }
                case CustomDownload::State::Failed: {
                    float buttonX = rightButton("settings#tools#retry", "重试", 0, rowY, blocked, pressed);
                    std::string text = "下载失败：" + status.error;
                    UiRender::textY(fit(text, buttonX - 12.0f - textX), textX, rowY, Page::ROW_H, UiTheme::danger(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    if (pressed) {
                        CustomDownload::start(status.url, status.target);
                    }
                    break;
                }
                default: break;
            }
        }
    }

    void draw(bool covered) {
        const CustomDownload::Status& status = CustomDownload::status();
        int downloadRows = status.state == CustomDownload::State::Idle ? 1 : 2;
        const PclThemes::Status& pcl = PclThemes::status();
        float contentH = 2.0f * Page::CAPTION_H + (downloadRows + 1) * Page::ROW_H + Page::GAP;
        Page::Scroll body("settings#tools", Page::TOP, MainWindow::height() - 14.0f, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered;
        float y = body.top - body.offset;

        Page::caption("自定义下载", y);
        y += Page::CAPTION_H;
        Page::card(y, downloadRows);
        addressRow(y, blocked);
        if (downloadRows > 1) {
            statusRow(status, y + Page::ROW_H, blocked);
        }
        y += downloadRows * Page::ROW_H + Page::GAP;

        Page::caption("PCL2", y);
        y += Page::CAPTION_H;
        Page::card(y, 1);
        float textX = Page::columnLeft() + 16.0f;
        UiRender::textY("解锁隐藏主题", textX, y, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
        bool pressed = false;
        float buttonX = rightButton("settings#tools#pcl", "一键解锁", 0xf09c, y, blocked, pressed);
        if (pcl.state == PclThemes::State::Failed) {
            float errW = UiFont::width(pcl.error, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float errX = buttonX - 12.0f - errW;
            UiRender::textY(fit(pcl.error, buttonX - 12.0f - textX - 180.0f), errX, y, Page::ROW_H, UiTheme::danger(),
                            UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        }
        if (pressed) {
            PclThemes::unlock();
            if (PclThemes::status().state == PclThemes::State::Done) {
                Notices::add(Notices::Kind::Done, "PCL 主题已解锁，重启 PCL 后生效", 4000);
            }
        }
        body.end();
    }
}

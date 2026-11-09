#include "ui/imgui/screens/downloads/JavaPage.h"
#include "app/instances/Covers.h"
#include "app/launch/JavaDownloads.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace JavaPage {

    namespace {
        constexpr float SELECT_W = 180.0f;
        constexpr float NAV_ITEM_H = 30.0f;
        using Page::FIELD_H;

        float left = 0.0f;
        int javaMajor = 21;

        float mainX() {
            return left;
        }

        float mainW() {
            return Page::right() - left;
        }

        std::shared_ptr<ModeBinding> majorChoice(const std::vector<JavaDownloads::Package>& packages) {
            std::vector<std::string> options = {"全部版本"};
            for (const JavaDownloads::Package& package : packages) {
                std::string label = "Java " + std::to_string(package.major);
                if (std::find(options.begin(), options.end(), label) == options.end()) {
                    options.push_back(label);
                }
            }
            return std::make_shared<ModeBinding>(
                    "downloads#java#major",
                    [] { return javaMajor == 0 ? std::string("全部版本") : "Java " + std::to_string(javaMajor); },
                    [](const std::string& value) {
                        javaMajor = value.rfind("Java ", 0) == 0 ? std::atoi(value.c_str() + 5) : 0;
                    },
                    [options] { return options; });
        }

        void vendorIcon(const JavaDownloads::Package& package, float cx, float cy) {
            constexpr float BOX = 20.0f;
            float pixel = UiRender::stableScale() * UiRender::pixelScale();
            std::shared_ptr<const Pixels> logo =
                    Covers::icon(JavaDownloads::icon(package), (int) std::lround(BOX * pixel), !UiTheme::isLight());
            if (logo == nullptr) {
                UiWidgets::icon(Ui::glyph(0xf0f4), cx, cy, UiTheme::textMuted(), 12.0f);
                return;
            }
            Textures::sharp(logo, cx - BOX * 0.5f, cy - BOX * 0.5f, BOX, BOX, 0.0f);
        }

        void list(bool covered) {
            const JavaDownloads::List& list = JavaDownloads::list();
            bool loading = list.status == JavaDownloads::Status::Loading;
            UiRender::textY("下载 Java", mainX() + 2.0f, Page::top(), NAV_ITEM_H, UiTheme::textPrimary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            float refreshX = Page::right() - 28.0f;
            if (Page::iconButton("downloads#java#reload", 0xf2f1, "刷新列表", refreshX, Page::top() + 1.0f,
                                 covered || loading)) {
                JavaDownloads::refresh();
            }
            SelectWidget major("downloads#java#major", majorChoice(list.packages));
            major.bounds(refreshX - 8.0f - SELECT_W, Page::top() + 4.0f, SELECT_W, FIELD_H).block(covered || loading);
            major.paint();

            float top = Page::top() + 40.0f;
            float bottom = MainWindow::height() - 14.0f;
            std::vector<const JavaDownloads::Package*> shown;
            for (const JavaDownloads::Package& package : list.packages) {
                if (javaMajor == 0 || package.major == javaMajor) {
                    shown.push_back(&package);
                }
            }
            if (shown.empty()) {
                // Without nix-ld on NixOS the list is blocked, not failed, so show only the reason.
                std::string text = loading                             ? "正在获取 Java 列表…"
                        : !JavaDownloads::available()                  ? list.error
                        : list.status == JavaDownloads::Status::Failed ? "获取 Java 列表失败：" + list.error
                        : list.packages.empty()                        ? "列表是空的"
                                                                       : "没有这个版本的 Java";
                text = UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, mainW() - 40.0f);
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float lead = loading ? 24.0f : 0.0f;
                float x = mainX() + (mainW() - w - lead) * 0.5f;
                if (loading) {
                    Page::spinner(x + 8.0f, top + 82.0f, 8.0f, UiTheme::accent());
                }
                UiRender::textY(text, x + lead, top + 70.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return;
            }
            int rows = (int) shown.size();
            Page::Scroll body("downloads#java#list", top, bottom, rows * Page::ROW_H);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y0 = body.top - body.offset;
            Page::card(y0, rows);
            for (int i = 0; i < rows; i++) {
                float rowY = y0 + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const JavaDownloads::Package& package = *shown[i];
                std::string id = "downloads#java#get#" + package.folder;
                bool hovered = !blocked && NewUi::hit(mainX(), rowY, mainW(), Page::ROW_H);
                Page::rowHover(id, rowY, hovered, Page::rowCorners(i, rows));
                float cy = rowY + Page::ROW_H * 0.5f;
                vendorIcon(package, mainX() + 24.0f, cy);

                JavaDownloads::State state = JavaDownloads::state(package.folder);
                bool done = state == JavaDownloads::State::Done || JavaDownloads::installed(package);
                float right = Page::controlRight();
                float actionX = right;
                if (state == JavaDownloads::State::Running) {
                    Page::progressRing(right - 10.0f, cy, JavaDownloads::progress(package.folder));
                    actionX = right - 24.0f;
                } else if (done) {
                    std::string text = "已安装";
                    float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    UiRender::textY(text, right - w, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    UiWidgets::check(right - w - 12.0f, cy, UiTheme::accent(), 9.0f);
                    actionX = right - w - 22.0f;
                } else {
                    std::string text = "安装";
                    float w = Page::buttonWidth(text, 0xf019);
                    if (Page::button(id + "#install", text, right - w, rowY + Page::FIELD_Y, w, false, blocked,
                                     0xf019)) {
                        JavaDownloads::install(package);
                    }
                    actionX = right - w;
                }
                std::string size = package.archiveSize > 0
                        ? std::to_string((package.archiveSize + 500000) / 1000000) + " MB"
                        : std::string();
                float sizeW = size.empty() ? 0.0f : UiFont::width(size, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (!size.empty()) {
                    UiRender::textY(size, actionX - 12.0f - sizeW, rowY, Page::ROW_H, UiTheme::textMuted(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                }
                float x = mainX() + 44.0f;
                float tags = UiFont::width(package.version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 16.0f +
                        (package.preview ? UiFont::width("预览", UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 16.0f : 0.0f);
                float room = std::max(40.0f, actionX - 24.0f - sizeW - x - tags);
                std::string name =
                        UiFont::fitEnd(JavaDownloads::name(package), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room);
                UiRender::textY(name, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                x += UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                x += Page::tag(package.version, x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                if (package.preview) {
                    Page::tag("预览", x, rowY, UiTheme::accentSoft(40), UiTheme::accentText());
                }
            }
            body.end();
        }

    }

    void draw(float x, bool covered) {
        left = x;
        Page::Column column(x);
        list(covered);
    }
}

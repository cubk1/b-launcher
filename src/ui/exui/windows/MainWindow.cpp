#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/MainWindow.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"
#include "ui/exui/Skins.h"
#include "ui/exui/widgets/Button.h"
#include "ui/exui/widgets/Label.h"

namespace ForgottenMelody {

    namespace {
        constexpr uint32_t NOTICE = 1;
        constexpr uint32_t LEAVE = 2;

        // One notice at a time, cleared once its duration is over.
        class NoticeLine : public exui::Label {
        public:
            using Label::Label;

            void show(const std::wstring& text, Gdiplus::ARGB color, long long durationMs) {
                StopTimer();
                SetTextColor(color);
                SetCaption(text);
                if (durationMs > 0) {
                    StartTimer((UINT) durationMs);
                }
            }

            void OnTimer() override {
                StopTimer();
                SetCaption(std::wstring());
            }
        };

        exui::Label& centered(exui::Label& label, float size, int style) {
            label.SetFont(L"微软雅黑", size, style);
            label.SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
            label.SetAutoGlowColor();
            return label;
        }
    }

    bool MainWindow::create() {
        if (!CreateHostWindow(nullptr, L"B Launcher", 720, 450, exui::SkinBytes(exui::Skin::MainBackground), 0xFFFFFF,
                              7, exui::CaptionButtonMask::Minimize | exui::CaptionButtonMask::Close, false)) {
            return false;
        }
        SetWindowAnimationTypes(exui::WindowAnimationKind::ZoomFade, exui::WindowAnimationKind::ZoomFade, false);
        centered(Add<exui::Label>(0, 165, 720, 50, L"忘却的旋律"), 30, Gdiplus::FontStyleBold);
        centered(Add<exui::Label>(0, 215, 720, 30, L"开发中"), 16, Gdiplus::FontStyleRegular);
        auto notice = std::make_shared<NoticeLine>(20, 412, 680, 24, std::wstring());
        notice->SetFont(L"微软雅黑", 14);
        notice->SetAutoGlowColor();
        AddControl(NOTICE, notice);
        auto leave = std::make_shared<exui::Button>(290, 262, 140, 34, L"回到默认界面");
        leave->SetFont(L"微软雅黑", 14);
        AddControl(LEAVE, leave);
        return true;
    }

    void MainWindow::OnAction(uint32_t id) {
        if (id != LEAVE) {
            return;
        }
        if (Services::busy()) {
            showNotice(Notices::Kind::Failed, L"有游戏、安装或下载正在进行，完成后再切换", 4000);
            return;
        }
        UiStyles::set("default");
        if (!Platform::restart()) {
            showNotice(Notices::Kind::Failed, L"重启失败，请手动重新打开启动器", 5000);
        }
    }

    void MainWindow::showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs) {
        Control<NoticeLine>(NOTICE).show(text, kind == Notices::Kind::Failed ? 0xFFC02020 : 0xD2000000, durationMs);
    }
}

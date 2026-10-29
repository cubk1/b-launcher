#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/MainWindow.h"
#include "ui/exui/Skins.h"
#include "ui/exui/widgets/Label.h"

namespace ForgottenMelody {

    namespace {
        constexpr uint32_t NOTICE = 1;

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
        return true;
    }

    void MainWindow::showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs) {
        Control<NoticeLine>(NOTICE).show(text, kind == Notices::Kind::Failed ? 0xFFC02020 : 0xD2000000, durationMs);
    }
}

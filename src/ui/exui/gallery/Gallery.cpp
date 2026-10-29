// EXUI gallery: renders every widget and window state to PNG files.
//
//   exui_gallery <outDir>                    write the PNGs
//   exui_gallery <outDir> --compare <dir>    also compare with a baseline;
//                                            writes diff-*.png and exits 1 on changes
//
// Used as a visual regression check while refactoring: render a baseline
// first, then compare after every change.
#include "ui/exui/Resources.h"
#include "ui/exui/MsgBox.h"
#include "ui/exui/Skins.h"
#include "ui/exui/Window.h"
#include "ui/exui/widgets/Button.h"
#include "ui/exui/widgets/CheckRadio.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/GroupBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/CaptionButton.h"
#include "ui/exui/widgets/Menu.h"
#include "ui/exui/widgets/ProgressBar.h"
#include "ui/exui/widgets/ScrollBar.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <vector>

using namespace exui;
using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {

    CLSID PngEncoder() {
        UINT count = 0, size = 0;
        GetImageEncodersSize(&count, &size);
        std::vector<BYTE> bytes(size);
        auto* encoders = reinterpret_cast<ImageCodecInfo*>(bytes.data());
        GetImageEncoders(count, size, encoders);
        for (UINT i = 0; i < count; ++i)
            if (std::wstring_view(encoders[i].MimeType) == L"image/png") return encoders[i].Clsid;
        return {};
    }

    std::map<std::wstring, std::unique_ptr<Bitmap>> scenes;

    void Keep(const std::wstring& name, std::unique_ptr<Bitmap> image) {
        if (image)
            scenes[name] = std::move(image);
        else
            std::wcerr << L"failed to render " << name << L"\n";
    }

    void Caption(Surface& sheet, float x, float y, std::wstring_view text, float size = 12) {
        Graphics g(sheet.GetBitmap());
        DrawGlowText(g, text, DefaultFontFamily(), size, size > 12 ? FontStyleBold : FontStyleRegular,
                     RectF(x, y, 400, size + 12), 0xFF303030, 0);
    }

    // Basic widgets in five states: normal, hover, pressed, disabled, selected.
    void BasicWidgets() {
        Surface sheet;
        sheet.Resize(990, 720);
        {
            Graphics g(sheet.GetBitmap());
            g.Clear(Color(0xFFF1F3F5));
        }
        Caption(sheet, 20, 8, L"基础控件", 18);
        const wchar_t* states[]{L"正常 / 0%", L"悬停 / 5%", L"按下 / 50%", L"禁用", L"选中 / 100%"};
        for (int col = 0; col < 5; ++col)
            Caption(sheet, float(150 + col * 164), 43, states[col]);
        const wchar_t* names[]{L"Button",      L"ImageButton", L"Label",       L"Hyperlink",
                               L"RadioButton", L"CheckBox",    L"ProgressBar", L"GroupBox"};
        const auto image = SkinBytes(Skin::ComboButtonHover);
        for (int row = 0; row < 8; ++row) {
            const int y = 80 + row * 77;
            Caption(sheet, 12, float(y + 8), names[row]);
            for (int col = 0; col < 5; ++col) {
                const int x = 150 + col * 164;
                std::unique_ptr<Widget> w;
                switch (row) {
                    case 0: w = std::make_unique<Button>(x, y, 145, 35, L"启动游戏"); break;
                    case 1: {
                        auto b = std::make_unique<ImageButton>(x, y, 145, 35, L"整合包");
                        b->SetNormalImageBytes(image);
                        b->SetSelected(col == 4);
                        w = std::move(b);
                        break;
                    }
                    case 2: w = std::make_unique<Label>(x, y, 145, 35, L"标签文本"); break;
                    case 3: w = std::make_unique<Hyperlink>(x, y, 145, 35, L"官网链接"); break;
                    case 4: {
                        auto b = std::make_unique<RadioButton>(x, y, 145, 30, L"单选项目");
                        b->SetChecked(col == 4);
                        w = std::move(b);
                        break;
                    }
                    case 5: {
                        auto b = std::make_unique<CheckBox>(x, y, 145, 30, L"复选项目");
                        b->SetChecked(col == 4);
                        w = std::move(b);
                        break;
                    }
                    case 6: {
                        auto b = std::make_unique<ProgressBar>(x, y + 6, 145, 22);
                        const int values[]{0, 5, 50, 50, 100};
                        b->SetPosition(values[col]);
                        w = std::move(b);
                        break;
                    }
                    default: w = std::make_unique<GroupBox>(x, y, 145, 58, L"分组框"); break;
                }
                if (col == 1) w->OnMouseEnter();
                if (col == 2) {
                    w->OnMouseEnter();
                    w->OnMouseDown(5, 5, VK_LBUTTON);
                }
                if (col == 3) w->SetEnabled(false);
                w->CompositeTo(sheet.GetDC());
            }
        }
        GdiFlush();
        Keep(L"widgets", CopyBitmap(*sheet.GetBitmap()));
    }

    // Edit boxes, combo box with its list, menu, scrollbars and the list view.
    void CompositeWidgets() {
        Surface sheet;
        sheet.Resize(980, 660);
        {
            Graphics g(sheet.GetBitmap());
            g.Clear(Color(0xFFE8EEF2));
        }
        Caption(sheet, 24, 12, L"复合控件", 18);
        for (int i = 0; i < 4; ++i) {
            Caption(sheet, float(24 + i * 235), 54, L"编辑框 / 样式 " + std::to_wstring(i));
            EditBox edit(24 + i * 235, 80, 214, 28, L"忘却的旋律 Minecraft");
            edit.SetBorderStyle(i);
            edit.CompositeTo(sheet.GetDC());
        }
        Caption(sheet, 24, 126, L"选中文字");
        Window focusHost; // the selection shows only while focused, and focus belongs to a window
        focusHost.CreateHostWindow(nullptr, L"", 300, 200, {}, 0xFFFFFF, 4, 0, false);
        auto& selection = focusHost.Add<EditBox>(24, 154, 214, 28, L"选择这段文字");
        selection.SetSelection(0, 4);
        focusHost.SetFocusedWidget(&selection, true);
        selection.CompositeTo(sheet.GetDC());
        focusHost.CloseWindow(false);
        Caption(sheet, 259, 126, L"密码框");
        EditBox password(259, 154, 214, 28, L"password", true);
        password.CompositeTo(sheet.GetDC());
        Caption(sheet, 494, 126, L"禁用");
        EditBox disabled(494, 154, 214, 28, L"不可编辑");
        disabled.SetEnabled(false);
        disabled.CompositeTo(sheet.GetDC());
        Caption(sheet, 729, 126, L"多行");
        EditBox multiline(729, 154, 214, 75);
        multiline.SetMultiline(true);
        multiline.SetText(
                L"第一行输入较长的文本，显示横向滚动条\r\n第二行中文\r\n第三行\r\n第四行\r\n第五行\r\n第六行");
        multiline.CompositeTo(sheet.GetDC());

        Caption(sheet, 24, 202, L"组合框 / 展开列表");
        ComboBox combo(24, 230, 214, 28);
        for (int i = 0; i < 12; ++i)
            combo.AddItem(L"Minecraft 1." + std::to_wstring(i + 5));
        combo.SetSelectedIndex(2);
        combo.OnMouseEnter();
        combo.CompositeTo(sheet.GetDC());
        {
            Graphics g(sheet.GetBitmap());
            g.TranslateTransform(24.f, 258.f);
            combo.PaintDropdown(g);
        }
        Caption(sheet, 259, 202, L"右键菜单");
        Menu menu;
        menu.SetItems({{1, L"剪切(&T)"},
                       {2, L"复制(&C)"},
                       {3, L"粘贴(&P)", false},
                       {0, L"", false, false, true},
                       {5, L"全部选择(&A)", true, true}});
        {
            Graphics g(sheet.GetBitmap());
            g.TranslateTransform(259.f, 230.f);
            menu.Paint(g, menu.Measure().Width, 1);
        }
        Caption(sheet, 494, 250, L"滚动条 / 正常、悬停、按下");
        for (int i = 0; i < 3; ++i) {
            ScrollBar scroll(510 + i * 50, 286, 10, 100);
            scroll.SetRange(0, 180, 30);
            scroll.SetPosition(30);
            if (i) scroll.OnMouseEnter();
            if (i == 2) scroll.OnMouseDown(4, int(scroll.GetGeometry().thumb.Y) + 5, VK_LBUTTON);
            scroll.CompositeTo(sheet.GetDC());
        }
        Caption(sheet, 24, 408, L"列表框 / 表头、选中、悬停、双向滚动");
        ListView list(24, 438, 680, 196);
        list.SetHeaderHeight(25);
        list.SetRowHeight(22);
        list.InsertColumn(L"名称", 110);
        list.InsertColumn(L"状态", 100);
        list.InsertColumn(L"说明", 180);
        list.InsertColumn(L"扩展列（横向滚动）", 420);
        for (int i = 0; i < 25; ++i)
            list.InsertRow(
                    {L"项目 " + std::to_wstring(i), L"已就绪", L"中文和长文本省略号测试 abcdefghijklmn", L"更多"});
        list.SetSelectedRow(3);
        list.SetRowColors(0x330060C0, 0x660060C0, 0, 0xD2FFFFFF);
        list.OnMouseMove(200, 28, 0);
        list.CompositeTo(sheet.GetDC());
        GdiFlush();
        Keep(L"composite", CopyBitmap(*sheet.GetBitmap()));
    }

    // A hidden host window rendered offscreen.
    std::unique_ptr<Bitmap> RenderHost(Window& window) {
        return window.RenderToBitmap();
    }

    void Windows() {
        struct Spec {
            const wchar_t* name;
            Skin skin;
            SkinBackgroundMode mode;
            int alpha, corner;
            uint32_t mask;
            bool hoverClose;
            float scale = 1;
        };
        const uint32_t full = CaptionButtonMask::Icon | CaptionButtonMask::Title | CaptionButtonMask::Close |
                CaptionButtonMask::Maximize | CaptionButtonMask::Minimize;
        const Spec specs[]{
                {L"window-main", Skin::MainBackground, SkinBackgroundMode::Stretch, 255, 4, full, false},
                {L"window-main-hover", Skin::MainBackground, SkinBackgroundMode::Stretch, 255, 4, full, true},
                {L"window-dialog-fade", Skin::DialogBackground, SkinBackgroundMode::EdgeFade, 150, 4,
                 CaptionButtonMask::DefaultLauncherMask, false},
                {L"window-translucent", Skin::DialogBackground, SkinBackgroundMode::EdgeFade, 80, 0,
                 CaptionButtonMask::Title | CaptionButtonMask::Close, false},
                {L"window-main-150", Skin::MainBackground, SkinBackgroundMode::Stretch, 255, 4, full, true, 1.5f},
        };
        for (const auto& spec : specs) {
            Window window;
            window.SetScale(spec.scale);
            window.CreateHostWindow(nullptr, L"EXUI 窗口", 420, 260, SkinBytes(spec.skin), 0xEDD8C7, spec.corner,
                                    spec.mask, false);
            window.SetBackgroundMode(spec.mode);
            window.SetWindowAlpha(spec.alpha);
            auto button = std::make_shared<Button>(250, 190, 140, 40, L"启动游戏");
            button->SetFont(L"微软雅黑", 16, FontStyleBold);
            window.Add(button);
            window.Add(std::make_shared<Label>(30, 60, 200, 22, L"自动反差色标签"));
            auto edit = std::make_shared<EditBox>(30, 100, 200, 26, L"Player");
            window.Add(edit);
            auto combo = std::make_shared<ComboBox>(30, 140, 200, 28);
            combo->AddItem(L"1.7.10");
            combo->SetSelectedIndex(0);
            window.Add(combo);
            if (spec.hoverClose && !window.GetCaptionButtons().empty()) {
                const RECT r = window.GetCaptionButtons().front()->GetBounds();
                SendMessageW(window.GetHwnd(), WM_MOUSEMOVE, 0,
                             MAKELPARAM(window.ToPhysical((r.left + r.right) / 2.f),
                                        window.ToPhysical((r.top + r.bottom) / 2.f)));
            }
            Keep(spec.name, RenderHost(window));
            window.CloseWindow(false);
        }
    }

    // Closing animations, every other frame of two effects.
    void Animations() {
        for (const auto kind : {WindowAnimationKind::ZoomFade, WindowAnimationKind::RevealLeft}) {
            Window window;
            window.CreateHostWindow(nullptr, L"动画", 240, 150, SkinBytes(Skin::DialogBackground), 0xFFFFFF, 4,
                                    CaptionButtonMask::DefaultLauncherMask, false);
            window.SetBackgroundMode(SkinBackgroundMode::Stretch);
            uint64_t now = 1000;
            window.SetAnimationClock([&now] { return now; });
            window.SetWindowAnimationTypes(kind, kind);
            Surface strip;
            strip.Resize(240 * 4, 150);
            {
                Graphics g(strip.GetBitmap());
                g.Clear(Color(0xFF808080));
            }
            window.CloseWindow(true);
            for (int frame = 0; frame < 8 && window.IsWindowAnimating(); ++frame) {
                if (frame % 2 == 0 && frame / 2 < 4) {
                    auto image = window.RenderPresentationToBitmap();
                    Graphics g(strip.GetBitmap());
                    g.DrawImage(image.get(), frame / 2 * 240, 0);
                }
                now += 50;
                window.AdvanceAnimations();
            }
            window.CloseWindow(false);
            GdiFlush();
            Keep(kind == WindowAnimationKind::ZoomFade ? L"animation-zoom" : L"animation-reveal",
                 CopyBitmap(*strip.GetBitmap()));
        }
    }

    // Message boxes: captured from inside their modal loop, then closed.
    std::unique_ptr<Bitmap> capturedBox;
    void CALLBACK CaptureBox(HWND, UINT, UINT_PTR timer, DWORD) {
        const HWND box = FindWindowW(L"Ex_DirectUI", L"EXUI 消息框");
        auto* window = Window::FromHandle(box);
        if (!window || window->IsWindowAnimating()) return;
        KillTimer(nullptr, timer);
        capturedBox = window->RenderToBitmap();
        PostMessageW(box, WM_CLOSE, 0, 0);
    }

    void MessageBoxes() {
        struct Spec {
            const wchar_t* name;
            const wchar_t* text;
            MsgBoxIcon icon;
            MsgBoxButtons buttons;
            std::vector<std::wstring> labels;
        };
        const Spec specs[]{
                {L"msgbox-info", L"已启动 Minecraft 1.7.10", MsgBoxIcon::Info, MsgBoxButtons::Ok, {}},
                {L"msgbox-question",
                 L"发现新版本，是否立即更新？\r\n更新完成后需要重新启动。",
                 MsgBoxIcon::Question,
                 MsgBoxButtons::OkCancel,
                 {L"稍后", L"立即更新"}},
                {L"msgbox-plain",
                 L"没有选择游戏，请先在下拉列表中选择一个版本。",
                 MsgBoxIcon::None,
                 MsgBoxButtons::Ok,
                 {}},
        };
        for (const auto& spec : specs) {
            capturedBox.reset();
            const UINT_PTR timer = SetTimer(nullptr, 0, 20, CaptureBox);
            MsgBox::Show(nullptr, spec.text, L"EXUI 消息框", spec.icon, spec.buttons, spec.labels, false, std::nullopt,
                         false);
            KillTimer(nullptr, timer);
            Keep(spec.name, std::move(capturedBox));
        }
    }

    // Pixel comparison; the diff image marks changed pixels in red.
    int Compare(const fs::path& out, const fs::path& baseline) {
        int changedScenes = 0;
        const auto encoder = PngEncoder();
        for (const auto& [name, image] : scenes) {
            const auto file = baseline / (name + L".png");
            Bitmap expected(file.c_str());
            if (expected.GetLastStatus() != Ok) {
                std::wcout << name << L": no baseline\n";
                continue;
            }
            const UINT w = image->GetWidth(), h = image->GetHeight();
            if (expected.GetWidth() != w || expected.GetHeight() != h) {
                std::wcout << name << L": size changed\n";
                ++changedScenes;
                continue;
            }
            Rect all(0, 0, int(w), int(h));
            BitmapData a{}, b{};
            image->LockBits(&all, ImageLockModeRead, PixelFormat32bppARGB, &a);
            expected.LockBits(&all, ImageLockModeRead, PixelFormat32bppARGB, &b);
            Bitmap diff(int(w), int(h), PixelFormat32bppARGB);
            long changed = 0;
            int maxDelta = 0; // largest change of one channel
            RECT box{LONG(w), LONG(h), -1, -1};
            for (UINT y = 0; y < h; ++y) {
                auto* rowA = reinterpret_cast<const uint32_t*>(static_cast<BYTE*>(a.Scan0) + y * a.Stride);
                auto* rowB = reinterpret_cast<const uint32_t*>(static_cast<BYTE*>(b.Scan0) + y * b.Stride);
                for (UINT x = 0; x < w; ++x) {
                    const bool same = rowA[x] == rowB[x];
                    diff.SetPixel(int(x), int(y), Color(same ? (rowA[x] & 0x00FFFFFF) | 0x30000000 : 0xFFFF0000));
                    if (same) continue;
                    ++changed;
                    for (int shift = 0; shift < 32; shift += 8)
                        maxDelta = std::max(maxDelta,
                                            std::abs(int((rowA[x] >> shift) & 255) - int((rowB[x] >> shift) & 255)));
                    box = {std::min(box.left, LONG(x)), std::min(box.top, LONG(y)), std::max(box.right, LONG(x)),
                           std::max(box.bottom, LONG(y))};
                }
            }
            image->UnlockBits(&a);
            expected.UnlockBits(&b);
            if (!changed) continue;
            ++changedScenes;
            std::wcout << name << L": " << changed << L" pixels changed in (" << box.left << L"," << box.top << L")-("
                       << box.right << L"," << box.bottom << L"), max channel delta " << maxDelta << L"\n";
            diff.Save((out / (L"diff-" + name + L".png")).c_str(), &encoder);
        }
        std::wcout << (changedScenes ? L"changes found\n" : L"no visual changes\n");
        return changedScenes ? 1 : 0;
    }

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcerr << L"usage: exui_gallery <outDir> [--compare <baselineDir>]\n";
        return 2;
    }
    const fs::path out = argv[1];
    fs::create_directories(out);
    if (!Initialize()) return 2;
    BasicWidgets();
    CompositeWidgets();
    Windows();
    Animations();
    MessageBoxes();
    const auto encoder = PngEncoder();
    for (const auto& [name, image] : scenes)
        image->Save((out / (name + L".png")).c_str(), &encoder);
    int result = 0;
    if (argc >= 4 && std::wstring_view(argv[2]) == L"--compare") result = Compare(out, argv[3]);
    scenes.clear();
    Shutdown();
    return result;
}

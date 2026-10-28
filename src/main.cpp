#include "app/accounts/Accounts.h"
#include "app/shell/Appearance.h"
#include "app/shell/Background.h"
#include "app/shell/BorderlessWindow.h"
#include "app/system/Catalog.h"
#include "app/system/Config.h"
#include "app/shell/Drop.h"
#include "app/instances/Installer.h"
#include "app/instances/InstanceExport.h"
#include "app/instances/Mods.h"
#include "app/launch/GameLog.h"
#include "app/home/Homepage.h"
#include "app/launch/LogWindow.h"
#include "app/launch/Game.h"
#include "app/instances/Instances.h"
#include "app/launch/JavaDownloads.h"
#include "app/shell/Logo.h"
#include "app/shell/MainWindow.h"
#include "app/downloads/Addons.h"
#include "app/system/Platform.h"
#include "app/downloads/ProjectInstaller.h"
#include "app/system/Updates.h"

#include "ui/gl/UiGL.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiTheme.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

    HICON iconOf(int size) {
        std::vector<unsigned char> rgba = Logo::pixels(size);
        if (rgba.empty()) {
            return nullptr;
        }
        BITMAPV5HEADER header = {};
        header.bV5Size = sizeof(header);
        header.bV5Width = size;
        header.bV5Height = -size;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000u;
        header.bV5GreenMask = 0x0000FF00u;
        header.bV5BlueMask = 0x000000FFu;
        header.bV5AlphaMask = 0xFF000000u;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(screen, (BITMAPINFO*) &header, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (color == nullptr || bits == nullptr) {
            return nullptr;
        }
        auto* out = (unsigned char*) bits;
        for (size_t i = 0; i < rgba.size(); i += 4) {
            out[i] = rgba[i + 2];
            out[i + 1] = rgba[i + 1];
            out[i + 2] = rgba[i];
            out[i + 3] = rgba[i + 3];
        }
        std::vector<unsigned char> zeros((size_t) ((size + 15) / 16) * 2 * size, 0);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
        ICONINFO info = {TRUE, 0, 0, mask, color};
        HICON icon = CreateIconIndirect(&info);
        DeleteObject(color);
        DeleteObject(mask);
        return icon;
    }

    struct WindowIcons {
        uint32_t accent = 0;
        HICON big = nullptr;
        HICON compact = nullptr;

        void follow(BorderlessWindow& window) {
            uint32_t current = UiTheme::accent() | 0xFF000000u;
            if (current == accent && big != nullptr) {
                return;
            }
            HICON nextBig = iconOf(GetSystemMetrics(SM_CXICON));
            HICON nextSmall = iconOf(GetSystemMetrics(SM_CXSMICON));
            if (nextBig == nullptr || nextSmall == nullptr) {
                if (nextBig != nullptr) {
                    DestroyIcon(nextBig);
                }
                if (nextSmall != nullptr) {
                    DestroyIcon(nextSmall);
                }
                accent = current;
                return;
            }
            window.setIcons(nextBig, nextSmall);
            LogWindow::setIcons(nextBig, nextSmall);
            release();
            big = nextBig;
            compact = nextSmall;
            accent = current;
        }

        void release() {
            if (big != nullptr) {
                DestroyIcon(big);
                big = nullptr;
            }
            if (compact != nullptr) {
                DestroyIcon(compact);
                compact = nullptr;
            }
        }
    };

    constexpr double BACKGROUND_FRAME_MS = 1000.0 / 30.0;

    bool foreground() {
        HWND active = GetForegroundWindow();
        DWORD process = 0;
        GetWindowThreadProcessId(active, &process);
        return active != nullptr && process == GetCurrentProcessId();
    }

    void idleUntil(std::chrono::steady_clock::time_point started) {
        double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        double wait = BACKGROUND_FRAME_MS - spent;
        if (wait >= 1.0) {
            MsgWaitForMultipleObjectsEx(0, nullptr, (DWORD) wait, QS_INPUT | QS_POSTMESSAGE | QS_SENDMESSAGE,
                                        MWMO_INPUTAVAILABLE);
        }
    }

    int pixels(float layout, float scale) {
        return (int) (layout * scale + 0.5f);
    }

    float storedSize(const char* key, float fallback) {
        const nlohmann::json& doc = Config::data();
        if (auto it = doc.find(key); it != doc.end() && it->is_number()) {
            return std::max(fallback, it->get<float>());
        }
        return fallback;
    }

    bool storeWindow(const BorderlessWindow& window, float scale) {
        WINDOWPLACEMENT placement = {sizeof(placement)};
        if (!GetWindowPlacement(window.hwnd, &placement)) {
            return false;
        }
        const RECT& normal = placement.rcNormalPosition;
        float w = std::round(std::max(MainWindow::MIN_WIDTH, (normal.right - normal.left) / scale));
        float h = std::round(std::max(MainWindow::MIN_HEIGHT, (normal.bottom - normal.top) / scale));
        bool maximized =
                IsZoomed(window.hwnd) || (IsIconic(window.hwnd) && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
        nlohmann::json& doc = Config::data();
        if (doc.value("windowWidth", 0.0f) == w && doc.value("windowHeight", 0.0f) == h &&
            doc.value("windowMaximized", false) == maximized) {
            return false;
        }
        doc["windowWidth"] = w;
        doc["windowHeight"] = h;
        doc["windowMaximized"] = maximized;
        return true;
    }
}

int main() {
    SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    float dpiScale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);

    float scale = dpiScale * Appearance::zoom() / 100.0f;
    BorderlessWindow window;
    window.resizable = true;
    window.isDragArea = [&scale](int x, int y) { return MainWindow::isDragArea(x / scale, y / scale); };
    bool remember = Appearance::rememberWindow();
    float startW = remember ? storedSize("windowWidth", MainWindow::MIN_WIDTH) : MainWindow::MIN_WIDTH;
    float startH = remember ? storedSize("windowHeight", MainWindow::MIN_HEIGHT) : MainWindow::MIN_HEIGHT;
    bool startMaximized = remember && Config::data().value("windowMaximized", false);
    std::string shownTitle = Appearance::windowTitle();
    LogWindow::setTitle(Platform::widen(shownTitle));
    if (!window.create(Platform::widen(shownTitle), pixels(startW, scale), pixels(startH, scale))) {
        return 1;
    }
    window.setVsync(true);
    window.setMinimumSize(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
    Platform::window = window.hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_InitForOpenGL(window.hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");
    UiGL::load();
    UiFont::init();
    Appearance::load();
    Instances::init();
    Accounts::init();
    window.imguiContext = ImGui::GetCurrentContext();
    LogWindow::init(&window);
    Drop::attach(window.hwnd);
    Catalog::preload();
    Addons::preload();
    Updates::check(true, false);
    bool shown = false;
    bool windowLight = false;
    WindowIcons icons;

    auto frame = [&]() {
        float next = dpiScale * Appearance::zoom() / 100.0f;
        if (next != scale) {
            float layoutW = window.width / scale;
            float layoutH = window.height / scale;
            scale = next;
            window.setMinimumSize(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
            if (!window.maximized()) {
                window.resize(pixels(std::max(MainWindow::MIN_WIDTH, layoutW), scale),
                              pixels(std::max(MainWindow::MIN_HEIGHT, layoutH), scale));
            }
        }
        MainWindow::setSize(window.width / scale, window.height / scale, window.maximized());

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Instances::poll();
        Accounts::poll();
        Game::poll();
        Catalog::poll();
        Addons::poll();
        Background::poll();
        ProjectInstaller::poll();
        Installer::poll();
        Mods::poll();
        InstanceExport::poll();
        Drop::poll();
        Homepage::poll();
        Updates::poll();
        JavaDownloads::poll();

        UiRender::uiScale = scale / (float) UiRender::guiScale();
        UiRender::begin(ImGui::GetForegroundDrawList());
        NewUi::render(MainWindow::draw);
        UiRender::end();

        ImGui::Render();
        glViewport(0, 0, window.width, window.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window.swap();
        if (!shown) {
            shown = true;
            window.show(startMaximized);
        }
        LogWindow::frame(scale);

        icons.follow(window);
        if (std::string title = Appearance::windowTitle(); title != shownTitle) {
            shownTitle = title;
            window.setTitle(Platform::widen(title));
            LogWindow::setTitle(Platform::widen(title));
        }
        if (UiTheme::isLight() != windowLight) {
            windowLight = UiTheme::isLight();
            window.setDarkMode(!windowLight);
        }

        if (MainWindow::minimizeRequested) {
            MainWindow::minimizeRequested = false;
            window.minimize();
        }
        if (MainWindow::maximizeRequested) {
            MainWindow::maximizeRequested = false;
            window.toggleMaximize();
        }
        if (MainWindow::closeRequested) {
            MainWindow::closeRequested = false;
            window.close();
        }
        if (Appearance::rememberWindow() && !window.sizing() && storeWindow(window, scale)) {
            Config::save();
        }
    };
    window.onModalFrame = frame;
    window.onClose = [&]() {
        if (Appearance::rememberWindow()) {
            storeWindow(window, scale);
        }
        DestroyWindow(window.hwnd);
    };

    while (window.pumpMessages()) {
        GameLog::poll();
        if (window.minimized() || (shown && !IsWindowVisible(window.hwnd))) {
            LogWindow::frame(scale);
            Sleep(LogWindow::isOpen() ? 15 : 10);
            continue;
        }
        auto started = std::chrono::steady_clock::now();
        frame();
        if (!foreground()) {
            idleUntil(started);
        }
    }

    window.onModalFrame = nullptr;
    MainWindow::save();
    Config::save();

    LogWindow::shutdown();
    icons.release();
    Homepage::shutdown();
    Updates::shutdown();
    JavaDownloads::shutdown();
    Drop::shutdown();
    InstanceExport::shutdown();
    Mods::shutdown();
    Installer::shutdown();
    Background::shutdown();
    ProjectInstaller::shutdown();
    Addons::shutdown();
    Catalog::shutdown();
    Game::shutdown();
    Accounts::shutdown();
    Instances::shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    window.destroy();
    return 0;
}

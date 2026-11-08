#include "ui/imgui/screens/shell/ImGuiApp.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "ui/imgui/screens/shell/Compact.h"
#include "ui/imgui/screens/shell/SdlWindow.h"
#include "app/system/Config.h"
#include "ui/imgui/screens/shell/Drop.h"
#include "app/instances/Drops.h"
#include "app/launch/GameLog.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "ui/imgui/screens/launch/LogWindow.h"
#include "app/launch/Game.h"
#include "app/launch/ShortcutLaunch.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/system/Notices.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <core/base/json.hpp>

#include <SDL3/SDL.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

    constexpr int ICON_SIZE = 64;
    constexpr float CORNER_RADIUS = 8.0f;
    constexpr double BACKGROUND_FRAME_MS = 1000.0 / 30.0;
    constexpr double IDLE_FRAME_MS = 100.0;
    constexpr double ROOM_CLOSE_MS = 10000.0;
    constexpr double INPUT_GRACE_MS = 600.0;

    double sinceMs(std::chrono::steady_clock::time_point started) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void idleUntil(std::chrono::steady_clock::time_point started, double frameMs) {
        double wait = frameMs - sinceMs(started);
        if (wait >= 1.0) {
            SDL_WaitEventTimeout(nullptr, (Sint32) wait);
        }
    }

    void sleepUntil(std::chrono::steady_clock::time_point started, double frameMs) {
        double wait = frameMs - sinceMs(started);
        if (wait >= 0.5) {
            SDL_DelayPrecise((Uint64) (wait * 1000000.0));
        }
    }

    void holdUntil(std::chrono::steady_clock::time_point started, double frameMs) {
        if (sinceMs(started) < frameMs * 0.75) {
            sleepUntil(started, frameMs - 0.3);
        }
    }

    void releaseMemory() {
#if defined(__GLIBC__)
        malloc_trim(0);
#endif
    }

    bool interacted() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
            io.InputQueueCharacters.Size > 0) {
            return true;
        }
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
            if (ImGui::IsKeyDown((ImGuiKey) key)) {
                return true;
            }
        }
        return false;
    }

    int pixels(float layout, float scale) {
        return (int) (layout * scale + 0.5f);
    }

    float storedSize(const char* key, float fallback) {
        const nlohmann::json& doc = Config::data();
        if (auto it = doc.find(key); it != doc.end() && it->is_number()) {
            float size = it->get<float>();
            if (std::isfinite(size)) return std::max(fallback, size);
        }
        return fallback;
    }

    bool storedMaximized() {
        return b_launcher_base::json_bool(Config::data(), "windowMaximized");
    }

    bool storeWindow(const SdlWindow& window, float scale) {
        bool maximized = window.maximized();
        nlohmann::json& doc = Config::data();
        auto matches = [&](const char* key, const auto& value) {
            auto found = doc.find(key);
            return found != doc.end() && *found == value;
        };
        if (maximized || window.minimized()) {
            if (matches("windowMaximized", maximized) || !maximized) {
                return false;
            }
            doc["windowMaximized"] = true;
            return true;
        }
        float w = std::round(std::max(MainWindow::MIN_WIDTH, window.width / scale));
        float h = std::round(std::max(MainWindow::MIN_HEIGHT, window.height / scale));
        if (matches("windowWidth", w) && matches("windowHeight", h) && matches("windowMaximized", false)) {
            return false;
        }
        doc["windowWidth"] = w;
        doc["windowHeight"] = h;
        doc["windowMaximized"] = false;
        return true;
    }

    float displayScale() {
        float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        return scale > 0.0f ? scale : 1.0f;
    }

    SDL_Rect usableArea() {
        SDL_Rect area{0, 0, 1280, 800};
        SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area);
        return area;
    }

    class TrayPanel {
    public:
        explicit TrayPanel(SdlWindow& window) : window(window) {}
        ~TrayPanel() {
            removeTray();
        }
        TrayPanel(const TrayPanel&) = delete;
        TrayPanel& operator=(const TrayPanel&) = delete;

        bool showWanted = false;
        bool mainWanted = false;
        bool quitWanted = false;

        bool active() const {
            return on;
        }

        bool opaque() const {
            return on && savedFramed;
        }

        void enter(float layoutScale, bool reveal, bool maximized) {
            if (on || window.window == nullptr) {
                return;
            }
            scale = layoutScale;
            savedMaximized = maximized || window.maximized();
            savedFramed = window.framed;
            if (window.maximized()) {
                savedW = pixels(storedSize("windowWidth", MainWindow::MIN_WIDTH), scale);
                savedH = pixels(storedSize("windowHeight", MainWindow::MIN_HEIGHT), scale);
                SDL_RestoreWindow(window.window);
            } else {
                savedW = window.width;
                savedH = window.height;
            }
            SDL_GetWindowPosition(window.window, &savedX, &savedY);
            window.hide();
            window.resizable = false;
            SDL_SetWindowResizable(window.window, false);
            if (window.framed) {
                window.framed = false;
                SDL_SetWindowBordered(window.window, false);
            }
            SDL_SetWindowAlwaysOnTop(window.window, true);
            on = true;
            Compact::setActive(true);
            addTray();
            place();
            if (reveal) {
                window.raise();
            }
        }

        void leave(int minWidth, int minHeight) {
            if (!on || window.window == nullptr) {
                return;
            }
            on = false;
            Compact::setActive(false);
            removeTray();
            window.hide();
            SDL_SetWindowAlwaysOnTop(window.window, false);
            if (savedFramed) {
                window.framed = true;
                SDL_SetWindowBordered(window.window, true);
            }
            window.resizable = true;
            SDL_SetWindowResizable(window.window, true);
            window.resize(savedW, savedH);
            window.setMinimumSize(minWidth, minHeight);
            SDL_SetWindowPosition(window.window, savedX, savedY);
            window.show(savedMaximized);
        }

        void show() {
            if (on) {
                place();
                window.raise();
            }
        }

        void hide() {
            if (on && window.visible()) {
                window.hide();
                Compact::hidden();
            }
        }

        void focusLost() {
            if (tray != nullptr) {
                hide();
            }
        }

        void fit(float layoutScale) {
            scale = layoutScale;
            if (on) {
                place();
            }
        }

        void setIcon(const std::vector<unsigned char>& rgba) {
            icon = rgba;
            if (tray == nullptr) {
                return;
            }
            if (SDL_Surface* surface = iconSurface()) {
                SDL_SetTrayIcon(tray, surface);
                SDL_DestroySurface(surface);
            }
        }

        void setTip(const std::string& text) {
            tip = text;
            if (tray != nullptr) {
                SDL_SetTrayTooltip(tray, tip.c_str());
            }
        }

        void removeTray() {
            if (tray != nullptr) {
                SDL_DestroyTray(tray);
                tray = nullptr;
            }
        }

    private:
        SDL_Surface* iconSurface() {
            if (icon.size() < (size_t) ICON_SIZE * ICON_SIZE * 4) {
                return nullptr;
            }
            return SDL_CreateSurfaceFrom(ICON_SIZE, ICON_SIZE, SDL_PIXELFORMAT_RGBA32, icon.data(), ICON_SIZE * 4);
        }

        void entry(SDL_TrayMenu* menu, const char* label, SDL_TrayCallback callback) {
            if (SDL_TrayEntry* item = SDL_InsertTrayEntryAt(menu, -1, label, SDL_TRAYENTRY_BUTTON)) {
                SDL_SetTrayEntryCallback(item, callback, this);
            }
        }

        void addTray() {
            if (tray != nullptr) {
                return;
            }
            SDL_Surface* surface = iconSurface();
            tray = SDL_CreateTray(surface, tip.c_str());
            if (surface != nullptr) {
                SDL_DestroySurface(surface);
            }
            SDL_TrayMenu* menu = tray != nullptr ? SDL_CreateTrayMenu(tray) : nullptr;
            if (menu == nullptr) {
                return;
            }
            entry(menu, "显示面板", [](void* self, SDL_TrayEntry*) { ((TrayPanel*) self)->showWanted = true; });
            entry(menu, "打开主窗口", [](void* self, SDL_TrayEntry*) { ((TrayPanel*) self)->mainWanted = true; });
            SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);
            entry(menu, "退出", [](void* self, SDL_TrayEntry*) { ((TrayPanel*) self)->quitWanted = true; });
        }

        void place() {
            SDL_DisplayID display = SDL_GetDisplayForWindow(window.window);
            if (display == 0) {
                display = SDL_GetPrimaryDisplay();
            }
            SDL_Rect usable{0, 0, 1280, 800};
            SDL_Rect bounds{0, 0, 1280, 800};
            SDL_GetDisplayUsableBounds(display, &usable);
            SDL_GetDisplayBounds(display, &bounds);
            window.resize(pixels(Compact::WIDTH, scale), pixels(Compact::HEIGHT, scale));
            float density = std::max(0.01f, window.density());
            int w = (int) (window.width / density + 0.5f);
            int h = (int) (window.height / density + 0.5f);
            const int gap = 12;
            bool top = usable.y > bounds.y || usable.y + usable.h >= bounds.y + bounds.h;
            int x = std::max(usable.x, usable.x + usable.w - w - gap);
            int y = top ? usable.y + gap : std::max(usable.y, usable.y + usable.h - h - gap);
            SDL_SetWindowPosition(window.window, x, y);
        }

        SdlWindow& window;
        SDL_Tray* tray = nullptr;
        bool on = false;
        float scale = 1.0f;
        int savedX = 0;
        int savedY = 0;
        int savedW = 0;
        int savedH = 0;
        bool savedMaximized = false;
        bool savedFramed = false;
        std::vector<unsigned char> icon;
        std::string tip;
    };
}

namespace ImGuiApp {

    int run(const std::string& pendingLaunch, bool headless) {
        bool launchSent = false;
        bool launchFailed = false;
        SDL_SetHint(SDL_HINT_APP_ID, "b-launcher");
        SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            return 1;
        }
        float dpiScale = displayScale();
        float scale = dpiScale * Appearance::zoom() / 100.0f;
        SdlWindow window;
        window.resizable = true;
        window.framed = Appearance::systemFrameRunning();
        window.isDragArea = [&scale](int x, int y) { return MainWindow::isDragArea(x / scale, y / scale); };
        bool remember = Appearance::rememberWindow();
        SDL_Rect area = usableArea();
        float startW = std::min(remember ? storedSize("windowWidth", MainWindow::MIN_WIDTH) : MainWindow::MIN_WIDTH,
                                std::max(MainWindow::MIN_WIDTH, area.w * dpiScale / scale));
        float startH = std::min(remember ? storedSize("windowHeight", MainWindow::MIN_HEIGHT) : MainWindow::MIN_HEIGHT,
                                std::max(MainWindow::MIN_HEIGHT, area.h * dpiScale / scale));
        bool startMaximized = remember && storedMaximized();
        std::string shownTitle = Appearance::windowTitle();
        LogWindow::setTitle(Platform::widen(shownTitle));
        if (!window.create(shownTitle, pixels(startW, scale), pixels(startH, scale))) {
            SDL_Quit();
            return 1;
        }
        if (window.displayScale() != dpiScale) {
            dpiScale = window.displayScale();
            scale = dpiScale * Appearance::zoom() / 100.0f;
            window.resize(pixels(startW, scale), pixels(startH, scale));
        }
        window.makeCurrent();
        window.setVsync(true);
        window.setMinimumSize(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
        Platform::window = window.window;
        Platform::claimLaunches();
        window.onDrop = [](std::vector<std::filesystem::path> files, int x, int y) {
            Drops::dropped(std::move(files), x, y);
        };
        window.onDragOver = [](bool inside, int x, int y) { Drops::hover(inside, x, y); };
        window.onDropText = [](const std::string& text) { Drops::droppedText(text); };
        TrayPanel panel(window);
        panel.setTip(shownTitle);
        panel.setIcon(Logo::iconPixels(ICON_SIZE, Appearance::systemFrameRunning()));
        window.onFocusLost = [&panel]() { panel.focusLost(); };
        auto toFull = [&]() {
            panel.leave(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
        };
        auto reveal = [&]() {
            if (panel.active()) {
                panel.show();
            } else {
                window.show(startMaximized);
            }
        };
        auto follow = [&]() {
            bool compact = false;
            if (Compact::takeRequest(compact)) {
                if (compact) {
                    panel.enter(scale, true, false);
                } else {
                    toFull();
                }
            }
            if (Compact::takeDismiss()) {
                panel.hide();
            }
            if (Compact::takeReveal()) {
                panel.show();
            }
        };

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        window.initImGui();
        UiGL::load();
        UiFont::init();
        Appearance::load();
        MainWindow::init();
        Services::start();
        if (!pendingLaunch.empty()) {
            ShortcutLaunch::request(pendingLaunch, headless);
        }
        window.imguiContext = ImGui::GetCurrentContext();
        LogWindow::init(&window);
        Drop::attach(window.window);
        bool shown = false;
        uint32_t iconAccent = 0;
        int iconRevision = -1;
        auto lastInput = std::chrono::steady_clock::now();
        auto lastCompact = std::chrono::steady_clock::now();
        bool released = false;
        double frameMs = window.refreshMs();
        int framesSinceRefresh = 0;

        auto frame = [&]() {
            float next = window.displayScale() * Appearance::zoom() / 100.0f;
            if (next != scale) {
                float layoutW = window.width / scale;
                float layoutH = window.height / scale;
                scale = next;
                if (panel.active()) {
                    panel.fit(scale);
                } else {
                    window.setMinimumSize(pixels(MainWindow::MIN_WIDTH, scale), pixels(MainWindow::MIN_HEIGHT, scale));
                    if (!window.maximized()) {
                        window.resize(pixels(std::max(MainWindow::MIN_WIDTH, layoutW), scale),
                                      pixels(std::max(MainWindow::MIN_HEIGHT, layoutH), scale));
                    }
                }
            }
            MainWindow::setSize(window.width / scale, window.height / scale, window.maximized());
            bool opaque = window.framed || window.maximized() || panel.opaque();
            MainWindow::cornerRadius = opaque ? 0.0f : CORNER_RADIUS;

            window.makeCurrent();
            window.newFrame();
            ImGui::NewFrame();
            UiState::beginFrame();
            if (interacted()) {
                lastInput = std::chrono::steady_clock::now();
            }
            Services::poll();
            Background::poll();
            Logo::poll();
            Homepage::poll();
            if (std::optional<ShortcutLaunch::Outcome> outcome = ShortcutLaunch::take()) {
                launchSent = outcome->launched;
                launchFailed = !outcome->launched;
                if (!outcome->launched && outcome->quiet && !headless) {
                    window.raise();
                }
            }
            UiRender::uiScale = scale / (float) UiRender::guiScale();
            UiRender::begin(ImGui::GetForegroundDrawList());
            NewUi::render(panel.active() ? Compact::draw : MainWindow::draw);
            Page::forgetHidden();
            UiRender::end();

            ImGui::Render();
            window.setTyping(!NewUi::textEditing.empty() || !UiInput::focused.empty());
            if (NewUi::caretFrame == NewUi::frameCounter) {
                window.setTextArea(NewUi::caretScreenX, NewUi::caretScreenY, NewUi::caretScreenH);
            } else {
                window.clearTextArea();
            }
            UiGL::Viewport(0, 0, window.width, window.height);
            UiGL::ClearColor(0.0f, 0.0f, 0.0f, opaque ? 1.0f : 0.0f);
            UiGL::Clear(UGL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            window.swap();
            if (!shown) {
                shown = true;
                if (!headless) {
                    reveal();
                }
            }
            LogWindow::frame(scale);

            uint32_t accent = UiTheme::accent() | 0xFF000000u;
            if (accent != iconAccent || Logo::iconRevision() != iconRevision) {
                iconAccent = accent;
                iconRevision = Logo::iconRevision();
                std::vector<unsigned char> rgba = Logo::iconPixels(ICON_SIZE, Appearance::systemFrameRunning());
                window.setIcon(rgba, ICON_SIZE);
                panel.setIcon(rgba);
                LogWindow::setIcons(nullptr, nullptr);
            }
            if (std::string title = Appearance::windowTitle(); title != shownTitle) {
                shownTitle = title;
                window.setTitle(title);
                LogWindow::setTitle(Platform::widen(title));
                panel.setTip(title);
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
            follow();
            if (Appearance::rememberWindow() && !panel.active() && window.visible() && storeWindow(window, scale)) {
                Config::save();
            }
        };
        bool closingRoom = false;
        bool exiting = false;
        auto closingSince = std::chrono::steady_clock::now();
        auto finishClose = [&]() {
            closingRoom = false;
            if (Appearance::rememberWindow() && !panel.active() && window.visible()) {
                storeWindow(window, scale);
            }
            panel.removeTray();
            window.hide();
            exiting = true;
        };
        window.onClose = [&]() {
            if (!closingRoom && Multiplayer::active()) {
                closingRoom = true;
                closingSince = std::chrono::steady_clock::now();
                Multiplayer::leave();
                Notices::add(Notices::Kind::Info, "正在关闭房间，关闭后启动器会退出", 4000);
                return;
            }
            finishClose();
        };

        if (Compact::stored()) {
            panel.enter(scale, false, startMaximized);
        }
        while (window.pumpMessages() && !exiting) {
            if (std::exchange(panel.showWanted, false)) {
                panel.show();
            }
            if (std::exchange(panel.mainWanted, false)) {
                toFull();
            }
            if (std::exchange(panel.quitWanted, false) && window.onClose) {
                window.onClose();
                continue;
            }
            if (closingRoom &&
                (Multiplayer::state().role == Multiplayer::Role::None || sinceMs(closingSince) > ROOM_CLOSE_MS)) {
                finishClose();
                continue;
            }
            GameLog::poll();
            if (window.minimized() || (shown && !headless && !window.visible())) {
                if (!released) {
                    released = true;
                    releaseMemory();
                }
                Services::poll();
                follow();
                LogWindow::frame(scale);
                SDL_Delay(LogWindow::isOpen() ? 15 : 10);
                continue;
            }
            auto started = std::chrono::steady_clock::now();
            frame();
            // Another launcher process showed the window during a headless launch, so keep it open.
            if (ShortcutLaunch::takeShown() && headless) {
                headless = false;
                Game::setQuiet(false);
            }
            if (headless) {
                if (launchSent && Game::running()) {
                    headless = false;
                    window.close();
                } else if (launchFailed || (launchSent && !Game::active())) {
                    headless = false;
                    Game::setQuiet(false);
                    reveal();
                }
            }
            if (sinceMs(lastInput) > 20000.0 && sinceMs(lastCompact) > 180000.0) {
                lastCompact = std::chrono::steady_clock::now();
                UiFont::compact();
            }
            bool foreground = window.focused();
            if (foreground) {
                released = false;
            } else if (!released && sinceMs(lastInput) > 30000.0) {
                released = true;
                releaseMemory();
            }
            if (++framesSinceRefresh >= 120) {
                framesSinceRefresh = 0;
                frameMs = window.refreshMs();
            }
            bool lively = UiState::animating() || Notifications::active() || sinceMs(lastInput) < INPUT_GRACE_MS ||
                    Game::preparing();
            if (lively && foreground) {
                holdUntil(started, frameMs);
            } else {
                sleepUntil(started, BACKGROUND_FRAME_MS);
                if (!lively) {
                    idleUntil(started, IDLE_FRAME_MS);
                }
            }
        }

        window.onFocusLost = nullptr;
        panel.removeTray();
        MainWindow::save();
        Config::save();

        window.makeCurrent();
        LogWindow::shutdown();
        UiImages::shutdown();
        Homepage::shutdown();
        Background::shutdown();
        Logo::shutdown();
        Services::stop();
        window.shutdownImGui();
        ImGui::DestroyContext();
        window.destroy();
        SDL_Quit();
        return 0;
    }
}
